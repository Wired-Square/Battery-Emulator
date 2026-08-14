#include "advanced_api.h"
#include "web_json.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "../../battery/BATTERIES.h"
#include "../../battery/Battery.h"
#include "../../datalayer/datalayer.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"

namespace {

void emit_field(JsonArray fields, const AdvancedField& f) {
  JsonObject o = fields.add<JsonObject>();
  if (f.kind == AdvancedFieldKind::Table) {
    o["kind"] = "table";
    o["label"] = std::string(f.label.c_str());
    JsonArray cols = o["columns"].to<JsonArray>();
    for (const String& c : f.columns) cols.add(std::string(c.c_str()));
    JsonArray rows = o["rows"].to<JsonArray>();
    for (const auto& r : f.rows) {
      JsonArray row = rows.add<JsonArray>();
      for (const String& cell : r) row.add(std::string(cell.c_str()));
    }
    if (!f.row_keys.empty()) {
      o["catalogue"] = std::string(f.catalogue.c_str());
      JsonArray keys = o["row_keys"].to<JsonArray>();
      for (const String& k : f.row_keys) keys.add(std::string(k.c_str()));
    }
  } else {
    o["kind"] = "kv";
    o["label"] = std::string(f.label.c_str());
    o["value"] = std::string(f.value.c_str());
    o["unit"] = std::string(f.unit.c_str());
  }
}


void emit_battery(JsonArray entries, Battery* batt, uint8_t index) {
  JsonObject entry = entries.add<JsonObject>();
  entry["index"] = index;
  JsonArray sections = entry["sections"].to<JsonArray>();
  BatteryAdvancedStatus status = batt->get_advanced_status();
  for (const AdvancedSection& s : status.sections) {
    JsonObject so = sections.add<JsonObject>();
    so["title"] = std::string(s.title.c_str());
    JsonArray fields = so["fields"].to<JsonArray>();
    for (const AdvancedField& f : s.fields) emit_field(fields, f);
  }
  JsonArray commands = entry["commands"].to<JsonArray>();
  for (const BatteryCommand& cmd : batt->get_commands()) {
    if (cmd.available && !cmd.available()) continue;
    JsonObject co = commands.add<JsonObject>();
    co["id"] = cmd.descriptor->identifier;
    co["title"] = cmd.descriptor->title;
    if (cmd.descriptor->prompt) co["prompt"] = cmd.descriptor->prompt;
    co["reload_after"] = cmd.descriptor->reload_after;
    if (cmd.value) {
      JsonObject vo = co["value"].to<JsonObject>();
      vo["unit"] = cmd.value->unit;
      vo["min"] = cmd.value->min;
      vo["max"] = cmd.value->max;
      vo["decimals"] = cmd.value->decimals;
    }
  }
}

// DTC status byte bits, precedence Active > Confirmed > Stored.
constexpr uint8_t DTC_STATUS_ACTIVE = 0x01;
constexpr uint8_t DTC_STATUS_CONFIRMED = 0x08;

static const char kSystemLetter[5] = "PCBU";
constexpr uint32_t SYSTEM_SHIFT = 22;
constexpr uint32_t SYSTEM_MASK = 0x03;
constexpr uint32_t HIGH_SHIFT = 16;
constexpr uint32_t HIGH_MASK = 0x3F;
constexpr uint32_t MID_SHIFT = 8;
constexpr uint32_t BYTE_MASK = 0xFF;

// SAE J2012 packing: 2 system bits, then 6/8-bit fields as hex. Yields "P33D7".
void format_sae_prefix(char* buf, size_t n, uint32_t code) {
  snprintf(buf, n, "%c%02lX%02lX", kSystemLetter[(code >> SYSTEM_SHIFT) & SYSTEM_MASK],
           (unsigned long)((code >> HIGH_SHIFT) & HIGH_MASK), (unsigned long)((code >> MID_SHIFT) & BYTE_MASK));
}

String format_dtc_code(uint32_t code, DtcCodeStyle code_style) {
  char buf[12];
  char prefix[6];

  if (code_style == DtcCodeStyle::kStandard) {
    format_sae_prefix(prefix, sizeof(prefix), code);
    snprintf(buf, sizeof(buf), "%s%02lX", prefix, (unsigned long)(code & BYTE_MASK));
  } else if (code_style == DtcCodeStyle::kShortFailureType) {
    // The LBC reports standard 3-byte DTCs, but Nissan service data, LeafSpy and nissan_leaf_dtc.json
    // all use the 5-character short form (P33D7, U1000) built from the first two bytes only. The third
    // byte is the failure type: it is appended for display when set ("P33D7-2F") so nothing is silently
    // dropped, but it stays out of the lookup key.
    format_sae_prefix(prefix, sizeof(prefix), code);
    uint8_t failure_type = code & BYTE_MASK;
    if (failure_type) {
      snprintf(buf, sizeof(buf), "%s-%02lX", prefix, (unsigned long)failure_type);
    } else {
      snprintf(buf, sizeof(buf), "%s", prefix);
    }
  } else {
    snprintf(buf, sizeof(buf), "%06lX", (unsigned long)code);
  }
  return String(buf);
}

// The key the description catalogue is indexed by, which is not always what is displayed:
// the short form drops the failure type, and raw-hex codes are catalogued in decimal.
String format_dtc_match_key(uint32_t code, DtcCodeStyle code_style) {
  if (code_style == DtcCodeStyle::kRawHex) {
    return String(code);
  }
  if (code_style == DtcCodeStyle::kShortFailureType) {
    char prefix[6];
    format_sae_prefix(prefix, sizeof(prefix), code);
    return String(prefix);
  }
  return format_dtc_code(code, code_style);
}

const char* dtc_status_string(uint8_t status) {
  if (status & DTC_STATUS_ACTIVE) return "Active";
  if (status & DTC_STATUS_CONFIRMED) return "Confirmed";
  return "Stored";
}
}  // namespace

