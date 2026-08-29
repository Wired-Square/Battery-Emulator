#include <gtest/gtest.h>

#include "../../Software/src/battery/BATTERIES.h"
#include "../../Software/src/battery/BYD-ATTO-3-BATTERY.h"
#include "../../Software/src/datalayer/datalayer.h"
#include "../../Software/src/datalayer/datalayer_extended.h"
#include "../../Software/src/devboard/webserver/cellmonitor_api.h"
#include "../../Software/src/lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "../advanced_status_recorder.h"

#include "Arduino.h"

// File-scope in the .cpp, no header. Lets tests build frames the driver will accept.
extern uint8_t computeBydChecksum(const uint8_t* u8);

namespace {

CAN_frame byd_frame(uint32_t id, std::initializer_list<uint8_t> bytes) {
  CAN_frame frame = {};
  frame.DLC = 8;
  frame.ID = id;
  uint8_t i = 0;
  for (uint8_t b : bytes) {
    if (i >= 8) {
      break;
    }
    frame.data.u8[i++] = b;
  }
  return frame;
}

// Builds a frame from the first 7 bytes, computing byte 7 the way the BMS does.
CAN_frame byd_checksummed_frame(uint32_t id, std::initializer_list<uint8_t> first7bytes) {
  CAN_frame frame = byd_frame(id, first7bytes);
  frame.data.u8[7] = computeBydChecksum(frame.data.u8);
  return frame;
}

// Same with a bad checksum. Payload must differ from the previous decode, or acceptance and
// rejection look identical.
CAN_frame byd_corrupt_frame(uint32_t id, std::initializer_list<uint8_t> first7bytes) {
  CAN_frame frame = byd_frame(id, first7bytes);
  frame.data.u8[7] = (uint8_t)(computeBydChecksum(frame.data.u8) ^ 0xFF);
  return frame;
}

// Clears the shared global datalayer between tests, so none sees values left by another.
void reset_byd_state() {
  datalayer.battery.pack[0].status = DATALAYER_BATTERY_STATUS_TYPE{};
  datalayer.battery.settings.max_user_set_charge_dA = 300;
  datalayer_extended.bydAtto3.chargePower = 0;
  datalayer_extended.bydAtto3.dischargePower = 0;
  datalayer_extended.bydAtto3.SOC_polled = 0;
  datalayer_extended.bydAtto3.SOC_highprec = 0;
  datalayer_extended.bydAtto3.BMS_min_temp_module_number = 0;
  datalayer_extended.bydAtto3.BMS_max_temp_module_number = 0;
}

// Coldest sensor 9 at 8C, hottest sensor 1 at 24C, SOC 99.2%, average 16C.
CAN_frame temperature_frame() {
  return byd_checksummed_frame(0x447, {0x09, 0x30, 0x01, 0x40, 0xE0, 0x03, 0x38});
}

// Discharge 285.5kW (b0:b1 = 0x0B27), charge 131.1kW (b2:b3 = 0x051F).
CAN_frame power_limit_frame() {
  return byd_checksummed_frame(0x345, {0x27, 0x0B, 0x1F, 0x05, 0x00, 0x00, 0x00});
}

}  // namespace

TEST(BydAtto3Tests, ShouldDecode0x345PowerLimitsAndRejectBadChecksum) {
  reset_byd_state();
  auto battery = new BydAttoBattery(battery_slot_context(0));
  battery->setup();

  battery->handle_incoming_can_frame(power_limit_frame());
  battery->update_values();

  EXPECT_EQ(datalayer_extended.bydAtto3.dischargePower, 2855);
  EXPECT_EQ(datalayer_extended.bydAtto3.chargePower, 1311);
  EXPECT_EQ(datalayer.battery.pack[0].status.CAN_error_counter, 0);

  // 100 / 200 with a bad checksum: a gate failing open would show those instead of 2855 / 1311.
  battery->handle_incoming_can_frame(byd_corrupt_frame(0x345, {0x64, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x00}));
  battery->update_values();

  EXPECT_EQ(datalayer_extended.bydAtto3.dischargePower, 2855);
  EXPECT_EQ(datalayer_extended.bydAtto3.chargePower, 1311);
  EXPECT_EQ(datalayer.battery.pack[0].status.CAN_error_counter, 1);

  // Same payload, valid checksum: accepted, so the checksum caused the rejection above.
  battery->handle_incoming_can_frame(byd_checksummed_frame(0x345, {0x64, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x00}));
  battery->update_values();

  EXPECT_EQ(datalayer_extended.bydAtto3.dischargePower, 100);
  EXPECT_EQ(datalayer_extended.bydAtto3.chargePower, 200);
}

