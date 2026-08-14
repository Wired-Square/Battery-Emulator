#include <gtest/gtest.h>

#include "../Software/src/battery/BATTERIES.h"
#include "../Software/src/battery/BatterySlotContext.h"
#include "../Software/src/battery/CHARGEBYTE-CCS.h"
#include "../Software/src/datalayer/datalayer.h"
#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_lilygo.h"
#include "../Software/src/devboard/utils/events.h"

// setup() must write only the bound slot's datalayer.
class BatteryCtorTest : public testing::Test {
 protected:
  void SetUp() override {
    init_hal();
    saved_battery_ = datalayer.battery.pack[0];
    saved_battery2_ = datalayer.battery.pack[1];
    saved_status_ = datalayer.system.status;
    saved_type_ = user_selected_battery_type;
  }

  void TearDown() override {
    datalayer.battery.pack[0] = saved_battery_;
    datalayer.battery.pack[1] = saved_battery2_;
    datalayer.system.status = saved_status_;
    user_selected_battery_type = saved_type_;
  }

 private:
  DATALAYER_BATTERY_TYPE saved_battery_;
  DATALAYER_BATTERY_TYPE saved_battery2_;
  DATALAYER_SYSTEM_STATUS_TYPE saved_status_;
  BatteryType saved_type_;
};

TEST_F(BatteryCtorTest, EcmpBindsSlotDatalayer) {
  auto* primary = new EcmpBattery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.number_of_cells, 0);

  auto* secondary = new EcmpBattery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.number_of_cells, 0);
}

TEST_F(BatteryCtorTest, MgGen1BindsSlotDatalayer) {
  auto* primary = new MgGen1Battery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.number_of_cells, 0);

  auto* secondary = new MgGen1Battery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.number_of_cells, 0);
}

TEST_F(BatteryCtorTest, SantaFePhevBindsSlotDatalayer) {
  auto* primary = new SantaFePhevBattery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.number_of_cells, 0);
  EXPECT_TRUE(datalayer.system.status.battery_allows_contactor_closing);

  auto* secondary = new SantaFePhevBattery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.number_of_cells, 0);
  // Non-primary slot: allows_contactor_closing is nullptr, so setup() must
  // not touch the primary's contactor-closing flag.
  EXPECT_TRUE(datalayer.system.status.battery_allows_contactor_closing);
}

TEST_F(BatteryCtorTest, TestFakeBindsSlotDatalayer) {
  auto* primary = new TestFakeBattery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.number_of_cells, 0);

  auto* secondary = new TestFakeBattery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.number_of_cells, 0);
}

TEST_F(BatteryCtorTest, TeslaBindsSlotDatalayer) {
  // Pin the setup() branch (3Y/NCMA pack-voltage table) so the expected
  // value isn't order-dependent on other tests mutating these globals.
  user_selected_battery_type = BatteryType::TeslaModel3Y;
  datalayer.battery.pack[0].info.chemistry = battery_chemistry_enum::NCA;
  datalayer.battery.pack[1].info.chemistry = battery_chemistry_enum::NCA;
  constexpr uint16_t kExpectedMaxDesignVoltageDv3YNcma = 4030;

  auto* primary = new TeslaBattery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_EQ(datalayer.battery.pack[0].info.max_design_voltage_dV, kExpectedMaxDesignVoltageDv3YNcma);

  auto* secondary = new TeslaBattery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_EQ(datalayer.battery.pack[1].info.max_design_voltage_dV, kExpectedMaxDesignVoltageDv3YNcma);
}

