#include "web_json.h"

#include <string>

#include "../../battery/battery_slots.h"
#include "../../system_settings.h"

bool battery_slot_addressable(uint8_t slot) {
  return slot < kMaxBatterySlots && (slot == 0 || batteries[slot] != nullptr);
}

const char* validate_battery_slot(const JsonDocument& doc, uint8_t& slot) {
  if (doc["battery"].isNull()) {
    slot = 0;
    return nullptr;
  }
  if (!doc["battery"].is<int>()) {
    return "Bad Request";
  }
  const int value = doc["battery"].as<int>();
  if (value < 0 || !battery_slot_addressable(static_cast<uint8_t>(value))) {
    return "Invalid battery";
  }
  slot = static_cast<uint8_t>(value);
  return nullptr;
}

String serialise_doc(const JsonDocument& doc) {
#if ARDUINOJSON_ENABLE_ARDUINO_STRING
  String out;
  serializeJson(doc, out);
  return out;
#else
  std::string out;
  serializeJson(doc, out);
  return String(out);
#endif
}
