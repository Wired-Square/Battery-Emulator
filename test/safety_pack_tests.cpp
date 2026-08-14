#include <gtest/gtest.h>

#include "../Software/src/battery/BATTERIES.h"
#include "../Software/src/battery/BYD-ATTO-3-BATTERY.h"
#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/datalayer/datalayer.h"
#include "../Software/src/devboard/safety/safety.h"
#include "../Software/src/devboard/utils/events.h"
#include "../Software/src/devboard/utils/types.h"
#include "../Software/src/system_settings.h"

#include "Arduino.h"

// A fault on one battery slot must stay visible while the other slots are
// healthy: the shared events (corrupted CAN, cell deviation, SOH difference)
// aggregate across slots instead of each slot setting/clearing independently.
namespace {

class SafetyPackEventTest : public testing::Test {
 protected:
  void SetUp() override {
    for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
      saved_batteries_[slot] = batteries[slot];
    }
    datalayer = DataLayer();
    init_events();
    reset_all_events();
    reset_battery_pack_safety_state();
    set_millis64(1000);

    // The slot blocks never dereference the pointers; presence is the gate.
    batteries[0] = nullptr;
    batteries[1] = present_marker();
    batteries[2] = present_marker();

    for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
      auto& b = datalayer.battery_slot(slot);
      b.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      b.info.max_cell_voltage_deviation_mV = 500;
      b.info.max_cell_voltage_mV = 4300;
      b.info.min_cell_voltage_mV = 2700;
      b.status.cell_max_voltage_mV = 3700;
      b.status.cell_min_voltage_mV = 3700;
      b.status.soh_pptt = 9900;
    }
  }

  void TearDown() override {
    for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
      batteries[slot] = saved_batteries_[slot];
    }
    datalayer = DataLayer();
    reset_all_events();
  }

  // Minimal dereferenceable battery: the per-slot checks call into the driver
  // (soc_plausible), so slot presence needs a real object, not a marker.
  struct StubBattery : Battery {
    void setup(void) override {}
    void update_values() override {}
    const char* interface_name() override { return "stub"; }
    bool soc_plausible() override { return plausible; }
    bool plausible = true;
  };

  static Battery* present_marker() {
    static StubBattery stub;
    return &stub;
  }

  // A dereferenceable primary for tests that exercise the primary-only block.
  // setup() repopulates slot 0's info, so the fixture's sane status values are
  // re-applied afterwards. Concrete type: Battery has no virtual destructor.
  BydAttoBattery* make_real_primary() {
    init_hal();
    BydAttoBattery* primary = new BydAttoBattery(battery_slot_context(0));
    primary->setup();
    auto& b = datalayer.battery.pack[0];
    b.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
    b.status.cell_max_voltage_mV = 3700;
    b.status.cell_min_voltage_mV = 3700;
    b.status.soh_pptt = 9900;
    b.status.voltage_dV = (b.info.max_design_voltage_dV + b.info.min_design_voltage_dV) / 2;
    batteries[0] = primary;
    return primary;
  }

  static bool event_active(EVENTS_ENUM_TYPE event) {
    const EVENTS_STRUCT_TYPE* entry = get_event_pointer(event);
    return entry->state == EVENT_STATE_ACTIVE || entry->state == EVENT_STATE_ACTIVE_LATCHED;
  }

  Battery* saved_batteries_[kMaxBatterySlots] = {};
};

}  // namespace

TEST_F(SafetyPackEventTest, CorruptedCanOnSlot1SurvivesHealthySlot2) {
  datalayer.battery.pack[1].status.CAN_error_counter = MAX_CAN_FAILURES + 1;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_CAN_CORRUPTED_WARNING));
}

TEST_F(SafetyPackEventTest, CorruptedCanClearsOnceEverySlotIsHealthy) {
  datalayer.battery.pack[1].status.CAN_error_counter = MAX_CAN_FAILURES + 1;
  update_machineryprotection();
  ASSERT_TRUE(event_active(EVENT_CAN_CORRUPTED_WARNING));

  datalayer.battery.pack[1].status.CAN_error_counter = 0;
  update_machineryprotection();

  EXPECT_FALSE(event_active(EVENT_CAN_CORRUPTED_WARNING));
}

TEST_F(SafetyPackEventTest, CellDeviationOnSlot1SurvivesHealthySlot2) {
  datalayer.battery.pack[1].status.cell_max_voltage_mV = 4200;
  datalayer.battery.pack[1].status.cell_min_voltage_mV = 3200;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_CELL_DEVIATION_HIGH));
}

