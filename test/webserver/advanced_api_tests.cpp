#include <gtest/gtest.h>

#include <cstring>

#include "../../Software/src/battery/BATTERIES.h"
#include "../../Software/src/datalayer/datalayer_extended.h"
#include "../../Software/src/devboard/hal/hal.h"
#include "../../Software/src/devboard/webserver/advanced_api.h"
#include "../../Software/src/devboard/webserver/json_response_writer.h"

static String advanced_json() {
  return render_json(write_advanced);
}
#include "../advanced_status_recorder.h"
#include "../../Software/src/lib/bblanchon-ArduinoJson/ArduinoJson.h"

class AdvancedApi : public testing::Test {
 public:
  void SetUp() override {
    batteries[0] = nullptr;
    batteries[1] = nullptr;
    batteries[2] = nullptr;
  }

  // A stub is a stack local; the global must not outlive it.
  void TearDown() override {
    batteries[0] = nullptr;
    batteries[1] = nullptr;
    batteries[2] = nullptr;
  }
};

namespace {

class StubBattery : public Battery {
 public:
  void setup() override {}
  void update_values() override {}
  const char* interface_name() override { return "stub"; }

  const std::vector<BatteryCommand>& get_commands() override { return commands_; }

  std::vector<BatteryCommand> commands_;
  int stop_calls = 0;
  bool gate_open = true;
  int value_calls = 0;
  int32_t last_value = 0;

  const char* get_dtc_json_filename() override { return dtc_json_filename; }
  const char* dtc_json_filename = "";

  void write_advanced_status(AdvancedStatusWriter& out) override {
    if (advanced_) advanced_(out);
  }
  std::function<void(AdvancedStatusWriter&)> advanced_;
};

// esp32hal is process-global, so restore it or later tests inherit this HAL.
// The allocation is abandoned rather than deleted: Esp32Hal has no virtual
// destructor, so destroying a derived HAL through this pointer is undefined.
struct HalScope {
  Esp32Hal* saved = esp32hal;
  HalScope() { init_hal(); }
  ~HalScope() { esp32hal = saved; }
};

bool command_offered(const String& json, const char* id) {
  JsonDocument doc;
  EXPECT_FALSE(deserializeJson(doc, json.c_str()));
  for (JsonObject command : doc["batteries"][0]["commands"].as<JsonArray>()) {
    if (std::strcmp(command["id"] | "", id) == 0) {
      return true;
    }
  }
  return false;
}

bool run_with_value(const char* id, uint8_t battery_index, int32_t value) {
  return run_advanced_command(id, battery_index, &value);
}

}  // namespace

TEST_F(AdvancedApi, EmptyWhenNoBattery) {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, advanced_json().c_str()));
  EXPECT_EQ(doc["batteries"].size(), 0u);
}

namespace {
// Captures what a driver writes, so a section can be asserted on without going
// through the JSON encoding.
RecordingWriter record_dtc(Battery& batt, DATALAYER_BATTERY_DTC_TYPE& dtc, DtcCodeStyle style) {
  RecordingWriter w;
  write_dtc_section(w, batt, dtc, style);
  return w;
}

// The table is the last field; the fields before it are status lines.
const RecordingWriter::Field& dtc_table(const RecordingWriter::Section& s) {
  EXPECT_TRUE(s.fields.back().is_table);
  return s.fields.back();
}
}  // namespace

TEST_F(AdvancedApi, DtcNotReadShowsStatus) {
  StubBattery stub;
  DATALAYER_BATTERY_DTC_TYPE dtc{};
  dtc.dtc_last_read_millis = 0;
  RecordingWriter w = record_dtc(stub, dtc, DtcCodeStyle::kRawHex);
  const RecordingWriter::Section& s = w.sections.at(0);
  EXPECT_EQ(s.title, "Diagnostic Trouble Codes");
  ASSERT_EQ(s.fields.size(), 1u);
  EXPECT_FALSE(s.fields[0].is_table);
}