// The 0x1DB decode must land voltage_dV on the bound slot only.
TEST_F(BatteryCtorTest, NissanLeafSlotBindingRoutesDecodedVoltage) {
  constexpr int kExpectedDv = 440;
  constexpr int kSentinelDv = 999;  // Implausible value planted between slots to detect cross-slot writes.

  int divided = kExpectedDv / 5;
  CAN_frame frame = {.ID = 0x1DB, .data = {.u8 = {0, 0, (uint8_t)(divided >> 2), (uint8_t)((divided & 0xC0) << 6)}}};

  auto* primary = new NissanLeafBattery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  CAN_frame frame0 = frame;
  frame0.data.u8[7] = primary->calculate_crc(frame0);
  primary->handle_incoming_can_frame(frame0);
  primary->update_values();
  EXPECT_EQ(datalayer.battery.pack[0].status.voltage_dV, kExpectedDv);

  // Poison the primary's slot so a stray slot-1 write into it would be caught.
  datalayer.battery.pack[0].status.voltage_dV = kSentinelDv;

  auto* secondary = new NissanLeafBattery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  CAN_frame frame1 = frame;
  frame1.data.u8[7] = secondary->calculate_crc(frame1);
  secondary->handle_incoming_can_frame(frame1);
  secondary->update_values();
  EXPECT_EQ(datalayer.battery.pack[1].status.voltage_dV, kExpectedDv);
  EXPECT_EQ(datalayer.battery.pack[0].status.voltage_dV, kSentinelDv);
}

TEST_F(BatteryCtorTest, CmfaEvBindsSlotDatalayer) {
  auto* primary = new CmfaEvBattery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.number_of_cells, 0);

  auto* secondary = new CmfaEvBattery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.number_of_cells, 0);
}

TEST_F(BatteryCtorTest, CmpSmartCarBindsSlotDatalayer) {
  auto* primary = new CmpSmartCarBattery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.number_of_cells, 0);

  auto* secondary = new CmpSmartCarBattery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.number_of_cells, 0);
}

TEST_F(BatteryCtorTest, RenaultZoeGen1BindsSlotDatalayer) {
  auto* primary = new RenaultZoeGen1Battery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.number_of_cells, 0);

  auto* secondary = new RenaultZoeGen1Battery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.number_of_cells, 0);
}

TEST_F(BatteryCtorTest, RenaultZoeGen2BindsSlotDatalayer) {
  auto* primary = new RenaultZoeGen2Battery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.number_of_cells, 0);

  auto* secondary = new RenaultZoeGen2Battery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.number_of_cells, 0);
}

TEST_F(BatteryCtorTest, PylonBindsSlotDatalayer) {
  auto* primary = new PylonBattery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.number_of_cells, 0);

  auto* secondary = new PylonBattery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.number_of_cells, 0);
}

// Listen path lives in transmit_can (needs private awake state); not asserted here.
TEST_F(BatteryCtorTest, BmwI3BindsSlotDatalayer) {
  auto* primary = new BmwI3Battery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.number_of_cells, 0);
  EXPECT_TRUE(datalayer.system.status.battery_allows_contactor_closing);

  auto* secondary = new BmwI3Battery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.number_of_cells, 0);
  // Non-primary slot: allows_contactor_closing is nullptr, so setup() must
  // not touch the primary's contactor-closing flag.
  EXPECT_TRUE(datalayer.system.status.battery_allows_contactor_closing);
}

TEST_F(BatteryCtorTest, RelionBindsSlotDatalayer) {
  auto* primary = new RelionBattery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.number_of_cells, 0);

  auto* secondary = new RelionBattery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.number_of_cells, 0);
}

// Extended state is driver-owned; only the slot datalayer is observable here.
TEST_F(BatteryCtorTest, BoltAmperaBindsSlotDatalayer) {
  auto* primary = new BoltAmperaBattery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.number_of_cells, 0);
  EXPECT_TRUE(datalayer.system.status.battery_allows_contactor_closing);

  auto* secondary = new BoltAmperaBattery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.number_of_cells, 0);
  // Non-primary slot: allows_contactor_closing is nullptr, so setup() must
  // not touch the primary's contactor-closing flag.
  EXPECT_TRUE(datalayer.system.status.battery_allows_contactor_closing);
}