// Deviation thresholds are per-slot properties: a spread within slot 1's
// generous limit must not trip just because slot 2's limit is tighter.
TEST_F(SafetyPackEventTest, CellDeviationJudgedAgainstOwnSlotThreshold) {
  datalayer.battery.pack[1].info.max_cell_voltage_deviation_mV = 500;
  datalayer.battery.pack[1].status.cell_max_voltage_mV = 4000;
  datalayer.battery.pack[1].status.cell_min_voltage_mV = 3600;
  datalayer.battery.pack[2].info.max_cell_voltage_deviation_mV = 100;

  update_machineryprotection();

  EXPECT_FALSE(event_active(EVENT_CELL_DEVIATION_HIGH));
}

TEST_F(SafetyPackEventTest, SohDifferenceOnSlot1SurvivesHealthySlot2) {
  datalayer.battery.pack[0].status.soh_pptt = 10000;
  datalayer.battery.pack[1].status.soh_pptt = 5000;
  datalayer.battery.pack[2].status.soh_pptt = 9800;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_SOH_DIFFERENCE));
}

TEST_F(SafetyPackEventTest, SohDifferenceClearsWhenPacksConverge) {
  datalayer.battery.pack[0].status.soh_pptt = 10000;
  datalayer.battery.pack[1].status.soh_pptt = 5000;
  update_machineryprotection();
  ASSERT_TRUE(event_active(EVENT_SOH_DIFFERENCE));

  datalayer.battery.pack[1].status.soh_pptt = 9800;
  update_machineryprotection();

  EXPECT_FALSE(event_active(EVENT_SOH_DIFFERENCE));
}

TEST_F(SafetyPackEventTest, CellDeviationClearsWhenOffenderRecovers) {
  datalayer.battery.pack[1].status.cell_max_voltage_mV = 4200;
  datalayer.battery.pack[1].status.cell_min_voltage_mV = 3200;
  update_machineryprotection();
  ASSERT_TRUE(event_active(EVENT_CELL_DEVIATION_HIGH));

  datalayer.battery.pack[1].status.cell_max_voltage_mV = 3700;
  update_machineryprotection();

  EXPECT_FALSE(event_active(EVENT_CELL_DEVIATION_HIGH));
}

// The event data byte must report the worst offender's spread, capped at the
// uint8 ceiling, not whichever offending slot the loop visited last.
TEST_F(SafetyPackEventTest, CellDeviationDataByteIsWorstOffenders) {
  datalayer.battery.pack[1].status.cell_max_voltage_mV = 4300;
  datalayer.battery.pack[1].status.cell_min_voltage_mV = 3700;  // 600 mV > 500 mV threshold
  datalayer.battery.pack[2].info.max_cell_voltage_deviation_mV = 100;
  datalayer.battery.pack[2].status.cell_max_voltage_mV = 4100;
  datalayer.battery.pack[2].status.cell_min_voltage_mV = 3700;  // 400 mV > 100 mV threshold

  update_machineryprotection();

  ASSERT_TRUE(event_active(EVENT_CELL_DEVIATION_HIGH));
  EXPECT_EQ(get_event_pointer(EVENT_CELL_DEVIATION_HIGH)->data, 600 / 20);
}

TEST_F(SafetyPackEventTest, CellDeviationDataByteSaturates) {
  datalayer.battery.pack[1].status.cell_max_voltage_mV = 65535;
  datalayer.battery.pack[1].status.cell_min_voltage_mV = 0;

  update_machineryprotection();

  ASSERT_TRUE(event_active(EVENT_CELL_DEVIATION_HIGH));
  EXPECT_EQ(get_event_pointer(EVENT_CELL_DEVIATION_HIGH)->data, 255);
}

TEST_F(SafetyPackEventTest, CorruptedCanOnPrimarySurvivesHealthySecondary) {
  BydAttoBattery* primary = make_real_primary();
  datalayer.battery.pack[0].status.CAN_error_counter = MAX_CAN_FAILURES + 1;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_CAN_CORRUPTED_WARNING));
  delete primary;
}

// On the tick a battery at 0% SOC first goes CAN-silent, the ACTIVE-gated
// empty check must still run (cutting discharge) before the missing event
// latches system FAULT — the liveness checks run after the primary block.
TEST_F(SafetyPackEventTest, BatteryEmptyStillFiresOnTheTickTheBatteryGoesMissing) {
  BydAttoBattery* primary = make_real_primary();
  batteries[1] = nullptr;
  batteries[2] = nullptr;
  datalayer.system.status.system_status = ACTIVE;
  datalayer.battery.pack[0].status.CAN_battery_still_alive = 0;
  datalayer.battery.combined.status.reported_soc = 0;
  datalayer.battery.combined.status.real_soc = 0;
  datalayer.battery.combined.status.max_discharge_power_W = 5000;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_BATTERY_EMPTY));
  EXPECT_TRUE(event_active(EVENT_CAN_BATTERY_MISSING));
  EXPECT_EQ(datalayer.battery.combined.status.max_discharge_power_W, 0u);
  delete primary;
}

