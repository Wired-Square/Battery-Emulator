#include "comm_can.h"
#include <Arduino.h>
#include "CanReceiver.h"
#include "src/datalayer/datalayer.h"
#include "src/devboard/hal/hal.h"
#include "src/devboard/safety/safety.h"
#include "src/devboard/sdcard/sdcard.h"
#include "src/devboard/utils/logging.h"
#include "src/devboard/webserver/webserver_can_streaming.h"

volatile CAN_Configuration can_config = {};

static void dump_can_frame_by_id(const CAN_frame& frame, uint8_t log_id, frameDirection msgDir);
static void print_can_frame(const CAN_frame& frame, uint8_t log_id, frameDirection msgDir);

bool use_canfd_as_can = false;
bool use_canfd2_as_can = false;
//CAN logging filter settings
uint16_t user_selected_CAN_ID_cutoff_filter = 0;  //Messages below this ID will not be logged in webserver

void register_can_receiver(CanReceiver* receiver, const InterfaceDescriptor* interface, CAN_Speed speed) {
  if (interface != nullptr && interface->can_bus != nullptr) {
    interface->can_bus->register_receiver(receiver, speed);
  }
}

bool init_CAN() {
  bool ok = true;
  for_each_unique_binding(esp32hal->interfaces(), &InterfaceDescriptor::can_bus, [&ok](CanBus* bus) {
    if (bus->has_receivers() && !bus->init()) {
      ok = false;
      return false;
    }
    return true;
  });
  return ok;
}

void transmit_can_frame_to_interface(const CAN_frame* tx_frame, const InterfaceDescriptor* interface) {
  if (!allowed_to_send_CAN) {
    return;
  }
  CanBus* bus = interface != nullptr ? interface->can_bus : nullptr;
  log_can_frame(*tx_frame, bus != nullptr ? bus->log_id() : CAN_LOG_ID_NONE, MSG_TX);
  if (bus != nullptr && bus->initialized()) {
    bus->transmit_frame(*tx_frame);
  }
}

// Receive functions
void receive_can() {
  for_each_unique_binding(esp32hal->interfaces(), &InterfaceDescriptor::can_bus, [](CanBus* bus) {
    if (bus->initialized()) {
      bus->receive();
    }
    return true;
  });
}

// Support functions
static void print_can_frame(const CAN_frame& frame, uint8_t log_id, frameDirection msgDir) {

  if (datalayer.system.info.CAN_usb_logging_active) {
    // Build the whole line first, then write it in one go - and only if the TX
    // buffer has room. This path runs in the core task: a blocked/slow USB host
    // must never stall it (EVENT_TASK_OVERRUN). Frames that don't fit are
    // counted and reported as a gap marker once the port drains.
    static char usb_line[288];  // header + up to 64 CAN-FD data bytes at 3 chars each
    static uint32_t usb_frames_dropped = 0;
    unsigned long currentTime = millis();
    size_t size = snprintf(usb_line, sizeof(usb_line), "(%lu.%02lu) %s%d %lX [%u] ", currentTime / 1000,
                           (currentTime % 1000) / 10, (msgDir == MSG_RX) ? "RX" : "TX",
                           (msgDir == MSG_RX) ? (int)(log_id * 2) : (int)(log_id * 2) + 1, frame.ID, frame.DLC);
    for (uint8_t i = 0; i < frame.DLC; i++) {
      size += snprintf(usb_line + size, sizeof(usb_line) - size, (i < frame.DLC - 1) ? "%02X " : "%02X\r\n",
                       frame.data.u8[i]);
    }
    if (frame.DLC == 0) {
      size += snprintf(usb_line + size, sizeof(usb_line) - size, "\r\n");
    }

    if ((size_t)Serial.availableForWrite() >= size) {
      if (usb_frames_dropped > 0) {
        char marker[48];
        int marker_len =
            snprintf(marker, sizeof(marker), "[%lu CAN frames not printed]\r\n", (unsigned long)usb_frames_dropped);
        if ((size_t)Serial.availableForWrite() >= size + (size_t)marker_len) {
          Serial.write((const uint8_t*)marker, marker_len);
          usb_frames_dropped = 0;
        }
      }
      Serial.write((const uint8_t*)usb_line, size);
    } else {
      usb_frames_dropped++;
    }
  }

  if (datalayer.system.info.can_logging_active) {  // If user clicked on CAN Logging page in webserver, start recording
    if (frame.ID > user_selected_CAN_ID_cutoff_filter) {  //Only log the message if CAN ID is higher than user set value
      dump_can_frame_by_id(frame, log_id, msgDir);
    }
  }

  if (datalayer.system.info.can_streaming_active) {
    stream_can_frame(frame, log_id, msgDir);
  }
}

void log_can_frame(const CAN_frame& frame, uint8_t log_id, frameDirection msgDir) {
  print_can_frame(frame, log_id, msgDir);
#ifdef SDCARD
  if (datalayer.system.info.CAN_SD_logging_active) {
    add_can_frame_to_buffer(frame, log_id, msgDir);
  }
#endif  // SDCARD
}

