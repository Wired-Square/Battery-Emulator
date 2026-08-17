#include <gtest/gtest.h>

#include "../Software/src/communication/nvm/comm_nvm.h"

class BatterySlotTypeMigrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    BatteryEmulatorSettingsStore store;
    store.clearAll();
  }
};

TEST_F(BatterySlotTypeMigrationTest, LegacyDoubleBatteryBoxMigratesOnce) {
  BatteryEmulatorSettingsStore store;
  store.saveUInt("BATTTYPE", (uint32_t)BatteryType::BmwI3);
  store.saveBool("DBLBTR", true);

  migrate_battery_slot_types(store);
  EXPECT_EQ(store.getUInt("BATT2TYPE", 999), (uint32_t)BatteryType::BmwI3)
      << "a DBLBTR box inherits the shared type into slot 1";

  store.saveUInt("BATT2TYPE", (uint32_t)BatteryType::NissanLeaf);
  migrate_battery_slot_types(store);
  EXPECT_EQ(store.getUInt("BATT2TYPE", 999), (uint32_t)BatteryType::NissanLeaf)
      << "an existing BATT2TYPE must never be overwritten by a re-run";
}

TEST_F(BatterySlotTypeMigrationTest, StoredNoneSurvivesRerun) {
  BatteryEmulatorSettingsStore store;
  store.saveUInt("BATTTYPE", (uint32_t)BatteryType::BmwI3);
  store.saveBool("DBLBTR", true);
  store.saveUInt("BATT2TYPE", (uint32_t)BatteryType::None);

  migrate_battery_slot_types(store);
  EXPECT_EQ(store.getUInt("BATT2TYPE", 999), (uint32_t)BatteryType::None)
      << "None is 0 and the settings form POSTs every field, so a stored None means 'slot deliberately empty', "
         "never 'unset' - a sentinel test here would resurrect a slot the user cleared";
}

TEST_F(BatterySlotTypeMigrationTest, NoLegacyFlagsWritesNoKeys) {
  BatteryEmulatorSettingsStore store;
  store.saveUInt("BATTTYPE", (uint32_t)BatteryType::BmwI3);

  migrate_battery_slot_types(store);
  EXPECT_FALSE(store.settingExists("BATT2TYPE"));
  EXPECT_FALSE(store.settingExists("BATT3TYPE"));
}

TEST_F(BatterySlotTypeMigrationTest, TripleFlagMigratesSlotTwo) {
  BatteryEmulatorSettingsStore store;
  store.saveUInt("BATTTYPE", (uint32_t)BatteryType::NissanLeaf);
  store.saveBool("DBLBTR", true);
  store.saveBool("TRIBTR", true);

  migrate_battery_slot_types(store);
  EXPECT_EQ(store.getUInt("BATT2TYPE", 999), (uint32_t)BatteryType::NissanLeaf);
  EXPECT_EQ(store.getUInt("BATT3TYPE", 999), (uint32_t)BatteryType::NissanLeaf);
}

TEST_F(BatterySlotTypeMigrationTest, TripleWithoutDoubleMigratesToAHole) {
  BatteryEmulatorSettingsStore store;
  store.saveUInt("BATTTYPE", (uint32_t)BatteryType::NissanLeaf);
  store.saveBool("TRIBTR", true);

  migrate_battery_slot_types(store);
  EXPECT_FALSE(store.settingExists("BATT2TYPE"))
      << "a TRIBTR-only box never had a second pack; inventing one would put a phantom battery on the bus";
  EXPECT_EQ(store.getUInt("BATT3TYPE", 999), (uint32_t)BatteryType::NissanLeaf);
}
