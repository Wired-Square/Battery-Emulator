#include <Arduino.h>

#include "CanBus.h"

#include "CanReceiver.h"
#include "comm_can.h"

void CanBus::register_receiver(CanReceiver* receiver, CAN_Speed receiver_speed) {
  if (receivers_.empty()) {
    speed_ = receiver_speed;
  }
  receivers_.push_back(receiver);
}

void CanBus::dispatch_frame(CAN_frame& frame) {
  last_rx_ms_ = millis();
  log_can_frame(frame, log_id_, MSG_RX);
  for (CanReceiver* receiver : receivers_) {
    receiver->receive_can_frame(&frame);
  }
}

bool CanBus::recently_received(uint32_t hold_ms) const {
  return last_rx_ms_ != 0 && millis() - last_rx_ms_ < hold_ms;
}
