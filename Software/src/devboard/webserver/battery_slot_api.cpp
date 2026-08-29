#include "battery_slot_api.h"

#include "../../battery/battery_slots.h"
#include "../../system_settings.h"

bool battery_slot_addressable(uint8_t slot) {
  return slot < kMaxBatterySlots && (slot == 0 || batteries[slot] != nullptr);
}

const char* validate_battery_slot(const ValueSource& body, uint8_t& slot) {
  const DocumentValue value = body.value("battery");
  if (value.missing()) {
    slot = 0;
    return nullptr;
  }
  if (!value.is_integer_in(INT32_MIN, INT32_MAX)) {
    return "Bad Request";
  }
  if (value.integer < 0 || !battery_slot_addressable(static_cast<uint8_t>(value.integer))) {
    return "Invalid battery";
  }
  slot = static_cast<uint8_t>(value.integer);
  return nullptr;
}
