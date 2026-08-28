#ifndef ADVANCED_API_H
#define ADVANCED_API_H

#include <WString.h>
#include <cstdint>

#include "../../battery/battery_advanced_status.h"
#include "../../datalayer/datalayer.h"

class Battery;

// GET /api/advanced payload: per present battery, its structured advanced
// status sections plus the commands that battery supports.
String build_advanced_json();

enum class DtcCodeStyle { kRawHex, kStandard, kShortFailureType };

// Build the "Diagnostic Trouble Codes" section for a battery's DTC store.
// Not-read / failed / empty render as a status line; present codes render as a
// DTC+Status+Description table. `batt` supplies the description catalogue via
// get_dtc_json_filename(); the client resolves it and fills the descriptions.
void write_dtc_section(AdvancedStatusWriter& out, Battery& batt, DATALAYER_BATTERY_DTC_TYPE& dtc,
                       DtcCodeStyle code_style);

// Dispatch a command to the battery at battery_index (0..2). `value` is null when
// the request carried no number. Returns false if id unknown, battery absent, the
// command is currently unavailable, or `value` does not match the command's spec.
bool run_advanced_command(const char* id, uint8_t battery_index, const int32_t* value = nullptr);

#endif
