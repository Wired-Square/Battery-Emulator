#include <Arduino.h>

#include "CanBus.h"

#include "CanReceiver.h"
#include "comm_can.h"

void CanBus::register_receiver(CanReceiver* receiver, CanRole role, CAN_Speed speed, CanSpeedMode mode) {
  demands_.push_back(CanDemand{receiver, role, speed, mode});
}

CanResolveError CanBus::resolve() {
  if (demands_.empty()) {
    return CanResolveError::Unresolved;
  }
  const CanDemand* agreed = nullptr;
  size_t battery_count = 0;
  bool variable = false;
  for (const CanDemand& demand : demands_) {
    if (demand.mode == CanSpeedMode::Fixed) {
      if (agreed == nullptr) {
        agreed = &demand;
      } else if (demand.speed != agreed->speed) {
        return CanResolveError::SpeedConflict;
      }
    } else {
      variable = true;
    }
    if (demand.role == CanRole::Battery) {
      battery_count++;
    }
  }
  if (variable && demands_.size() > 1) {
    return CanResolveError::VariableShared;
  }
  if (battery_count > 1) {
    return CanResolveError::MultipleBatteries;
  }
  speed_ = agreed != nullptr ? agreed->speed : demands_.front().speed;
  return CanResolveError::None;
}

void CanBus::dispatch_frame(CAN_frame& frame) {
  last_rx_ms_ = millis();
  log_can_frame(frame, log_id_, MSG_RX);
  for (const CanDemand& demand : demands_) {
    demand.receiver->receive_can_frame(&frame);
  }
}

bool CanBus::recently_received(uint32_t hold_ms) const {
  return last_rx_ms_ != 0 && millis() - last_rx_ms_ < hold_ms;
}
