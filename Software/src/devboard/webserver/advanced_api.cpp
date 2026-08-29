#include "advanced_api.h"

#include <cstdio>
#include <cstring>

#include "../../battery/BATTERIES.h"
#include "../../battery/Battery.h"
#include "../../datalayer/datalayer.h"

namespace {

// rows and row_keys are sibling arrays, so the keys are held back while the rows
// stream. A key that does not fit is emitted empty: that costs a description but
// keeps the two arrays aligned, where dropping it would misalign every later row.
constexpr size_t kMaxCatalogueRows = DATALAYER_BATTERY_DTC_TYPE::MAX_DTC_COUNT;
constexpr size_t kMaxCatalogueKeyLen = 16;

class StreamingAdvancedStatusWriter : public AdvancedStatusWriter {
 public:
  explicit StreamingAdvancedStatusWriter(ResponseWriter& out) : out_(out) {}

  void section(const char* title = "") override {
    close_table();
    close_section();
    out_.begin_object();
    out_.field("title", title);
    out_.begin_array("fields");
    section_open_ = true;
  }

  void kv(const char* label, const String& value, const char* unit = "",
          AdvancedSeverity severity = AdvancedSeverity::Normal) override {
    emit_kv(label, value.c_str(), unit, severity);
  }

  void kv(const char* label, const char* value, const char* unit = "",
          AdvancedSeverity severity = AdvancedSeverity::Normal) override {
    emit_kv(label, value, unit, severity);
  }

  void table(const char* label, std::initializer_list<const char*> columns,
             const char* catalogue = nullptr) override {
    close_table();
    if (!section_open_) {
      return;
    }
    out_.begin_object();
    out_.field("kind", "table");
    out_.field("label", label);
    out_.begin_array("columns");
    for (const char* c : columns) out_.element(c);
    out_.end_array();
    out_.begin_array("rows");
    catalogue_ = catalogue;
    key_count_ = 0;
    table_open_ = true;
  }

  void row_begin(const char* key = nullptr) override {
    if (!table_open_) {
      return;
    }
    out_.begin_array();
    row_open_ = true;
    if (key == nullptr || catalogue_ == nullptr || key_count_ == kMaxCatalogueRows) {
      return;
    }
    const size_t len = std::strlen(key);
    if (len < kMaxCatalogueKeyLen) {
      std::memcpy(keys_[key_count_], key, len + 1);
    } else {
      keys_[key_count_][0] = '\0';
    }
    key_count_++;
  }

  void cell(const String& text) override {
    if (row_open_) {
      out_.element(text.c_str());
    }
  }

  void row_end() override {
    if (row_open_) {
      out_.end_array();
      row_open_ = false;
    }
  }

  void finish() {
    close_table();
    close_section();
  }

 private:
  static const char* name_for_severity(AdvancedSeverity severity) {
    switch (severity) {
      case AdvancedSeverity::Good:
        return "good";
      case AdvancedSeverity::Warning:
        return "warn";
      case AdvancedSeverity::Critical:
        return "bad";
      case AdvancedSeverity::Muted:
        return "muted";
      case AdvancedSeverity::Normal:
        break;
    }
    return nullptr;
  }

  void emit_kv(const char* label, const char* value, const char* unit, AdvancedSeverity severity) {
    close_table();
    if (!section_open_) {
      return;
    }
    out_.begin_object();
    out_.field("kind", "kv");
    out_.field("label", label);
    out_.field("value", value);
    out_.field("unit", unit);
    if (const char* name = name_for_severity(severity)) {
      out_.field("sev", name);
    }
    out_.end_object();
  }

  void close_table() {
    if (!table_open_) {
      return;
    }
    row_end();
    out_.end_array();
    if (catalogue_ != nullptr) {
      out_.field("catalogue", catalogue_);
      out_.begin_array("row_keys");
      for (size_t i = 0; i < key_count_; i++) out_.element(keys_[i]);
      out_.end_array();
    }
    out_.end_object();
    table_open_ = false;
  }

  void close_section() {
    if (!section_open_) {
      return;
    }
    out_.end_array();
    out_.end_object();
    section_open_ = false;
  }

  ResponseWriter& out_;
  bool section_open_ = false;
  bool table_open_ = false;
  bool row_open_ = false;
  const char* catalogue_ = nullptr;
  size_t key_count_ = 0;
  char keys_[kMaxCatalogueRows][kMaxCatalogueKeyLen] = {};
};

void emit_battery(ResponseWriter& out, Battery* batt, uint8_t index) {
  out.begin_object();
  out.field("index", index);
  out.begin_array("sections");
  StreamingAdvancedStatusWriter writer(out);
  batt->write_advanced_status(writer);
  writer.finish();
  out.end_array();
  out.begin_array("commands");
  for (const BatteryCommand& cmd : batt->get_commands()) {
    if (cmd.available && !cmd.available()) continue;
    out.begin_object();
    out.field("id", cmd.descriptor->identifier);
    out.field("title", cmd.descriptor->title);
    if (cmd.descriptor->prompt) out.field("prompt", cmd.descriptor->prompt);
    out.field("reload_after", cmd.descriptor->reload_after);
    if (cmd.value) {
      out.begin_object("value");
      out.field("unit", cmd.value->unit);
      out.field("min", cmd.value->min);
      out.field("max", cmd.value->max);
      out.field("decimals", cmd.value->decimals);
      out.end_object();
    }
    out.end_object();
  }
  out.end_array();
  out.end_object();
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

void write_advanced(ResponseWriter& out) {
  out.begin_object();
  out.begin_array("batteries");
  for (uint8_t i = 0; i < kMaxBatterySlots; i++) {
    if (batteries[i]) emit_battery(out, batteries[i], i);
  }
  out.end_array();
  out.end_object();
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

void write_dtc_section(AdvancedStatusWriter& out, Battery& batt, DATALAYER_BATTERY_DTC_TYPE& dtc,
                       DtcCodeStyle code_style) {
  out.section("Diagnostic Trouble Codes");
  if (dtc.dtc_last_read_millis == 0) {
    out.kv("Status", "Not read yet");
  } else if (dtc.dtc_read_failed) {
    out.kv("Status", "Last read failed");
  } else if (dtc.dtc_count == 0) {
    out.kv("Status", "No DTCs present");
  } else {
    constexpr uint32_t MS_PER_S = 1000;
    out.kv("Read", String((millis() - dtc.dtc_last_read_millis) / MS_PER_S) + "s ago");
    if (dtc.dtc_reported_count > dtc.dtc_count) {
      // The battery had more to say than there are slots to hold it. Say so, rather than presenting
      // a truncated list as if it were the whole story.
      out.kv("Status",
             String(dtc.dtc_count) + " codes shown of " + String(dtc.dtc_reported_count) + " reported");
    }
    out.table("", {TL("DTC"), TL("Status"), TL("Description")}, batt.get_dtc_json_filename());
    for (uint8_t i = 0; i < dtc.dtc_count; i++) {
      out.row_begin(format_dtc_match_key(dtc.dtc_codes[i], code_style).c_str());
      out.cell(format_dtc_code(dtc.dtc_codes[i], code_style));
      out.cell(String(dtc_status_string(dtc.dtc_status[i])));
      out.cell("Unknown");
      out.row_end();
    }
  }
}
