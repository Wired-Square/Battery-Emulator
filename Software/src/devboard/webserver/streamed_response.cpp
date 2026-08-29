#include "streamed_response.h"

#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>

#include <atomic>

#include "../hal/hal.h"
#include "json_response_writer.h"

namespace {

constexpr size_t kStreamBufferBytes = 4096;
constexpr size_t kProducerStackBytes = 8192;
constexpr UBaseType_t kProducerPriority = 2;
// The async task waits this long for the producer rather than returning
// RESPONSE_TRY_AGAIN, which costs a 125 ms poll interval of dead air.
constexpr TickType_t kFillWaitTicks = pdMS_TO_TICKS(20);
constexpr TickType_t kSendWaitTicks = pdMS_TO_TICKS(100);

StaticStreamBuffer_t stream_meta;
uint8_t stream_storage[kStreamBufferBytes + 1];
StreamBufferHandle_t stream = nullptr;

StaticTask_t producer_task_buffer;
StackType_t producer_stack[kProducerStackBytes];
TaskHandle_t producer_task = nullptr;

ResponseProducer active_producer;
std::atomic<bool> producer_busy{false};
std::atomic<bool> producer_done{true};
std::atomic<bool> producer_cancelled{false};
AsyncWebServerRequest* stream_owner = nullptr;

// Discards everything once the client is gone: the producer is straight-line
// driver code that cannot be unwound, so cancellation stops the output rather
// than the task, and the task retires normally a moment later.
class StreamSink : public ResponseSink {
 public:
  void write(const char* text, size_t len) override {
    while (len > 0 && !producer_cancelled.load(std::memory_order_relaxed)) {
      const size_t sent = xStreamBufferSend(stream, text, len, kSendWaitTicks);
      text += sent;
      len -= sent;
    }
  }
};

void producer_entry(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    {
      StreamSink sink;
      JsonResponseWriter writer(sink);
      active_producer(writer);
      writer.flush();
    }
    active_producer = nullptr;
    producer_done.store(true, std::memory_order_release);
  }
}

bool start_producer(ResponseProducer& producer) {
  if (stream == nullptr) {
    stream = xStreamBufferCreateStatic(kStreamBufferBytes, 1, stream_storage, &stream_meta);
    if (stream == nullptr) {
      return false;
    }
  }
  if (producer_task == nullptr) {
    producer_task = xTaskCreateStaticPinnedToCore(producer_entry, "web_stream", kProducerStackBytes, nullptr,
                                                  kProducerPriority, producer_stack, &producer_task_buffer,
                                                  esp32hal->CORE_FUNCTION_CORE());
    if (producer_task == nullptr) {
      return false;
    }
  }
  if (producer_busy.exchange(true)) {
    return false;
  }
  if (!producer_done.load(std::memory_order_acquire)) {
    producer_busy.store(false);
    return false;
  }
  xStreamBufferReset(stream);
  active_producer = std::move(producer);
  producer_cancelled.store(false, std::memory_order_relaxed);
  producer_done.store(false, std::memory_order_release);
  xTaskNotifyGive(producer_task);
  return true;
}

void release_stream() {
  stream_owner = nullptr;
  producer_busy.store(false, std::memory_order_release);
}

size_t fill_from_producer(uint8_t* data, size_t max_len, size_t) {
  const size_t received = xStreamBufferReceive(stream, data, max_len, kFillWaitTicks);
  if (received > 0) {
    return received;
  }
  if (producer_done.load(std::memory_order_acquire) && xStreamBufferIsEmpty(stream) == pdTRUE) {
    // The slot is freed here rather than on disconnect: a kept-alive connection
    // may not disconnect for minutes, and until it does every other endpoint
    // would silently fall back to the buffered writer.
    release_stream();
    return 0;
  }
  return RESPONSE_TRY_AGAIN;
}

// Fires on every teardown, including one after a later request has taken the
// slot, so it cancels only its own stream.
void cancel_stream(AsyncWebServerRequest* request) {
  if (stream_owner != request) {
    return;
  }
  producer_cancelled.store(true, std::memory_order_relaxed);
  release_stream();
}

}  // namespace

void send_streamed(AsyncWebServerRequest* request, const char* content_type, ResponseProducer producer) {
  if (!start_producer(producer)) {
    return request->send(200, content_type, render_json(producer));
  }
  stream_owner = request;
  request->onDisconnect([request]() { cancel_stream(request); });
  request->send(request->beginChunkedResponse(content_type, fill_from_producer));
}