// The decide/listen swap lives in transmit_can; not asserted here.
TEST_F(BatteryCtorTest, KiaHyundai64BindsSlotDatalayer) {
  constexpr uint16_t kExpectedMaxDesignVoltage98sDv = 4110;

  auto* primary = new KiaHyundai64Battery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_EQ(datalayer.battery.pack[0].info.max_design_voltage_dV, kExpectedMaxDesignVoltage98sDv);
  EXPECT_TRUE(datalayer.system.status.battery_allows_contactor_closing);

  auto* secondary = new KiaHyundai64Battery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_EQ(datalayer.battery.pack[1].info.max_design_voltage_dV, kExpectedMaxDesignVoltage98sDv);
  EXPECT_TRUE(datalayer.system.status.battery_allows_contactor_closing);
}

TEST_F(BatteryCtorTest, BydAttoBindsSlotDatalayer) {
  // setup() does not set allows_contactor_closing; the contactor state machine owns it.
  auto* primary = new BydAttoBattery(battery_slot_context(0));
  ASSERT_NE(primary, nullptr);
  primary->setup();
  EXPECT_NE(datalayer.battery.pack[0].info.max_design_voltage_dV, 0);

  auto* secondary = new BydAttoBattery(battery_slot_context(1));
  ASSERT_NE(secondary, nullptr);
  secondary->setup();
  EXPECT_NE(datalayer.battery.pack[1].info.max_design_voltage_dV, 0);
}

// Gate on the second/triple flags, not active_battery_slots() (triple-on
// reports 3 even when double is off).
TEST(SetupBatterySlotGateTest, TripleOnDoubleOffSkipsSecondSlot) {
  init_hal();

  Battery* saved_battery = batteries[0];
  Battery* saved_battery2 = batteries[1];
  Battery* saved_battery3 = batteries[2];
  bool saved_second = user_selected_second_battery;
  bool saved_triple = user_selected_triple_battery;
  BatteryType saved_type = user_selected_battery_type;
  DATALAYER_BATTERY_TYPE saved_datalayer_battery = datalayer.battery.pack[0];
  DATALAYER_BATTERY_TYPE saved_datalayer_battery2 = datalayer.battery.pack[1];
  DATALAYER_BATTERY_TYPE saved_datalayer_battery3 = datalayer.battery.pack[2];
  DATALAYER_SYSTEM_STATUS_TYPE saved_status = datalayer.system.status;

  batteries[0] = nullptr;
  batteries[1] = nullptr;
  batteries[2] = nullptr;
  user_selected_battery_type = BatteryType::NissanLeaf;
  user_selected_second_battery = false;
  user_selected_triple_battery = true;

  setup_battery();

  EXPECT_NE(batteries[0], nullptr);
  EXPECT_EQ(batteries[1], nullptr);
  EXPECT_NE(batteries[2], nullptr);

  delete batteries[0];
  delete batteries[2];
  batteries[0] = saved_battery;
  batteries[1] = saved_battery2;
  batteries[2] = saved_battery3;
  user_selected_second_battery = saved_second;
  user_selected_triple_battery = saved_triple;
  user_selected_battery_type = saved_type;
  datalayer.battery.pack[0] = saved_datalayer_battery;
  datalayer.battery.pack[1] = saved_datalayer_battery2;
  datalayer.battery.pack[2] = saved_datalayer_battery3;
  datalayer.system.status = saved_status;
}

