#ifndef SETTINGS_API_H
#define SETTINGS_API_H

#include <WString.h>
#include <cstddef>
#include <cstdint>

#include "../../battery/balancing_bounds.h"
#include "../utils/types.h"
#include "document_reader.h"
#include "response_writer.h"
#include "setting_options.h"
#include "settings_field.h"

class BatteryEmulatorSettingsStore;

extern const SettingField kSettingFields[];
extern const size_t kSettingFieldCount;

using DeviceSettingSource = DeviceSettingList (*)();

struct SettingRef {
  const SettingField* field;
  const DeviceSetting* device;
  DeviceSettingSource source;
  SettingDomain domain;
};

size_t setting_count();
SettingRef setting_at(size_t index);

// Must run before board_init(): a board's own bring-up reads what this applies.
void apply_stored_board_settings(BatteryEmulatorSettingsStore& store);

// Writes the settings payload: {"values":{...}} scalars, {"options":{...}}
// enum/map pick-lists, {"schema":[...]} the field list (key, category, widget
// type, options-key) the client renders from, and, when a HAL is present, the
// "interfaces" array and board-gated "dynamic" sections.
// reboot_required is echoed into meta.reboot_required; the POST route passes the
// applier's result, GET leaves it false.
void write_settings(ResponseWriter& out, BatteryEmulatorSettingsStore& store, bool reboot_required = false);

struct SettingsApplyResult {
  bool ok;
  String error;
  bool changed;
  bool reboot_required;
  const char* error_key = nullptr;
  String error_arg;
};

// Applies a POSTed settings document back to NVS through kSettingFields (the same
// descriptors write_settings reads, so the two directions cannot drift). The
// reader's scalar map is "values"; "dynamic" carries batteries, balancing,
// termination and load switch. A field absent from the map — or present as null — is
// preserved, never wiped. Wrong-type scalars and any invalid batteries entry
// (bad slot, disallowed type, empty primary with occupied extras) reject the
// whole POST before anything is written; termination and load-switch entries
// apply best-effort per entry (presence-gated, bounds-checked, type-skipped),
// matching the legacy handler, and are not atomic with the scalar pass.
SettingsApplyResult apply_settings(BatteryEmulatorSettingsStore& store, const DocumentReader& body);

// Returns nullptr when the value is absent, or well-formed and in range for its
// key, else the message for the 400 response. Ceilings mirror the driver's
// chemistry mapping (LFP, else NCM); the driver clamps again at apply time, so
// this layer is user feedback, not the safety boundary.
const char* validate_balancing_field(battery_chemistry_enum chemistry, const char* key,
                                     const DocumentValue& value);

#endif