TEST(BydAtto3Tests, ShouldDecode0x447TemperaturesSensorNumbersAndHighPrecisionSoc) {
  reset_byd_state();
  auto battery = new BydAttoBattery(battery_slot_context(0));
  battery->setup();

  battery->handle_incoming_can_frame(temperature_frame());
  battery->update_values();

  EXPECT_EQ(datalayer_extended.bydAtto3.BMS_min_temp_module_number, 9);
  EXPECT_EQ(datalayer_extended.bydAtto3.BMS_max_temp_module_number, 1);
  EXPECT_EQ(datalayer.battery.pack[0].status.temperature_min_dC, 80);   // 8C
  EXPECT_EQ(datalayer.battery.pack[0].status.temperature_max_dC, 240);  // 24C
  EXPECT_EQ(datalayer.battery.pack[0].status.real_soc, 9920);           // 99.2%, scaled by 100
  EXPECT_EQ(datalayer_extended.bydAtto3.SOC_highprec, 992);
  EXPECT_EQ(datalayer.battery.pack[0].status.CAN_error_counter, 0);

  // Sensor 1 at 40C / sensor 2 at 56C / SOC 25.6%, all materially different, with a bad checksum.
  battery->handle_incoming_can_frame(byd_corrupt_frame(0x447, {0x01, 0x50, 0x02, 0x68, 0x00, 0x01, 0x50}));
  battery->update_values();

  EXPECT_EQ(datalayer_extended.bydAtto3.BMS_min_temp_module_number, 9);
  EXPECT_EQ(datalayer_extended.bydAtto3.BMS_max_temp_module_number, 1);
  EXPECT_EQ(datalayer.battery.pack[0].status.temperature_min_dC, 80);
  EXPECT_EQ(datalayer.battery.pack[0].status.temperature_max_dC, 240);
  EXPECT_EQ(datalayer.battery.pack[0].status.real_soc, 9920);
  EXPECT_EQ(datalayer.battery.pack[0].status.CAN_error_counter, 1);
}

TEST(BydAtto3Tests, ShouldDecode0x444WholePercentSocAndRejectBadChecksum) {
  reset_byd_state();
  auto battery = new BydAttoBattery(battery_slot_context(0));
  battery->setup();

  // b0/b1 = whole-volt voltage (420V), b2:b3 = current (offset 5000, here 0A), b4 = SOH (93%),
  // b5 = whole-percent SOC (31%)
  battery->handle_incoming_can_frame(byd_checksummed_frame(0x444, {0xA4, 0x01, 0x88, 0x13, 0x5D, 0x1F, 0x00}));
  battery->update_values();

  EXPECT_EQ(datalayer_extended.bydAtto3.SOC_polled, 31);
  EXPECT_EQ(datalayer.battery.pack[0].status.soh_pptt, 9300);
  EXPECT_EQ(datalayer.battery.pack[0].status.CAN_error_counter, 0);

  // SOH 50% / SOC 80%, both materially different, with a bad checksum.
  battery->handle_incoming_can_frame(byd_corrupt_frame(0x444, {0xA4, 0x01, 0x88, 0x13, 0x32, 0x50, 0x00}));
  battery->update_values();

  EXPECT_EQ(datalayer_extended.bydAtto3.SOC_polled, 31);
  EXPECT_EQ(datalayer.battery.pack[0].status.soh_pptt, 9300);
  EXPECT_EQ(datalayer.battery.pack[0].status.CAN_error_counter, 1);
}

namespace {
CAN_frame uds_reply(std::initializer_list<uint8_t> bytes) {
  return byd_frame(0x7EF, bytes);
}
}  // namespace

class BydAtto3BalanceTimeTest : public testing::Test {
 protected:
  void SetUp() override {
    reset_byd_state();
    datalayer.battery.pack[0].info.number_of_cells = 0;
    battery = new BydAttoBattery(battery_slot_context(0));
    battery->setup();
  }

  void TearDown() override { delete battery; }

  BydAttoBattery* battery = nullptr;
};

TEST_F(BydAtto3BalanceTimeTest, RejectsScanUntilCellCountIsKnown) {
  EXPECT_FALSE(battery->request_cell_balance_times());
  EXPECT_EQ(battery->cell_balance_times().state(), CellSeriesState::Unread);
}

