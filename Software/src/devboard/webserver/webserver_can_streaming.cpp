#include "webserver_can_streaming.h"
#include "../../communication/can/comm_can.h"
#include "webserver.h"

#include <atomic>

// ---------------------------------------------------------------------------
// Streaming CAN dump (/dump_can)
//
// A browser connects to /dump_can and the request deliberately "hangs": we
// never call request->send(), so the connection stays open indefinitely. As CAN
// frames arrive, stream_can_frame() (called from the core loop) writes the
// formatted log lines straight to the raw TCP socket of that one connection.
//
// Only a single concurrent connection is allowed; starting a new one closes any
// previous stream. The AsyncTCP layer protects the socket itself with its own
// write mutex, so writing from the CAN task is thread-safe. We still guard our
// client pointer with a separate mutex so the disconnect callback (which runs on
// the TCP task, before the AsyncClient is freed) can safely tear the stream down
// without a race or use-after-free.
// ---------------------------------------------------------------------------
static AsyncClient* can_dump_client = nullptr;
static SemaphoreHandle_t can_dump_mutex = nullptr;

// Replace the active streaming client. Returns the previously active client (if
// any); the caller is responsible for closing it.
static AsyncClient* can_dump_swap_client(AsyncClient* client) {
  AsyncClient* prev = nullptr;
  if (can_dump_mutex)
    xSemaphoreTake(can_dump_mutex, portMAX_DELAY);
  prev = can_dump_client;
  can_dump_client = client;
  if (can_dump_mutex)
    xSemaphoreGive(can_dump_mutex);
  return prev;
}

// Close and clear the current streaming client. Closing the socket is async, so
// its disconnect callback may fire later; that callback matches by pointer below
// and so will not clobber a client installed after us.
static void can_dump_close_current() {
  AsyncClient* c = can_dump_swap_client(nullptr);
  datalayer.system.info.can_streaming_active = false;
  if (c != nullptr) {
    c->close();
  }
}

// The AsyncTCP send buffer isn't very big, and calling it is somewhat
// expensive, so we use a larger ring buffer to store several CAN frames to then
// be sent in larger chunks where possible.
//
// This setting can be adjusted as available RAM permits - 16384 gives great
// performance in testing, 8192 gives occasional overruns.
static constexpr size_t CAN_DUMP_RING_SIZE = 8192;

// Always keep this many bytes free at the tail end of the ring.  Each byte is
// one '\n' overflow-indicator, so this guarantees the drain task can still
// signal 30 overrun events even when the buffer is nearly saturated.
static constexpr size_t CAN_DUMP_RING_OVERRUN_RESERVE = 30;

static uint8_t can_dump_ring[CAN_DUMP_RING_SIZE];

// Single-producer / single-consumer lock-free byte ring.
//  - CAN receive task is the ONLY producer and only writes can_dump_ring_tail.
//  - drain task is the ONLY consumer and only writes can_dump_ring_head.
// Indices are monotonic byte counters (never wrapped), so "used = tail - head"
// is always well-defined; each side needs only a relaxed load of the other's
// index plus a release/acquire around the buffer copy.
static std::atomic<size_t> can_dump_ring_head{0};
static std::atomic<size_t> can_dump_ring_tail{0};

// Orders the stream-start index reset against an in-flight producer write: a
// producer that loaded tail before the reset would otherwise store a stale tail
// after it, and "used = tail - head" would then read as garbage.
static std::atomic_flag can_dump_ring_resetting = ATOMIC_FLAG_INIT;

static bool __attribute__((noinline)) can_dump_ring_try_lock() {
  return can_dump_ring_resetting.test_and_set(std::memory_order_acquire);
}

// Blocking acquisition - only ever called from task context (stream start).
// Never taken from an ISR.
static void can_dump_ring_lock() {
  while (can_dump_ring_try_lock()) {
    vTaskDelay(1);
  }
}

static void can_dump_ring_unlock() {
  can_dump_ring_resetting.clear(std::memory_order_release);
}

// Set by the /dump_can handler when a brand-new client is installed: the drain
// task must drop any stale bytes still in the ring before streaming resumes.
static std::atomic<bool> can_dump_reset_pending{false};

// Called ONLY from the CAN receive task, never blocks.
static void can_dump_ring_push(const char* line, size_t len) {
  // Non-blocking: if the stream-start path owns the lock right now, drop this
  // frame rather than stalling the CAN task.
  if (can_dump_ring_try_lock())
    return;

  const size_t tail = can_dump_ring_tail.load(std::memory_order_relaxed);
  const size_t head = can_dump_ring_head.load(std::memory_order_acquire);
  const size_t used = tail - head;
  const size_t free = CAN_DUMP_RING_SIZE - used;

  if (free >= len + CAN_DUMP_RING_OVERRUN_RESERVE) {
    // Append the whole line, wrapping at the ring boundary. Safe to split
    // across the wrap: we already checked total free >= len + reserve, so the
    // write can never reach live (unread) data.
    size_t i = tail % CAN_DUMP_RING_SIZE;
    size_t n1 = CAN_DUMP_RING_SIZE - i;
    if (n1 > len)
      n1 = len;
    memcpy(&can_dump_ring[i], line, n1);
    if (len > n1) {
      memcpy(&can_dump_ring[0], line + n1, len - n1);
    }
    can_dump_ring_tail.store(tail + len, std::memory_order_release);
  } else if (free > 0) {
    // Not enough room for a whole line, but there is still space beyond the
    // reserve for a newline indicator. These are useful signals of overflow
    // that are compatible with the log format.
    can_dump_ring[tail % CAN_DUMP_RING_SIZE] = '\n';
    can_dump_ring_tail.store(tail + 1, std::memory_order_release);
  }
  // Otherwise we just silently drop the line.

  can_dump_ring_resetting.clear(std::memory_order_release);
}

