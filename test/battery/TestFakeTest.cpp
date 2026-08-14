#include <gtest/gtest.h>

#include "../../Software/src/battery/BATTERIES.h"
#include "../../Software/src/battery/TEST-FAKE-BATTERY.h"
#include "../../Software/src/datalayer/datalayer.h"
#include "../../Software/src/devboard/hal/hal.h"
#include "../../Software/src/devboard/webserver/advanced_api.h"

namespace {

constexpr int32_t TEST_VOLTAGE_DV = 3725;

}  // namespace

TEST(TestFakeTests, SetVoltageWritesTheAddressedSlot) {
  init_hal();
  datalayer.battery.pack[0].status.voltage_dV = 0;
  datalayer.battery.pack[1].status.voltage_dV = 0;
  auto fake = new TestFakeBattery(battery_slot_context(1));
  batteries[0] = nullptr;
  batteries[1] = fake;

  int32_t decivolts = TEST_VOLTAGE_DV;
  EXPECT_TRUE(run_advanced_command("setFakeVoltage", 1, &decivolts));
  EXPECT_EQ(datalayer.battery.pack[1].status.voltage_dV, TEST_VOLTAGE_DV);
  EXPECT_EQ(datalayer.battery.pack[0].status.voltage_dV, 0);

  batteries[1] = nullptr;
  delete fake;
}
