#include <gtest/gtest.h>

#include "../Software/src/battery/BATTERIES.h"
#include "../Software/src/datalayer/datalayer.h"

TEST(BatterySlot, DatalayerAccessorAliasesNamedMembers) {
  EXPECT_EQ(&datalayer.battery_slot(0), &datalayer.battery.pack[0]);
  EXPECT_EQ(&datalayer.battery_slot(1), &datalayer.battery.pack[1]);
  EXPECT_EQ(&datalayer.battery_slot(2), &datalayer.battery.pack[2]);
}

TEST(BatterySlot, BatteryAtIsNullOutOfRange) {
  EXPECT_EQ(battery_at(kMaxBatterySlots), nullptr);
  EXPECT_EQ(battery_at(0), batteries[0]);
}

TEST(BatterySlot, StatusFlagAccessorsAliasNamedMembers) {
  auto& status = datalayer.system.status;
  EXPECT_EQ(&status.slot_allows_contactor_closing(0), &status.battery_allows_contactor_closing);
  EXPECT_EQ(&status.slot_allows_contactor_closing(1), &status.battery2_allowed_contactor_closing);
  EXPECT_EQ(&status.slot_allows_contactor_closing(2), &status.battery3_allowed_contactor_closing);
  EXPECT_EQ(&status.slot_contactors_engaged(1), &status.contactors_battery2_engaged);
  EXPECT_EQ(&status.slot_contactors_engaged(2), &status.contactors_battery3_engaged);
}

TEST(BatterySlot, ActiveBatterySlotsIsHighestEnabledSlotPlusOne) {
  bool saved_second = user_selected_second_battery;
  bool saved_triple = user_selected_triple_battery;

  user_selected_second_battery = false;
  user_selected_triple_battery = false;
  EXPECT_EQ(active_battery_slots(), 1);

  user_selected_second_battery = true;
  EXPECT_EQ(active_battery_slots(), 2);

  user_selected_triple_battery = true;
  EXPECT_EQ(active_battery_slots(), 3);

  // Triple without second still exposes slot 2: setup_battery() does not
  // gate the third battery on the second.
  user_selected_second_battery = false;
  EXPECT_EQ(active_battery_slots(), 3);

  user_selected_second_battery = saved_second;
  user_selected_triple_battery = saved_triple;
}