String build_advanced_json() {
  JsonDocument doc;
  JsonArray entries = doc["batteries"].to<JsonArray>();
  for (uint8_t i = 0; i < kMaxBatterySlots; i++) {
    if (batteries[i]) emit_battery(entries, batteries[i], i);
  }
  return serialise_doc(doc);
}

bool run_advanced_command(const char* id, uint8_t battery_index, const int32_t* value) {
  Battery* batt = battery_at(battery_index);
  if (!batt) return false;
  for (const BatteryCommand& cmd : batt->get_commands()) {
    if (std::strcmp(cmd.descriptor->identifier, id) != 0) continue;
    if (cmd.available && !cmd.available()) return false;
    if (!cmd.value) {
      if (value) {
        return false;
      }
      cmd.action();
      return true;
    }
    if (!value || *value < cmd.value->min || *value > cmd.value->max) {
      return false;
    }
    cmd.value_action(*value);
    return true;
  }
  return false;
}

AdvancedSection dtc_advanced_section(Battery& batt, DATALAYER_BATTERY_DTC_TYPE& dtc, DtcCodeStyle code_style) {
  AdvancedSection section;
  section.title = "Diagnostic Trouble Codes";
  if (dtc.dtc_last_read_millis == 0) {
    section.fields.push_back(kv("Status", "Not read yet"));
  } else if (dtc.dtc_read_failed) {
    section.fields.push_back(kv("Status", "Last read failed"));
  } else if (dtc.dtc_count == 0) {
    section.fields.push_back(kv("Status", "No DTCs present"));
  } else {
    constexpr uint32_t MS_PER_S = 1000;
    section.fields.push_back(
        kv("Read", String((millis() - dtc.dtc_last_read_millis) / MS_PER_S) + "s ago"));
    if (dtc.dtc_reported_count > dtc.dtc_count) {
      // The battery had more to say than there are slots to hold it. Say so, rather than presenting
      // a truncated list as if it were the whole story.
      section.fields.push_back(kv(
          "Status", String(dtc.dtc_count) + " codes shown of " + String(dtc.dtc_reported_count) + " reported"));
    }
    AdvancedField table;
    table.kind = AdvancedFieldKind::Table;
    table.columns = {"DTC", "Status", "Description"};
    table.catalogue = batt.get_dtc_json_filename();
    for (uint8_t i = 0; i < dtc.dtc_count; i++) {
      table.rows.push_back({format_dtc_code(dtc.dtc_codes[i], code_style),
                            String(dtc_status_string(dtc.dtc_status[i])), "Unknown"});
      table.row_keys.push_back(format_dtc_match_key(dtc.dtc_codes[i], code_style));
    }
    section.fields.push_back(table);
  }
  return section;
}
