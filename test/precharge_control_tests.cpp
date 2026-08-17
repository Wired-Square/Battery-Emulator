#include <gtest/gtest.h>

#include <algorithm>

#include <Arduino.h>

#include "../Software/src/battery/BATTERIES.h"
#include "../Software/src/communication/precharge_control/precharge_control.h"
#include "../Software/src/datalayer/datalayer.h"
#include "../Software/src/devboard/hal/hal.h"

class PrechargeControlTest : public ::testing::Test {
 protected:
  void SetUp() override {
    datalayer = DataLayer();
    init_hal();
    set_millis64(100000);
    std::copy(std::begin(user_selected_battery_types), std::end(user_selected_battery_types),
              std::begin(saved_types_));
    saved_battery_ = batteries[0];
    batteries[0] = nullptr;
    user_selected_battery_types[0] = BatteryType::None;
    datalayer.system.status.system_status = ACTIVE;
    datalayer.system.status.inverter_allows_contactor_closing = true;
  }

  void TearDown() override {
    batteries[0] = saved_battery_;
    std::copy(std::begin(saved_types_), std::end(saved_types_), std::begin(user_selected_battery_types));
    set_millis64(0);
  }

 private:
  BatteryType saved_types_[kMaxBatterySlots];
  Battery* saved_battery_;
};

TEST_F(PrechargeControlTest, NoSelectedTypeIsTestMode) {
  datalayer.system.info.start_precharging = false;
  handle_precharge_control(millis());
  EXPECT_TRUE(datalayer.system.info.start_precharging)
      << "with no battery type selected the precharge circuit must be exercisable for hardware test";
}

TEST_F(PrechargeControlTest, UnconstructedBatteryIsNotTestMode) {
  user_selected_battery_types[0] = BatteryType::NissanLeaf;
  datalayer.system.info.start_precharging = false;
  handle_precharge_control(millis());
  EXPECT_FALSE(datalayer.system.info.start_precharging)
      << "a selected type whose slot failed to start must not trigger an automatic hardware-test precharge";
  EXPECT_EQ(datalayer.system.status.precharge_status, AUTO_PRECHARGE_IDLE);
}

TEST_F(PrechargeControlTest, OccupiedExtraSlotIsNotTestMode) {
  user_selected_battery_types[1] = BatteryType::NissanLeaf;
  datalayer.system.info.start_precharging = false;
  handle_precharge_control(millis());
  EXPECT_FALSE(datalayer.system.info.start_precharging)
      << "an occupied extra slot means a real pack is wired, so an empty primary slot must not enable "
         "hardware-test precharge";
}

TEST_F(PrechargeControlTest, IdleToStartRequiresActiveSystem) {
  user_selected_battery_types[0] = BatteryType::NissanLeaf;
  datalayer.system.info.start_precharging = true;
  datalayer.system.status.system_status = FAULT;
  handle_precharge_control(millis());
  EXPECT_EQ(datalayer.system.status.precharge_status, AUTO_PRECHARGE_IDLE)
      << "a FAULTed system must not emit a precharge START tick before the fault handler aborts it";

  datalayer.system.status.system_status = ACTIVE;
  handle_precharge_control(millis());
  EXPECT_EQ(datalayer.system.status.precharge_status, AUTO_PRECHARGE_START);
}

TEST_F(PrechargeControlTest, IdleToStartRequiresInverterPermission) {
  user_selected_battery_types[0] = BatteryType::NissanLeaf;
  datalayer.system.info.start_precharging = true;
  datalayer.system.status.inverter_allows_contactor_closing = false;
  handle_precharge_control(millis());
  EXPECT_EQ(datalayer.system.status.precharge_status, AUTO_PRECHARGE_IDLE);
}