TEST_F(BydAtto3BalanceTimeTest, ReadsMatchingDidsAndKeepsZeroDistinctFromUnread) {
  datalayer.battery.pack[0].info.number_of_cells = 2;
  ASSERT_TRUE(battery->request_cell_balance_times());
  EXPECT_EQ(battery->cell_balance_times().state(), CellSeriesState::Unread);

  battery->transmit_can(200);
  EXPECT_EQ(battery->cell_balance_times().state(), CellSeriesState::Reading);

  battery->handle_incoming_can_frame(uds_reply({0x05, 0x62, 0x00, 0x41, 0xFF, 0x7F, 0xAA, 0xAA}));
  EXPECT_EQ(battery->cell_balance_times().received(), 0) << "a reply for another DID must not be banked";

  battery->handle_incoming_can_frame(uds_reply({0x05, 0x62, 0x00, 0x40, 0xA4, 0x01, 0xAA, 0xAA}));
  EXPECT_EQ(battery->cell_balance_times().value(0), 420);
  EXPECT_TRUE(battery->cell_balance_times().cell_valid(0));

  battery->transmit_can(400);
  battery->handle_incoming_can_frame(uds_reply({0x05, 0x62, 0x00, 0x41, 0x00, 0x00, 0xAA, 0xAA}));

  const CellSeriesBuffer& result = battery->cell_balance_times();
  EXPECT_EQ(result.state(), CellSeriesState::Complete);
  EXPECT_EQ(result.received(), 2);
  EXPECT_EQ(result.value(1), 0);
  EXPECT_TRUE(result.cell_valid(1)) << "zero hours is a reading, not an unread cell";
  EXPECT_EQ(result.revision(), 1u);
}

TEST_F(BydAtto3BalanceTimeTest, NegativeReplyProducesUsefulPartialResult) {
  datalayer.battery.pack[0].info.number_of_cells = 2;
  ASSERT_TRUE(battery->request_cell_balance_times());
  battery->transmit_can(200);

  battery->handle_incoming_can_frame(uds_reply({0x03, 0x7F, 0x22, 0x31, 0x00, 0x00, 0x00, 0x00}));
  battery->transmit_can(400);
  battery->handle_incoming_can_frame(uds_reply({0x05, 0x62, 0x00, 0x41, 0xDD, 0x01, 0xAA, 0xAA}));

  const CellSeriesBuffer& result = battery->cell_balance_times();
  EXPECT_EQ(result.state(), CellSeriesState::Partial);
  EXPECT_EQ(result.received(), 1);
  EXPECT_FALSE(result.cell_valid(0));
  EXPECT_TRUE(result.cell_valid(1));
  EXPECT_EQ(result.value(1), 477);
}