// A slot configured onto an already-used CAN interface must not start.
TEST(SetupBatterySlotGateTest, DuplicateInterfaceSkipsSecondSlot) {
  init_hal();
  init_events();

  Battery* saved_battery = batteries[0];
  Battery* saved_battery2 = batteries[1];
  Battery* saved_battery3 = batteries[2];
  bool saved_second = user_selected_second_battery;
  bool saved_triple = user_selected_triple_battery;
  BatteryType saved_type = user_selected_battery_type;
  DATALAYER_BATTERY_TYPE saved_datalayer_battery = datalayer.battery.pack[0];
  DATALAYER_BATTERY_TYPE saved_datalayer_battery2 = datalayer.battery.pack[1];
  DATALAYER_SYSTEM_STATUS_TYPE saved_status = datalayer.system.status;
  auto* saved_iface = can_config.battery;
  auto* saved_iface_double = can_config.battery_double;

  static InterfaceDescriptor shared_iface{InterfaceType::CanNative, nullptr, comm_interface::Highest, nullptr};
  batteries[0] = nullptr;
  batteries[1] = nullptr;
  batteries[2] = nullptr;
  user_selected_battery_type = BatteryType::NissanLeaf;
  user_selected_second_battery = true;
  user_selected_triple_battery = false;
  can_config.battery = &shared_iface;
  can_config.battery_double = &shared_iface;

  setup_battery();

  EXPECT_NE(batteries[0], nullptr);
  EXPECT_EQ(batteries[1], nullptr);
  const EVENTS_STRUCT_TYPE* conflict = get_event_pointer(EVENT_BATTERY_INTERFACE_CONFLICT);
  EXPECT_EQ(conflict->state, EVENT_STATE_ACTIVE);
  EXPECT_EQ(conflict->data, 2);

  delete batteries[0];
  batteries[0] = saved_battery;
  batteries[1] = saved_battery2;
  batteries[2] = saved_battery3;
  user_selected_second_battery = saved_second;
  user_selected_triple_battery = saved_triple;
  user_selected_battery_type = saved_type;
  can_config.battery = saved_iface;
  can_config.battery_double = saved_iface_double;
  datalayer.battery.pack[0] = saved_datalayer_battery;
  datalayer.battery.pack[1] = saved_datalayer_battery2;
  datalayer.system.status = saved_status;
}

// A second battery on a driver without multi-pack support must not start,
// and must say why.
TEST(SetupBatterySlotGateTest, UnsupportedSecondSlotRaisesEvent) {
  init_hal();
  init_events();

  Battery* saved_battery = batteries[0];
  Battery* saved_battery2 = batteries[1];
  Battery* saved_battery3 = batteries[2];
  bool saved_second = user_selected_second_battery;
  bool saved_triple = user_selected_triple_battery;
  BatteryType saved_type = user_selected_battery_type;
  DATALAYER_BATTERY_TYPE saved_datalayer_battery = datalayer.battery.pack[0];
  DATALAYER_BATTERY_TYPE saved_datalayer_battery2 = datalayer.battery.pack[1];
  DATALAYER_SYSTEM_STATUS_TYPE saved_status = datalayer.system.status;
  auto* saved_iface = can_config.battery;
  auto* saved_iface_double = can_config.battery_double;

  static InterfaceDescriptor first_iface{InterfaceType::CanMcp2517fd, nullptr, comm_interface::Highest, nullptr};
  static InterfaceDescriptor second_iface{InterfaceType::CanMcp2517fd, nullptr, comm_interface::Highest, nullptr};
  batteries[0] = nullptr;
  batteries[1] = nullptr;
  batteries[2] = nullptr;
  user_selected_battery_type = BatteryType::BmwIX;
  user_selected_second_battery = true;
  user_selected_triple_battery = false;
  can_config.battery = &first_iface;
  can_config.battery_double = &second_iface;

  setup_battery();

  EXPECT_NE(batteries[0], nullptr);
  EXPECT_EQ(batteries[1], nullptr);
  const EVENTS_STRUCT_TYPE* unsupported = get_event_pointer(EVENT_BATTERY_SLOT_UNSUPPORTED);
  EXPECT_EQ(unsupported->state, EVENT_STATE_ACTIVE);
  EXPECT_EQ(unsupported->data, 2);

  delete batteries[0];
  batteries[0] = saved_battery;
  batteries[1] = saved_battery2;
  batteries[2] = saved_battery3;
  user_selected_second_battery = saved_second;
  user_selected_triple_battery = saved_triple;
  user_selected_battery_type = saved_type;
  can_config.battery = saved_iface;
  can_config.battery_double = saved_iface_double;
  datalayer.battery.pack[0] = saved_datalayer_battery;
  datalayer.battery.pack[1] = saved_datalayer_battery2;
  datalayer.system.status = saved_status;
}