// Move as many buffered bytes as AsyncTCP can currently accept into the client.
// Returns false only if the socket is gone / could not be written to.
static bool can_dump_ring_drain(AsyncClient* client) {
  const size_t tail = can_dump_ring_tail.load(std::memory_order_acquire);
  size_t head = can_dump_ring_head.load(std::memory_order_relaxed);
  size_t used = tail - head;
  while (used > 0) {
    size_t space = client->space();
    if (space == 0)
      break;  // AsyncTCP full; leave the rest for the next poll.
    size_t i = head % CAN_DUMP_RING_SIZE;
    size_t n = CAN_DUMP_RING_SIZE - i;
    if (n > used)
      n = used;
    if (n > space)
      n = space;
    size_t written = client->write(reinterpret_cast<const char*>(&can_dump_ring[i]), n, ASYNC_WRITE_FLAG_COPY);
    if (written == 0) {
      return false;  // Socket broken or AsyncTCP can no longer accept data.
    }
    head += written;
    can_dump_ring_head.store(head, std::memory_order_release);
    used = tail - head;
  }
  return true;
}

// Attempt to drain some of the ring to the active client. If the client is
// gone, or if the socket is broken, stop streaming until a new connection
// appears. This is called from the connectivity loop.
void can_dump_drain_tick() {
  // Wait at most 1ms for the mutex
  if (xSemaphoreTake(can_dump_mutex, 1 / portTICK_PERIOD_MS) != pdTRUE) {
    return;  // Failed to take the mutex, bail
  }

  AsyncClient* client = can_dump_client;

  if (can_dump_reset_pending.exchange(false)) {
    // Drop anything buffered by the previous stream before allowing new frames.
    const size_t tail = can_dump_ring_tail.load(std::memory_order_acquire);
    can_dump_ring_head.store(tail, std::memory_order_release);
    if (client != nullptr) {
      datalayer.system.info.can_streaming_active = true;
    }
  }

  if (client != nullptr && datalayer.system.info.can_streaming_active) {
    if (!can_dump_ring_drain(client)) {
      // Client is gone / the socket broke. Stop dumping until a new connection appears.
      can_dump_client = nullptr;
      datalayer.system.info.can_streaming_active = false;
    }
  }

  xSemaphoreGive(can_dump_mutex);
}

// Output a CAN frame to the active stream, if present. The line itself is
// formatted by the shared format_can_frame() (see comm_can.h).
void stream_can_frame(const CAN_frame& frame, uint8_t log_id, frameDirection msgDir) {
  if (!datalayer.system.info.can_streaming_active) {
    return;
  }

  char line[230];
  const size_t len = format_can_frame(line, sizeof(line), frame, log_id, msgDir);
  if (len > 0) {
    // Lock-free enqueue into the ring; the drain task handles the actual socket I/O.
    can_dump_ring_push(line, len);
  }
}

// ---------------------------------------------------------------------------
// Streaming CAN dump
// ---------------------------------------------------------------------------
void register_dump_can_route(AsyncWebServer& server) {
  if (can_dump_mutex == nullptr) {
    can_dump_mutex = xSemaphoreCreateMutex();
  }

  server.on("/dump_can", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (webserver_auth_is_ready() && !request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication(AsyncAuthType::AUTH_BASIC, WEB_AUTH_REALM);
    }

    // Only one concurrent stream is allowed: drop any previous one.
    can_dump_close_current();

    AsyncClient* client = request->client();

    can_dump_ring_lock();
    can_dump_ring_head.store(0, std::memory_order_release);
    can_dump_ring_tail.store(0, std::memory_order_release);

    // We leave can_streaming_active gated OFF until the drain task has dropped
    // any stale bytes and re-opened the gate, so a fresh stream never inherits
    // lines from the previous tenant.
    can_dump_swap_client(client);
    can_dump_reset_pending.store(true, std::memory_order_relaxed);
    can_dump_ring_unlock();

    // Tell the framework not to auto-send a response when this handler returns:
    // otherwise it would inject "501 Not Implemented / Handler did not handle
    // the request" once we deliberately skip request->send().
    request->setStreamingResponse(true);

    // Send the response headers ourselves and deliberately leave the connection
    // open (we never call request->send()). The request/response machinery is
    // bypassed entirely; we stream raw bytes on the socket from stream_can_frame().
    const char headers[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: close\r\n"
        "\r\n";
    client->write(headers, sizeof(headers) - 1, ASYNC_WRITE_FLAG_COPY);

    // On disconnect (browser closes, or this connection is replaced) this runs on
    // the TCP task before the framework deletes the AsyncClient. We only clear our
    // active-client pointer if it still refers to *this* connection, so replacing
    // a stream doesn't clobber its successor.
    request->onDisconnect([client]() {
      AsyncClient* c = can_dump_swap_client(nullptr);
      if (c != nullptr && c != client) {
        // A newer connection has already taken over; restore it and leave logging on.
        can_dump_swap_client(c);
        return;
      }
      // This (or a now-cleared) client was the active stream, so tear everything down.
      datalayer.system.info.can_streaming_active = false;
    });
  });
}
