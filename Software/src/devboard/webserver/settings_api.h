#ifndef SETTINGS_API_H
#define SETTINGS_API_H

#include <WString.h>
#include <cstddef>
#include <cstdint>

#include "../../battery/balancing_bounds.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "../utils/types.h"

class BatteryEmulatorSettingsStore;
struct DATALAYER_BATTERY_SETTINGS_TYPE;

// Selects the JSON<->NVS transform. InterfacePacked carries a packed
// interface-config uint that resolves against the live descriptor table.
enum class SettingType : uint8_t {
  Bool,
  Uint,
  Int,
  StringVal,
  EnumUint,
  FloatX10,
  FloatString,
  SecondsToMs,
  InterfacePacked
};

enum class SettingApplies : uint8_t { Boot, Live };

constexpr int32_t kNoMin = INT32_MIN;
constexpr int32_t kNoMax = INT32_MAX;

struct SettingField {
  const char* json_key;
  const char* nvs_key;  // <= 15 chars (NVS key-length limit)
  SettingType type;
  const char* category;
  SettingApplies applies;
  int32_t default_int;      // Bool(0/1), Uint, Int, EnumUint, FloatX10 (deci-units), SecondsToMs (seconds)
  const char* default_str;  // StringVal / FloatString; nullptr otherwise
  // Pick-list key into the JSON "options" object; nullptr when the field is not
  // a dropdown. InterfacePacked fields leave this null and use "interfaces".
  const char* options_key = nullptr;
  // Inclusive numeric bounds (kNoMin/kNoMax = unbounded); text-field patterns are client-only.
  int32_t min_value = kNoMin;
  int32_t max_value = kNoMax;
};

extern const SettingField kSettingFields[];
extern const size_t kSettingFieldCount;

// Serialises the settings payload: {"values":{...}} scalars, {"options":{...}}
// enum/map pick-lists, {"schema":[...]} the field list (key, category, widget
// type, options-key) the client renders from, and, when a HAL is present, the
// "interfaces" array and board-gated "dynamic" sections.
// Returns an empty String if the document overflowed (caller should answer 500).
// reboot_required is echoed into meta.reboot_required; the POST route passes the
// applier's result, GET leaves it false.
String build_settings_json(BatteryEmulatorSettingsStore& store, bool reboot_required = false);

struct SettingsApplyResult {
  bool ok;
  String error;
  bool changed;
  bool reboot_required;
};

// Applies a POSTed settings document back to NVS through kSettingFields (the same
// descriptors build_settings_json reads, so the two directions cannot drift). root
// carries "values" (scalar map) and optional "dynamic" (batteries + termination +
// load switch). A field absent from "values" — or present as JSON null — is
// preserved, never wiped. Wrong-type scalars and any invalid batteries entry
// (bad slot, disallowed type, empty primary with occupied extras) reject the
// whole POST before anything is written; termination and load-switch entries
// apply best-effort per entry (presence-gated, bounds-checked, type-skipped),
// matching the legacy handler, and are not atomic with the scalar pass.
SettingsApplyResult apply_settings_json(BatteryEmulatorSettingsStore& store, JsonObjectConst root);

// Returns nullptr when every present field is well-formed and in range, else
// the message for the 400 response. Applies nothing. Ceilings mirror the
// driver's chemistry mapping (LFP, else NCM); the driver clamps again at apply
// time, so this layer is user feedback, not the safety boundary.
const char* validate_balancing_update(battery_chemistry_enum chemistry, const JsonDocument& doc);

// Writes every present field into settings (absent fields preserved). The
// caller validates first; a rejected body must not reach here.
void apply_balancing_update(DATALAYER_BATTERY_SETTINGS_TYPE& settings, const JsonDocument& doc);

void fill_balancing_ack(const DATALAYER_BATTERY_SETTINGS_TYPE& settings, JsonObject ack);

#endif