TEST_F(AdvancedApi, DtcCodesProduceTable) {
  StubBattery stub;
  DATALAYER_BATTERY_DTC_TYPE dtc{};
  dtc.dtc_last_read_millis = 1000;
  dtc.dtc_count = 2;
  dtc.dtc_codes[0] = 0x9B4E12;
  dtc.dtc_status[0] = 0x01;
  dtc.dtc_codes[1] = 0x123456;
  dtc.dtc_status[1] = 0x08;
  RecordingWriter w = record_dtc(stub, dtc, DtcCodeStyle::kRawHex);
  const RecordingWriter::Section& s = w.sections.at(0);
  const RecordingWriter::Field& table = dtc_table(s);
  ASSERT_EQ(table.columns.size(), 3u);
  EXPECT_EQ(table.columns[2], "Description");
  ASSERT_EQ(table.rows.size(), 2u);
  EXPECT_EQ(table.rows[0][0], "9B4E12");
  EXPECT_EQ(table.rows[0][1], "Active");
  EXPECT_EQ(table.rows[1][1], "Confirmed");
}

// Raw-hex codes are displayed as hex but catalogued in decimal, so the key is not the cell.
TEST_F(AdvancedApi, DtcRawHexMatchKeyIsDecimal) {
  StubBattery stub;
  DATALAYER_BATTERY_DTC_TYPE dtc{};
  dtc.dtc_last_read_millis = 1000;
  dtc.dtc_count = 1;
  dtc.dtc_codes[0] = 0x9B4E12;
  RecordingWriter w = record_dtc(stub, dtc, DtcCodeStyle::kRawHex);
  const RecordingWriter::Section& s = w.sections.at(0);
  const RecordingWriter::Field& table = dtc_table(s);
  ASSERT_EQ(table.row_keys.size(), 1u);
  EXPECT_EQ(table.rows[0][0], "9B4E12");
  EXPECT_EQ(table.row_keys[0], "10178066");
}

// The failure type is shown but is not part of the catalogue key.
TEST_F(AdvancedApi, DtcShortFormMatchKeyDropsFailureType) {
  StubBattery stub;
  DATALAYER_BATTERY_DTC_TYPE dtc{};
  dtc.dtc_last_read_millis = 1000;
  dtc.dtc_count = 2;
  dtc.dtc_codes[0] = 0x33D72F;
  dtc.dtc_codes[1] = 0x33D700;
  RecordingWriter w = record_dtc(stub, dtc, DtcCodeStyle::kShortFailureType);
  const RecordingWriter::Section& s = w.sections.at(0);
  const RecordingWriter::Field& table = dtc_table(s);
  EXPECT_EQ(table.rows[0][0], "P33D7-2F");
  EXPECT_EQ(table.row_keys[0], "P33D7");
  EXPECT_EQ(table.rows[1][0], "P33D7");
  EXPECT_EQ(table.row_keys[1], "P33D7");
}

TEST_F(AdvancedApi, DtcStandardMatchKeyIsTheDisplayedCode) {
  StubBattery stub;
  DATALAYER_BATTERY_DTC_TYPE dtc{};
  dtc.dtc_last_read_millis = 1000;
  dtc.dtc_count = 1;
  dtc.dtc_codes[0] = 0x0C9500;
  RecordingWriter w = record_dtc(stub, dtc, DtcCodeStyle::kStandard);
  const RecordingWriter::Section& s = w.sections.at(0);
  const RecordingWriter::Field& table = dtc_table(s);
  EXPECT_EQ(table.rows[0][0], "P0C9500");
  EXPECT_EQ(table.row_keys[0], "P0C9500");
}

TEST_F(AdvancedApi, DtcCatalogueComesFromTheBatteryAndReachesTheJson) {
  StubBattery stub;
  stub.dtc_json_filename = "mg_dtc.json";
  DATALAYER_BATTERY_DTC_TYPE dtc{};
  dtc.dtc_last_read_millis = 1000;
  dtc.dtc_count = 1;
  dtc.dtc_codes[0] = 0x0C9500;
  RecordingWriter w = record_dtc(stub, dtc, DtcCodeStyle::kStandard);
  EXPECT_EQ(dtc_table(w.sections.at(0)).catalogue, "mg_dtc.json");

  stub.advanced_ = [&](AdvancedStatusWriter& out) {
    write_dtc_section(out, stub, dtc, DtcCodeStyle::kStandard);
  };
  batteries[0] = &stub;
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, advanced_json().c_str()));
  JsonObject field = doc["batteries"][0]["sections"][0]["fields"][1];
  EXPECT_STREQ(field["catalogue"] | "", "mg_dtc.json");
  EXPECT_STREQ(field["row_keys"][0] | "", "P0C9500");
}