// The combined view is composed before update_machineryprotection() runs, so the
// pack-level zeroing alone never reaches the inverter view — these pin the
// same-cycle combined-view actuation.
TEST_F(SafetyPackEventTest, FaultZeroesInverterViewPower) {
  set_event(EVENT_INTERNAL_OPEN_FAULT, 0);
  ASSERT_EQ(datalayer.system.status.system_status, FAULT);
  datalayer.battery.combined.status.max_charge_power_W = 5000;
  datalayer.battery.combined.status.max_discharge_power_W = 5000;

  update_machineryprotection();

  EXPECT_EQ(datalayer.battery.combined.status.max_charge_power_W, 0u);
  EXPECT_EQ(datalayer.battery.combined.status.max_discharge_power_W, 0u);
}

TEST_F(SafetyPackEventTest, SecondaryCellOvervoltageBlocksSystemCharging) {
  datalayer.battery.pack[1].status.cell_max_voltage_mV = datalayer.battery.pack[1].info.max_cell_voltage_mV;
  datalayer.battery.combined.status.max_charge_power_W = 5000;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_CELL_OVER_VOLTAGE));
  EXPECT_EQ(datalayer.battery.combined.status.max_charge_power_W, 0u);
}

TEST_F(SafetyPackEventTest, SecondaryCellUndervoltageBlocksSystemDischarge) {
  datalayer.battery.pack[1].status.cell_min_voltage_mV = datalayer.battery.pack[1].info.min_cell_voltage_mV;
  datalayer.battery.combined.status.max_discharge_power_W = 5000;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_CELL_UNDER_VOLTAGE));
  EXPECT_EQ(datalayer.battery.combined.status.max_discharge_power_W, 0u);
}

TEST_F(SafetyPackEventTest, OverheatOnSlot2SurvivesHealthyOthers) {
  datalayer.battery.pack[2].status.temperature_max_dC = BATTERY_MAXTEMPERATURE + 10;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_BATTERY_OVERHEAT));
}

TEST_F(SafetyPackEventTest, OverheatClearsWhenAllPacksCool) {
  datalayer.battery.pack[2].status.temperature_max_dC = BATTERY_MAXTEMPERATURE + 10;
  update_machineryprotection();
  ASSERT_TRUE(event_active(EVENT_BATTERY_OVERHEAT));

  datalayer.battery.pack[2].status.temperature_max_dC = 200;
  update_machineryprotection();

  EXPECT_FALSE(event_active(EVENT_BATTERY_OVERHEAT));
}

TEST_F(SafetyPackEventTest, FrozenOnSlot1Fires) {
  datalayer.battery.pack[1].status.temperature_min_dC = BATTERY_MINTEMPERATURE - 10;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_BATTERY_FROZEN));
}

TEST_F(SafetyPackEventTest, TempDeviationOnSlot1Fires) {
  datalayer.battery.pack[1].status.temperature_max_dC = 300;
  datalayer.battery.pack[1].status.temperature_min_dC = 100;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_BATTERY_TEMP_DEVIATION_HIGH));
}

TEST_F(SafetyPackEventTest, Slot2OvervoltageZeroesCombinedCharge) {
  datalayer.battery.combined.status.max_charge_power_W = 5000;
  datalayer.battery.pack[2].status.max_charge_power_W = 5000;
  datalayer.battery.pack[2].status.voltage_dV = datalayer.battery.pack[2].info.max_design_voltage_dV + 10;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_BATTERY_OVERVOLTAGE));
  EXPECT_EQ(datalayer.battery.combined.status.max_charge_power_W, 0u);
  EXPECT_EQ(datalayer.battery.pack[2].status.max_charge_power_W, 0u);
}

TEST_F(SafetyPackEventTest, Slot1UndervoltageZeroesCombinedDischarge) {
  datalayer.battery.combined.status.max_discharge_power_W = 5000;
  datalayer.battery.pack[1].status.max_discharge_power_W = 5000;
  datalayer.battery.pack[1].status.voltage_dV = datalayer.battery.pack[1].info.min_design_voltage_dV - 10;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_BATTERY_UNDERVOLTAGE));
  EXPECT_EQ(datalayer.battery.combined.status.max_discharge_power_W, 0u);
  EXPECT_EQ(datalayer.battery.pack[1].status.max_discharge_power_W, 0u);
}

TEST_F(SafetyPackEventTest, SohLowOnSlot2Fires) {
  datalayer.battery.pack[2].status.soh_pptt = 2000;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_SOH_LOW));
}

TEST_F(SafetyPackEventTest, ImplausibleSocOnSecondarySlotFires) {
  StubBattery bad;
  bad.plausible = false;
  batteries[1] = &bad;
  datalayer.battery.pack[1].status.real_soc = 4200;

  update_machineryprotection();

  EXPECT_TRUE(event_active(EVENT_SOC_PLAUSIBILITY_ERROR));
}