// For formatting CAN frames considerably faster than using snprintf
static const char* hex = "0123456789abcdef";

static char* put_hex(char* ptr, uint32_t value, uint8_t digits) {
  for (int i = digits - 1; i >= 0; i--) {
    *ptr++ = hex[(value >> (i * 4)) & 0x0f];
  }
  return ptr;
}

static char* put_time(char* ptr, unsigned long time) {
  // Wrap around after 100000 seconds (about 27.7 hours)
  if (time >= 100000000)
    time = time % 100000000;

  char buf[8];
  int i = 0;
  do {
    buf[i++] = (time % 10) + '0';
    time /= 10;
  } while (time > 0);
  while (i > 0) {
    *ptr++ = buf[--i];
    if (i == 3) {
      *ptr++ = '.';
    }
  }
  return ptr;
}

// CAN log formatter: "(12345.678) RX0 123 [8] 01 02 03 ... 0A\n".
size_t format_can_frame(char* buffer, size_t len, const CAN_frame& frame, uint8_t log_id, frameDirection msgDir) {
  // Worst-case line length: '(' + up-to-8-digit time + optional '.' + ')' + ' '
  // + "RX"/"TX" + channel digit + ' ' + 8-hex ID + ' ' + '[' + 2-digit DLC + ']'
  // + 3 bytes per data byte + '\n'.
  const size_t needed = 1 + 9 + 1 + 1 + 3 + 1 + 8 + 1 + 1 + 2 + 1 + (size_t)frame.DLC * 3 + 1;
  if (needed > len) {
    if (len > 0) {
      buffer[0] = '\0';
    }
    return 0;
  }

  char* ptr = buffer;
  const unsigned long currentTime = millis();
  *ptr++ = '(';
  ptr = put_time(ptr, currentTime);
  *ptr++ = ')';
  *ptr++ = ' ';
  if (msgDir == MSG_RX) {
    *ptr++ = frame.FD ? 'R' : 'r';
    *ptr++ = frame.FD ? 'X' : 'x';
    *ptr++ = '0' + ((int)log_id * 2);
  } else {
    *ptr++ = frame.FD ? 'T' : 't';
    *ptr++ = frame.FD ? 'X' : 'x';
    *ptr++ = '1' + ((int)log_id * 2);
  }
  *ptr++ = ' ';
  if (frame.ext_ID)
    ptr = put_hex(ptr, frame.ID, 8);
  else
    ptr = put_hex(ptr, frame.ID, 3);
  *ptr++ = ' ';
  *ptr++ = '[';
  if (frame.DLC > 9) {
    *ptr++ = '0' + (frame.DLC / 10);
    *ptr++ = '0' + (frame.DLC % 10);
  } else
    *ptr++ = '0' + (frame.DLC);
  *ptr++ = ']';
  for (int i = 0; i < frame.DLC; i++) {
    *ptr++ = ' ';
    ptr = put_hex(ptr, frame.data.u8[i], 2);
  }
  *ptr++ = '\n';
  *ptr = '\0';
  return (size_t)(ptr - buffer);
}

static void dump_can_frame_by_id(const CAN_frame& frame, uint8_t log_id, frameDirection msgDir) {
  char* message_string = datalayer.system.info.logged_can_messages;
  size_t offset =
      datalayer.system.info.logged_can_messages_offset;  // Keeps track of the current position in the buffer
  size_t message_string_size = sizeof(datalayer.system.info.logged_can_messages);

  size_t written = format_can_frame(message_string + offset, message_string_size - offset, frame, log_id, msgDir);
  if (written == 0 && offset != 0) {
    // Not enough space left at the tail - wrap around and start from the beginning
    offset = 0;
    written = format_can_frame(message_string, message_string_size, frame, log_id, msgDir);
  }
  if (written > 0) {
    datalayer.system.info.logged_can_messages_offset = offset + written;  // Update offset in buffer
  }
}

void dump_can_frame(CAN_frame& frame, const InterfaceDescriptor* interface, frameDirection msgDir) {
  dump_can_frame_by_id(frame, can_event_interface_id(interface), msgDir);
}

void stop_can() {
  for_each_unique_binding(esp32hal->interfaces(), &InterfaceDescriptor::can_bus, [](CanBus* bus) {
    if (bus->initialized()) {
      bus->stop();
    }
    return true;
  });
}

void restart_can() {
  for_each_unique_binding(esp32hal->interfaces(), &InterfaceDescriptor::can_bus, [](CanBus* bus) {
    if (bus->initialized()) {
      bus->restart();
    }
    return true;
  });
}

bool change_can_speed(const InterfaceDescriptor* interface, CAN_Speed speed) {
  return (interface != nullptr && interface->can_bus != nullptr) ? interface->can_bus->change_speed(speed) : false;
}
