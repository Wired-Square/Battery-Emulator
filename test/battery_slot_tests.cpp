#include <gtest/gtest.h>

#include <algorithm>

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

TEST(BatterySlot, OccupancyIsPerSlotAndToleratesHoles) {
  BatteryType saved[kMaxBatterySlots];
  std::copy(std::begin(user_selected_battery_types), std::end(user_selected_battery_types), std::begin(saved));

  user_selected_battery_types[0] = BatteryType::NissanLeaf;
  user_selected_battery_types[1] = BatteryType::None;
  user_selected_battery_types[2] = BatteryType::BmwI3;

  EXPECT_TRUE(battery_slot_occupied(0));
  EXPECT_FALSE(battery_slot_occupied(1)) << "a {0,2} configuration must read slot 1 as a hole, not as part of a count";
  EXPECT_TRUE(battery_slot_occupied(2));
  EXPECT_EQ(battery_type_for_slot(1), BatteryType::None);
  EXPECT_EQ(battery_type_for_slot(2), BatteryType::BmwI3);
  EXPECT_FALSE(battery_slot_occupied(kMaxBatterySlots)) << "an out-of-range slot must never read as occupied";

  std::copy(std::begin(saved), std::end(saved), std::begin(user_selected_battery_types));
}
