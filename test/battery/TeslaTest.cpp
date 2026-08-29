#include <gtest/gtest.h>

#include <cstring>

#include "../../Software/src/battery/BATTERIES.h"
#include "../../Software/src/battery/TESLA-BATTERY.h"
#include "../../Software/src/datalayer/datalayer.h"
#include "../../Software/src/datalayer/datalayer_extended.h"
#include "../../Software/src/devboard/hal/hal.h"
#include "../../Software/src/devboard/webserver/advanced_api.h"
#include "../../Software/src/devboard/webserver/json_response_writer.h"

static String advanced_json() {
  return render_json(write_advanced);
}
#include "../../Software/src/lib/bblanchon-ArduinoJson/ArduinoJson.h"

namespace {

// Reads the served payload rather than the command list, so a command withdrawn
// by its availability predicate is indistinguishable from one never declared.
bool offers(uint8_t entry, const char* id) {
  JsonDocument doc;
  EXPECT_FALSE(deserializeJson(doc, advanced_json().c_str()));
  for (JsonObject command : doc["batteries"][entry]["commands"].as<JsonArray>()) {
    if (std::strcmp(command["id"] | "", id) == 0) {
      return true;
    }
  }
  return false;
}

// The other three are asserted throughout because a non-empty declared list
// suppresses the legacy registry outright, so dropping one fails silently.
void expect_unconditional_commands(uint8_t entry) {
  EXPECT_TRUE(offers(entry, "clearIsolation"));
  EXPECT_TRUE(offers(entry, "resetBMS"));
  EXPECT_TRUE(offers(entry, "resetSOC"));
}

}  // namespace

// The manufacture date is only published once the serial number frame is parsed,
// so the page has to render before any of it has arrived.
TEST(TeslaTests, AdvancedStatusRendersBeforeAnyCanTraffic) {
  init_hal();
  datalayer_extended.tesla.battery_manufactureDate[0] = '\0';
  auto tesla = new TeslaBattery(battery_slot_context(0));
  tesla->setup();
  batteries[0] = tesla;

  JsonDocument doc;
  EXPECT_FALSE(deserializeJson(doc, advanced_json().c_str()));
  EXPECT_STREQ(doc["batteries"][0]["sections"][0]["fields"][3]["label"] | "", "Battery Manufacture Date");
  EXPECT_STREQ(doc["batteries"][0]["sections"][0]["fields"][3]["value"] | "?", "");

  batteries[0] = nullptr;
  delete tesla;
}

TEST(TeslaTests, BalancingCommandsTrackTheRequestFlag) {
  init_hal();
  datalayer.battery.pack[0].settings.user_requests_balancing = false;
  auto tesla = new TeslaBattery(battery_slot_context(0));
  tesla->setup();
  batteries[0] = tesla;

  EXPECT_TRUE(offers(0, "startBalancing"));
  EXPECT_FALSE(offers(0, "endBalancing"));
  expect_unconditional_commands(0);

  EXPECT_FALSE(run_advanced_command("endBalancing", 0));
  EXPECT_TRUE(run_advanced_command("startBalancing", 0));
  EXPECT_TRUE(datalayer.battery.pack[0].settings.user_requests_balancing);

  EXPECT_FALSE(offers(0, "startBalancing"));
  EXPECT_TRUE(offers(0, "endBalancing"));
  expect_unconditional_commands(0);

  EXPECT_FALSE(run_advanced_command("startBalancing", 0));
  EXPECT_TRUE(run_advanced_command("endBalancing", 0));
  EXPECT_FALSE(datalayer.battery.pack[0].settings.user_requests_balancing);

  batteries[0] = nullptr;
  delete tesla;
}