TEST_F(BydAtto3BalanceTimeTest, ResponsePendingDoesNotAdvanceTheCellCursor) {
  datalayer.battery.pack[0].info.number_of_cells = 1;
  ASSERT_TRUE(battery->request_cell_balance_times());
  battery->transmit_can(200);

  battery->handle_incoming_can_frame(uds_reply({0x03, 0x7F, 0x22, 0x78, 0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ(battery->cell_balance_times().state(), CellSeriesState::Reading);
  EXPECT_EQ(battery->cell_balance_times().received(), 0);

  battery->handle_incoming_can_frame(uds_reply({0x05, 0x62, 0x00, 0x40, 0xA4, 0x01, 0xAA, 0xAA}));
  EXPECT_EQ(battery->cell_balance_times().state(), CellSeriesState::Complete);
  EXPECT_EQ(battery->cell_balance_times().value(0), 420);
}

TEST_F(BydAtto3BalanceTimeTest, ResponsePendingCannotHoldTheScanOpen) {
  datalayer.battery.pack[0].info.number_of_cells = 1;
  ASSERT_TRUE(battery->request_cell_balance_times());
  battery->transmit_can(200);

  battery->handle_incoming_can_frame(uds_reply({0x03, 0x7F, 0x22, 0x78, 0x00, 0x00, 0x00, 0x00}));
  // Must be past CELL_BALANCE_TIME_SCAN_TIMEOUT_MS, otherwise the scan is still legitimately open.
  battery->transmit_can(90200);

  EXPECT_EQ(battery->cell_balance_times().state(), CellSeriesState::Failed);
  EXPECT_EQ(battery->cell_balance_times().revision(), 1u);
}

TEST_F(BydAtto3BalanceTimeTest, RefusesASecondScanWhileOneIsStillOutstanding) {
  datalayer.battery.pack[0].info.number_of_cells = 1;
  ASSERT_TRUE(battery->request_cell_balance_times());
  EXPECT_FALSE(battery->request_cell_balance_times());

  battery->transmit_can(200);
  battery->handle_incoming_can_frame(uds_reply({0x05, 0x62, 0x00, 0x40, 0xA4, 0x01, 0xAA, 0xAA}));
  ASSERT_EQ(battery->cell_balance_times().state(), CellSeriesState::Complete);
  EXPECT_TRUE(battery->request_cell_balance_times());
}

TEST_F(BydAtto3BalanceTimeTest, PublishesTheReadingAsACellSeries) {
  datalayer.battery.pack[0].info.number_of_cells = 2;
  ASSERT_TRUE(battery->request_cell_balance_times());
  battery->transmit_can(200);
  battery->handle_incoming_can_frame(uds_reply({0x05, 0x62, 0x00, 0x40, 0xA4, 0x01, 0xAA, 0xAA}));
  battery->transmit_can(400);
  battery->handle_incoming_can_frame(uds_reply({0x03, 0x7F, 0x22, 0x31, 0x00, 0x00, 0x00, 0x00}));

  batteries[0] = battery;
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, build_cellmonitor_json().c_str()));
  batteries[0] = nullptr;

  JsonObjectConst series = doc["batteries"][0]["series"][0];
  EXPECT_STREQ(series["id"], "balance_hours");
  EXPECT_STREQ(series["kind"], "counter");
  EXPECT_EQ(series["revision"].as<uint32_t>(), 1u);
  ASSERT_EQ(series["values"].size(), 2u);
  EXPECT_EQ(series["values"][0].as<int>(), 420);
  EXPECT_TRUE(series["values"][1].isNull()) << "the refused cell must read as unknown, not as zero hours";
}

namespace {

const RecordingWriter::Section* autocal_section(const RecordingWriter& out) {
  for (const auto& section : out.sections) {
    if (section.title == "Auto-calibration status") {
      return &section;
    }
  }
  return nullptr;
}

std::string autocal_field(const RecordingWriter& out, const std::string& label) {
  const RecordingWriter::Section* section = autocal_section(out);
  if (section == nullptr) {
    return "";
  }
  for (const auto& field : section->fields) {
    if (field.label == label) {
      return field.value;
    }
  }
  return "";
}

class BydAtto3AutoCalibrationTest : public testing::Test {
 protected:
  void SetUp() override {
    reset_byd_state();
    datalayer_extended.bydAtto3 = DATALAYER_INFO_BYDATTO3{};
    battery = new BydAttoBattery(battery_slot_context(0));
  }

  void TearDown() override { delete battery; }

  RecordingWriter render() {
    RecordingWriter out;
    battery->write_advanced_status(out);
    return out;
  }

  BydAttoBattery* battery = nullptr;
};

}  // namespace

TEST_F(BydAtto3AutoCalibrationTest, DisplayedThresholdsComeFromTheEnforcedConstants) {
  const std::string dwell_minutes = std::to_string(BydAttoBattery::kAutoCalDwellRequiredMs / 60000);
  EXPECT_NE(autocal_field(render(), "Dwell time").find("/ " + dwell_minutes + "m"), std::string::npos)
      << autocal_field(render(), "Dwell time");

  datalayer_extended.bydAtto3.autocal_crit_taper = true;
  datalayer_extended.bydAtto3.autocal_crit_low_current = false;
  const std::string grace_seconds = std::to_string(BydAttoBattery::kAutoCalCurrentGraceMs / 1000);
  EXPECT_NE(autocal_field(render(), "Current in range").find("/ " + grace_seconds + "s"), std::string::npos)
      << autocal_field(render(), "Current in range");

  datalayer_extended.bydAtto3.autocal_crit_low_current = true;
  const std::string in_range = autocal_field(render(), "Current in range");
  EXPECT_NE(in_range.find("≤3.0A"), std::string::npos) << in_range;
  EXPECT_NE(in_range.find("≤0.5A"), std::string::npos) << in_range;
  EXPECT_EQ(BydAttoBattery::kAutoCalMaxChargeCurrentDa, 30);
  EXPECT_EQ(BydAttoBattery::kAutoCalMinDischargeCurrentDa, -5);
}

TEST_F(BydAtto3AutoCalibrationTest, EachRowPairsItsTextWithOneSeverity) {
  datalayer_extended.bydAtto3.autocal_crit_contactors = true;
  datalayer_extended.bydAtto3.autocal_crit_taper = false;
  RecordingWriter out = render();
  EXPECT_EQ(autocal_field(out, "Contactors"), "OK");
  EXPECT_EQ(autocal_field(out, "Full / In taper?"), "No");
  EXPECT_EQ(autocal_field(out, "Current in range"), "Waiting for taper");

  datalayer_extended.bydAtto3.autocal_crit_contactors = false;
  out = render();
  EXPECT_EQ(autocal_field(out, "Contactors"), "Open");
}
