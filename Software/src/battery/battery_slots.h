#ifndef BATTERY_SLOTS_H
#define BATTERY_SLOTS_H

#include <stdint.h>

#include "../system_settings.h"

class Battery;

// Null means the slot is not configured/initialised.
extern Battery* batteries[kMaxBatterySlots];

// Null when the slot is out of range or not populated.
inline Battery* battery_at(uint8_t slot) {
  return slot < kMaxBatterySlots ? batteries[slot] : nullptr;
}

#endif