TEST(TeslaTests, BalancingIsOfferedOnSecondarySlots) {
  init_hal();
  datalayer.battery.pack[1].settings.user_requests_balancing = false;
  auto tesla = new TeslaBattery(battery_slot_context(1));
  tesla->setup();
  // Only present batteries are emitted, so entry 0 is the slot-1 battery
  // while slot 0 is empty. Assert both rather than inherit them from test order.
  batteries[0] = nullptr;
  batteries[1] = tesla;

  JsonDocument doc;
  EXPECT_FALSE(deserializeJson(doc, advanced_json().c_str()));
  EXPECT_EQ(doc["batteries"][0]["index"] | -1, 1);

  EXPECT_TRUE(offers(0, "startBalancing"));
  EXPECT_FALSE(offers(0, "endBalancing"));
  expect_unconditional_commands(0);

  EXPECT_TRUE(run_advanced_command("startBalancing", 1));
  EXPECT_TRUE(datalayer.battery.pack[1].settings.user_requests_balancing);
  EXPECT_FALSE(datalayer.battery.pack[0].settings.user_requests_balancing);

  EXPECT_TRUE(offers(0, "endBalancing"));
  EXPECT_TRUE(run_advanced_command("endBalancing", 1));
  EXPECT_FALSE(datalayer.battery.pack[1].settings.user_requests_balancing);

  batteries[1] = nullptr;
  delete tesla;
}

// The clamp is the safety boundary: web-validated settings can outlive a
// chemistry autodetect flip, so the driver must bound them at apply time.
TEST(TeslaTests, BalancingClampsSettingsToActiveChemistryLimits) {
  init_hal();
  user_selected_battery_types[0] = BatteryType::TeslaModel3Y;
  auto tesla = new TeslaBattery(battery_slot_context(0));
  tesla->setup();
  batteries[0] = tesla;
  datalayer.battery.pack[0].info.chemistry = battery_chemistry_enum::LFP;
  auto& settings = datalayer.battery.pack[0].settings;
  settings.user_requests_balancing = true;
  settings.balancing_max_cell_voltage_mV = 4250;
  settings.balancing_max_deviation_cell_voltage_mV = 500;
  settings.balancing_max_pack_voltage_dV = 4090;
  settings.balancing_float_power_W = 5000;

  tesla->update_values();

  EXPECT_EQ(datalayer.battery.pack[0].info.max_cell_voltage_mV, 3650);
  EXPECT_EQ(datalayer.battery.pack[0].info.max_cell_voltage_deviation_mV, 400);
  EXPECT_EQ(datalayer.battery.pack[0].info.max_design_voltage_dV, 3940);
  EXPECT_EQ(datalayer.battery.pack[0].status.max_charge_power_W, 2000u);

  settings.user_requests_balancing = false;
  batteries[0] = nullptr;
  delete tesla;
}

TEST(TeslaTests, BalancingSettingsWithinChemistryLimitsPassThrough) {
  init_hal();
  user_selected_battery_types[0] = BatteryType::TeslaModel3Y;
  auto tesla = new TeslaBattery(battery_slot_context(0));
  tesla->setup();
  batteries[0] = tesla;
  datalayer.battery.pack[0].info.chemistry = battery_chemistry_enum::NMC;
  auto& settings = datalayer.battery.pack[0].settings;
  settings.user_requests_balancing = true;
  settings.balancing_max_cell_voltage_mV = 4250;
  settings.balancing_max_deviation_cell_voltage_mV = 500;
  settings.balancing_max_pack_voltage_dV = 4090;
  settings.balancing_float_power_W = 1500;

  tesla->update_values();

  EXPECT_EQ(datalayer.battery.pack[0].info.max_cell_voltage_mV, 4250);
  EXPECT_EQ(datalayer.battery.pack[0].info.max_cell_voltage_deviation_mV, 500);
  EXPECT_EQ(datalayer.battery.pack[0].info.max_design_voltage_dV, 4090);
  EXPECT_EQ(datalayer.battery.pack[0].status.max_charge_power_W, 1500u);

  settings.user_requests_balancing = false;
  batteries[0] = nullptr;
  delete tesla;
}

// Two instances transmit 0x213 from their own member frames; each frame's
// alive counter must advance by exactly 1 per call, not share a sequence.
TEST(TeslaTests, Tesla213AliveCounterIsPerFrame) {
  CAN_frame a = {.FD = false, .ext_ID = false, .DLC = 2, .ID = 0x213, .data = {0x00, 0x15}};
  CAN_frame b = {.FD = false, .ext_ID = false, .DLC = 2, .ID = 0x213, .data = {0x00, 0x15}};

  generateTESLA_213(a);
  generateTESLA_213(b);
  generateTESLA_213(a);

  EXPECT_EQ(a.data.u8[0] >> 4, 2);
  EXPECT_EQ(b.data.u8[0] >> 4, 1);
  EXPECT_EQ(a.data.u8[1], ((a.data.u8[0] + 0x15) & 0xFF));
}