// Two truncation notices were rendered for one condition; one is the whole story.
TEST_F(AdvancedApi, DtcTruncationIsReportedExactlyOnce) {
  StubBattery stub;
  DATALAYER_BATTERY_DTC_TYPE dtc{};
  dtc.dtc_last_read_millis = 1000;
  dtc.dtc_count = 2;
  dtc.dtc_reported_count = 149;
  RecordingWriter w = record_dtc(stub, dtc, DtcCodeStyle::kRawHex);
  const RecordingWriter::Section& s = w.sections.at(0);
  int mentions = 0;
  for (const auto& f : s.fields) {
    if (f.value.find("149 reported") != std::string::npos) mentions++;
  }
  EXPECT_EQ(mentions, 1);
}

TEST_F(AdvancedApi, DeclaredCommandsAreOfferedAndDispatched) {
  StubBattery stub;
  stub.commands_ = {command(CMD_CHADEMO_STOP, [&stub] { stub.stop_calls++; })};
  batteries[0] = &stub;

  const String json = advanced_json();
  EXPECT_TRUE(command_offered(json, "chademoStop"));
  EXPECT_FALSE(command_offered(json, "chademoRestart"));

  EXPECT_TRUE(run_advanced_command("chademoStop", 0));
  EXPECT_EQ(stub.stop_calls, 1);
}

TEST_F(AdvancedApi, UnavailableCommandIsHiddenAndRefused) {
  StubBattery stub;
  stub.gate_open = false;
  stub.commands_ = {
      command(CMD_CHADEMO_STOP, [&stub] { stub.stop_calls++; }, [&stub] { return stub.gate_open; })};
  batteries[0] = &stub;

  EXPECT_FALSE(command_offered(advanced_json(), "chademoStop"));
  EXPECT_FALSE(run_advanced_command("chademoStop", 0));
  EXPECT_EQ(stub.stop_calls, 0);

  stub.gate_open = true;
  EXPECT_TRUE(command_offered(advanced_json(), "chademoStop"));
  EXPECT_TRUE(run_advanced_command("chademoStop", 0));
  EXPECT_EQ(stub.stop_calls, 1);
}

TEST_F(AdvancedApi, ChademoStopStopsAndRestartRestarts) {
  HalScope hal;
  ChademoBattery chademo;
  batteries[0] = &chademo;
  datalayer_extended.chademo.UserRequestStop = false;
  datalayer_extended.chademo.UserRequestRestart = false;

  const String json = advanced_json();
  EXPECT_TRUE(command_offered(json, "chademoStop"));
  EXPECT_TRUE(command_offered(json, "chademoRestart"));

  EXPECT_TRUE(run_advanced_command("chademoStop", 0));
  EXPECT_TRUE(datalayer_extended.chademo.UserRequestStop);
  EXPECT_FALSE(datalayer_extended.chademo.UserRequestRestart);

  datalayer_extended.chademo.UserRequestStop = false;
  EXPECT_TRUE(run_advanced_command("chademoRestart", 0));
  EXPECT_TRUE(datalayer_extended.chademo.UserRequestRestart);
  EXPECT_FALSE(datalayer_extended.chademo.UserRequestStop);

  datalayer_extended.chademo.UserRequestRestart = false;
}

TEST_F(AdvancedApi, AvailabilityIsReevaluatedOnDispatchAlone) {
  StubBattery stub;
  stub.commands_ = {
      command(CMD_CHADEMO_STOP, [&stub] { stub.stop_calls++; }, [&stub] { return stub.gate_open; })};
  batteries[0] = &stub;

  EXPECT_TRUE(command_offered(advanced_json(), "chademoStop"));

  stub.gate_open = false;
  EXPECT_FALSE(run_advanced_command("chademoStop", 0));
  EXPECT_EQ(stub.stop_calls, 0);

  stub.gate_open = true;
  EXPECT_TRUE(run_advanced_command("chademoStop", 0));
  EXPECT_EQ(stub.stop_calls, 1);
}

