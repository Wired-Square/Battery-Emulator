#include <gtest/gtest.h>
#include <stdio.h>

#include <string>

#include "../Software/src/battery/BATTERIES.h"
#include "../Software/src/battery/battery_slots.h"
#include "../Software/src/charger/CHARGERS.h"
#include "../Software/src/datalayer/datalayer.h"
#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/safety/safety.h"
#include "../Software/src/devboard/utils/events.h"
#include "../Software/src/inverter/INVERTERS.h"

// wifi.cpp is not linked host-side; the settings applier sets these live WiFi
// credential globals, so the host build needs them.
std::string ssid;
std::string password;

void RegisterCanLogTests(void);
void RegisterStillAliveTests(void);

class DataLayerResetListener : public ::testing::EmptyTestEventListener {
 public:
  void OnTestStart(const ::testing::TestInfo& /*test_info*/) override {
    datalayer = DataLayer();
    reset_all_events();

    // Every instance holds pointers into the datalayer we just replaced, so
    // destroy them all.
    for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
      delete batteries[slot];
      batteries[slot] = nullptr;
    }
    delete charger;
    charger = nullptr;
    /* The inverter is the same kind of instance and was the one omission here.
       It also decides behaviour by TYPE - needs_can_startup_grace() is true for
       the SMA family and false for the rest - so a test inheriting the previous
       test's inverter silently runs against the wrong protocol. */
    delete inverter;
    inverter = nullptr;

    // Selection globals must be owned by each test's own fixture.
    user_selected_battery_types[1] = BatteryType::None;
    user_selected_battery_types[2] = BatteryType::None;

    init_hal();
  }
};

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  // Add a listener to reset the datalayer and events before each test
  ::testing::UnitTest::GetInstance()->listeners().Append(new DataLayerResetListener);

  RegisterCanLogTests();
  RegisterStillAliveTests();

  return RUN_ALL_TESTS();
}

void store_settings_equipment_stop(void) {}