TEST(SetupBatterySlotGateTest, UnsupportedInterfaceRaisesEvent) {
  init_hal();
  init_events();

  Battery* saved_battery = batteries[0];
  BatteryType saved_type = user_selected_battery_type;
  bool saved_second = user_selected_second_battery;
  bool saved_triple = user_selected_triple_battery;
  DATALAYER_BATTERY_TYPE saved_datalayer_battery = datalayer.battery.pack[0];
  DATALAYER_SYSTEM_STATUS_TYPE saved_status = datalayer.system.status;
  auto* saved_iface = can_config.battery;

  static InterfaceDescriptor classic_iface{InterfaceType::CanNative, nullptr, comm_interface::Highest, nullptr};
  batteries[0] = nullptr;
  user_selected_battery_type = BatteryType::Meb;
  user_selected_second_battery = false;
  user_selected_triple_battery = false;
  can_config.battery = &classic_iface;

  setup_battery();

  EXPECT_EQ(batteries[0], nullptr);
  const EVENTS_STRUCT_TYPE* unsupported = get_event_pointer(EVENT_BATTERY_INTERFACE_UNSUPPORTED);
  EXPECT_EQ(unsupported->state, EVENT_STATE_ACTIVE);
  EXPECT_EQ(unsupported->data, 1);

  batteries[0] = saved_battery;
  user_selected_battery_type = saved_type;
  user_selected_second_battery = saved_second;
  user_selected_triple_battery = saved_triple;
  can_config.battery = saved_iface;
  datalayer.battery.pack[0] = saved_datalayer_battery;
  datalayer.system.status = saved_status;
}

TEST(SetupBatterySlotGateTest, SatisfiedInterfaceConstructs) {
  init_hal();
  init_events();

  Battery* saved_battery = batteries[0];
  BatteryType saved_type = user_selected_battery_type;
  bool saved_second = user_selected_second_battery;
  bool saved_triple = user_selected_triple_battery;
  DATALAYER_BATTERY_TYPE saved_datalayer_battery = datalayer.battery.pack[0];
  DATALAYER_SYSTEM_STATUS_TYPE saved_status = datalayer.system.status;
  auto* saved_iface = can_config.battery;

  static InterfaceDescriptor fd_iface{InterfaceType::CanMcp2517fd, nullptr, comm_interface::Highest, nullptr};
  batteries[0] = nullptr;
  user_selected_battery_type = BatteryType::Meb;
  user_selected_second_battery = false;
  user_selected_triple_battery = false;
  can_config.battery = &fd_iface;

  setup_battery();

  EXPECT_NE(batteries[0], nullptr);
  EXPECT_EQ(get_event_pointer(EVENT_BATTERY_INTERFACE_UNSUPPORTED)->state, EVENT_STATE_INACTIVE);

  delete batteries[0];
  batteries[0] = saved_battery;
  user_selected_battery_type = saved_type;
  user_selected_second_battery = saved_second;
  user_selected_triple_battery = saved_triple;
  can_config.battery = saved_iface;
  datalayer.battery.pack[0] = saved_datalayer_battery;
  datalayer.system.status = saved_status;
}