TEST_F(AdvancedApi, ValueCommandPublishesItsSpecAndReceivesTheValue) {
  StubBattery stub;
  stub.commands_ = {value_command(CMD_SET_FAKE_VOLTAGE, [&stub](int32_t v) {
    stub.value_calls++;
    stub.last_value = v;
  })};
  batteries[0] = &stub;

  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, advanced_json().c_str()));
  JsonObject spec = doc["batteries"][0]["commands"][0]["value"];
  EXPECT_STREQ(spec["unit"] | "", "V");
  EXPECT_EQ(spec["min"] | -1, FAKE_VOLTAGE_MIN_DV);
  EXPECT_EQ(spec["max"] | -1, FAKE_VOLTAGE_MAX_DV);
  EXPECT_EQ(spec["decimals"] | -1, DECI_UNIT_DECIMALS);

  EXPECT_TRUE(run_with_value("setFakeVoltage", 0, 4005));
  EXPECT_EQ(stub.value_calls, 1);
  EXPECT_EQ(stub.last_value, 4005);
}

TEST_F(AdvancedApi, ValueCommandAcceptsItsBoundsAndRefusesBeyondThem) {
  StubBattery stub;
  stub.commands_ = {value_command(CMD_SET_FAKE_VOLTAGE, [&stub](int32_t v) {
    stub.value_calls++;
    stub.last_value = v;
  })};
  batteries[0] = &stub;

  EXPECT_TRUE(run_with_value("setFakeVoltage", 0, FAKE_VOLTAGE_MIN_DV));
  EXPECT_TRUE(run_with_value("setFakeVoltage", 0, FAKE_VOLTAGE_MAX_DV));
  EXPECT_EQ(stub.value_calls, 2);

  EXPECT_FALSE(run_with_value("setFakeVoltage", 0, FAKE_VOLTAGE_MIN_DV - 1));
  EXPECT_FALSE(run_with_value("setFakeVoltage", 0, FAKE_VOLTAGE_MAX_DV + 1));
  EXPECT_EQ(stub.value_calls, 2);
}

TEST_F(AdvancedApi, CommandArityIsEnforcedBothWays) {
  StubBattery stub;
  stub.commands_ = {
      command(CMD_CHADEMO_STOP, [&stub] { stub.stop_calls++; }),
      value_command(CMD_SET_FAKE_VOLTAGE, [&stub](int32_t) { stub.value_calls++; }),
  };
  batteries[0] = &stub;

  EXPECT_FALSE(run_with_value("chademoStop", 0, 1));
  EXPECT_EQ(stub.stop_calls, 0);

  EXPECT_FALSE(run_advanced_command("setFakeVoltage", 0));
  EXPECT_EQ(stub.value_calls, 0);
}

TEST_F(AdvancedApi, DeclaredCommandsDispatchToTheAddressedSlot) {
  StubBattery slot0;
  StubBattery slot1;
  slot0.commands_ = {command(CMD_CHADEMO_STOP, [&slot0] { slot0.stop_calls++; })};
  slot1.commands_ = {command(CMD_CHADEMO_STOP, [&slot1] { slot1.stop_calls++; })};
  batteries[0] = &slot0;
  batteries[1] = &slot1;

  EXPECT_TRUE(run_advanced_command("chademoStop", 1));
  EXPECT_EQ(slot1.stop_calls, 1);
  EXPECT_EQ(slot0.stop_calls, 0);

  EXPECT_FALSE(run_advanced_command("chademoStop", 2));
  EXPECT_EQ(slot1.stop_calls, 1);
  EXPECT_EQ(slot0.stop_calls, 0);
}

TEST_F(AdvancedApi, OmitsSevWhenSeverityIsNormal) {
  StubBattery stub;
  stub.advanced_ = [](AdvancedStatusWriter& out) {
    out.section();
    out.kv("Isolation", "OK");
  };
  batteries[0] = &stub;

  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, advanced_json().c_str()));
  JsonObject field = doc["batteries"][0]["sections"][0]["fields"][0];
  EXPECT_STREQ(field["value"] | "", "OK");
  EXPECT_FALSE(field["sev"].is<const char*>());
}

TEST_F(AdvancedApi, EmitsSevWhenSeverityIsWarning) {
  StubBattery stub;
  stub.advanced_ = [](AdvancedStatusWriter& out) {
    out.section();
    out.kv("Pyro fuse", "Successfully Blown", "", AdvancedSeverity::Warning);
  };
  batteries[0] = &stub;

  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, advanced_json().c_str()));
  JsonObject field = doc["batteries"][0]["sections"][0]["fields"][0];
  EXPECT_STREQ(field["sev"] | "", "warn");
}