static void expect_protocol_after_setup(BatteryType type, const char* expected) {
  init_hal();

  Battery* saved_battery = batteries[0];
  Battery* saved_battery2 = batteries[1];
  Battery* saved_battery3 = batteries[2];
  bool saved_second = user_selected_second_battery;
  bool saved_triple = user_selected_triple_battery;
  BatteryType saved_type = user_selected_battery_type;
  DATALAYER_BATTERY_TYPE saved_datalayer_battery = datalayer.battery.pack[0];
  DATALAYER_SYSTEM_STATUS_TYPE saved_status = datalayer.system.status;
  DATALAYER_SYSTEM_INFO_TYPE saved_info = datalayer.system.info;

  batteries[0] = nullptr;
  batteries[1] = nullptr;
  batteries[2] = nullptr;
  user_selected_battery_type = type;
  user_selected_second_battery = false;
  user_selected_triple_battery = false;

  setup_battery();

  ASSERT_NE(batteries[0], nullptr);
  EXPECT_STREQ(datalayer.system.info.battery_protocol, expected)
      << "the name the dashboard shows must be the one the settings list offers";

  delete batteries[0];
  batteries[0] = saved_battery;
  batteries[1] = saved_battery2;
  batteries[2] = saved_battery3;
  user_selected_second_battery = saved_second;
  user_selected_triple_battery = saved_triple;
  user_selected_battery_type = saved_type;
  datalayer.battery.pack[0] = saved_datalayer_battery;
  datalayer.system.status = saved_status;
  datalayer.system.info = saved_info;
}

TEST(SetupBatteryNameTest, CentralisedCopyMatchesDriverName) {
  expect_protocol_after_setup(BatteryType::NissanLeaf, name_for_battery_type(BatteryType::NissanLeaf));
}

#ifndef SMALL_FLASH_DEVICE
TEST(SetupBatteryNameTest, ChargebyteReportsItsRegisteredName) {
  expect_protocol_after_setup(BatteryType::ChargebyteCCSBattery, ChargebyteCCSBattery::Name);
}
#endif

TEST(BatteryRegistryCharacterization, NamePresenceMatchesValidIds) {
  for (int i = 0; i < static_cast<int>(BatteryType::Highest); i++) {
    const bool is_gap = (i == 1) || (i == 46);  // unassigned values in BatteryType
    const char* n = name_for_battery_type(static_cast<BatteryType>(i));
#ifdef SMALL_FLASH_DEVICE
    if (i == static_cast<int>(BatteryType::Mg5)) {
      EXPECT_EQ(n, nullptr) << "Mg5 excluded on small-flash";
      continue;
    }
#endif
    if (is_gap) {
      EXPECT_EQ(n, nullptr) << "gap id " << i << " must be null";
    } else {
      EXPECT_NE(n, nullptr) << "valid id " << i << " must have a name";
    }
  }
  EXPECT_EQ(name_for_battery_type(BatteryType::Highest), nullptr);
}

TEST(BatteryRegistryCharacterization, NameExactAnchors) {
  EXPECT_STREQ(name_for_battery_type(BatteryType::None), "None");
  EXPECT_STREQ(name_for_battery_type(BatteryType::Pylon), "Pylon / Dyness compatible battery");
  EXPECT_STREQ(name_for_battery_type(BatteryType::TeslaModel3Y), "Tesla Model 3/Y");
  EXPECT_STREQ(name_for_battery_type(BatteryType::TeslaModelSX), "Tesla Model S/X");
}

TEST_F(BatteryCtorTest, CreateBatterySlotGating) {
  user_selected_battery_type = BatteryType::NissanLeaf;
  EXPECT_NE(create_battery(BatteryType::NissanLeaf, battery_slot_context(0)), nullptr);
  EXPECT_NE(create_battery(BatteryType::NissanLeaf, battery_slot_context(2)), nullptr);  // max 3

  user_selected_battery_type = BatteryType::StellantisEcmp;
  EXPECT_NE(create_battery(BatteryType::StellantisEcmp, battery_slot_context(2)), nullptr);  // max 3

  user_selected_battery_type = BatteryType::CmfaEv;
  EXPECT_NE(create_battery(BatteryType::CmfaEv, battery_slot_context(1)), nullptr);  // max 2
  EXPECT_EQ(create_battery(BatteryType::CmfaEv, battery_slot_context(2)), nullptr);  // beyond 2

  EXPECT_EQ(create_battery(BatteryType::None, battery_slot_context(0)), nullptr);
  EXPECT_EQ(create_battery(BatteryType::Highest, battery_slot_context(0)), nullptr);
}
