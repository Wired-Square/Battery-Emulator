#include <gtest/gtest.h>

#include <Wire.h>

#include "../Software/src/devboard/hal/hw_wiredflexlink.h"

#include "Arduino.h"

// Host-side characterisation of the WiredFlexLink board data: descriptor
// table, migration matrix and expander bring-up.

class WflBoardTest : public testing::Test {
 public:
  void SetUp() override {
    Wire.transmissions.clear();
    Wire.fail_writes = false;
  }
  WiredFlexLinkHal hal;
};

TEST_F(WflBoardTest, DescriptorTable) {
  InterfaceList list = hal.interfaces();
  ASSERT_EQ(list.count, 4u);
  EXPECT_EQ(list.data[0].type, InterfaceType::CanNative);
  EXPECT_STREQ(descriptor_name(list.data[0]), "CAN0");
  EXPECT_EQ(list.data[0].legacy_id, comm_interface::CanNative);
  EXPECT_EQ(list.data[0].can_bus, &wfl_can0_bus);
  EXPECT_EQ(list.data[0].rs485_port, nullptr);
  EXPECT_EQ(list.data[1].type, InterfaceType::CanNative);
  EXPECT_STREQ(descriptor_name(list.data[1]), "CAN1");
  EXPECT_EQ(list.data[1].legacy_id, comm_interface::Highest);
  EXPECT_EQ(list.data[1].can_bus, &wfl_can1_bus);
  EXPECT_EQ(list.data[1].rs485_port, nullptr);
  EXPECT_EQ(list.data[2].type, InterfaceType::Rs485);
  EXPECT_STREQ(descriptor_name(list.data[2]), "RS0");
  EXPECT_EQ(list.data[2].legacy_id, comm_interface::Highest);
  EXPECT_EQ(list.data[2].can_bus, nullptr);
  EXPECT_EQ(list.data[2].rs485_port, &wfl_rs0_port);
  EXPECT_EQ(list.data[3].type, InterfaceType::Rs485);
  EXPECT_STREQ(descriptor_name(list.data[3]), "RS1");
  EXPECT_EQ(list.data[3].legacy_id, comm_interface::Highest);
  EXPECT_EQ(list.data[3].can_bus, nullptr);
  EXPECT_EQ(list.data[3].rs485_port, &wfl_rs1_port);
}

TEST_F(WflBoardTest, MigrationMatrix) {
  InterfaceList list = hal.interfaces();
  EXPECT_EQ(default_interface_config(list), pack_interface_config(InterfaceType::CanNative, 0));
  // Only legacy CanNative (3) ever existed on this board; everything else
  // falls back to the default.
  for (uint32_t legacy = 0; legacy <= (uint32_t)comm_interface::Highest; legacy++) {
    uint32_t expected = legacy == (uint32_t)comm_interface::CanNative
                            ? pack_interface_config(InterfaceType::CanNative, 0)
                            : default_interface_config(list);
    EXPECT_EQ(migrate_interface_config(list, legacy), expected) << "legacy " << legacy;
  }
  // CAN1 is reachable only as a packed value, never via migration.
  uint32_t can1 = pack_interface_config(InterfaceType::CanNative, 1);
  EXPECT_EQ(migrate_interface_config(list, can1), can1);
  EXPECT_EQ(resolve_interface_config(list, can1), &list.data[1]);
  uint32_t rs0 = pack_interface_config(InterfaceType::Rs485, 2);
  EXPECT_EQ(migrate_interface_config(list, rs0), rs0);
  EXPECT_EQ(resolve_interface_config(list, rs0), &list.data[2]);
  uint32_t rs1 = pack_interface_config(InterfaceType::Rs485, 3);
  EXPECT_EQ(migrate_interface_config(list, rs1), rs1);
  EXPECT_EQ(resolve_interface_config(list, rs1), &list.data[3]);
}

TEST_F(WflBoardTest, BoardInitConfiguresExpander) {
  hal.board_init();
  ASSERT_EQ(Wire.transmissions.size(), 2u);
  EXPECT_EQ(Wire.transmissions[0].address, kWflExpanderAddress);
  EXPECT_EQ(Wire.transmissions[0].bytes, (std::vector<uint8_t>{TCA9538_REG_OUTPUT, kWflExpanderOutputInit}));
  EXPECT_EQ(Wire.transmissions[1].bytes, (std::vector<uint8_t>{TCA9538_REG_CONFIG, kWflExpanderConfigMask}));
}

TEST_F(WflBoardTest, TerminationCapabilityMap) {
  EXPECT_TRUE(hal.supports_interface_termination(0));
  EXPECT_TRUE(hal.supports_interface_termination(1));
  EXPECT_TRUE(hal.supports_interface_termination(2));
  EXPECT_TRUE(hal.supports_interface_termination(3));
  EXPECT_FALSE(hal.supports_interface_termination(4));
}

TEST_F(WflBoardTest, TerminationWritesExpanderBits) {
  hal.board_init();
  Wire.transmissions.clear();

  EXPECT_TRUE(hal.set_interface_termination(0, true));
  ASSERT_EQ(Wire.transmissions.size(), 1u);
  EXPECT_EQ(Wire.transmissions[0].bytes, (std::vector<uint8_t>{TCA9538_REG_OUTPUT, (uint8_t)(1u << kWflExpTermCan0)}));

  EXPECT_TRUE(hal.set_interface_termination(1, true));
  EXPECT_EQ(Wire.transmissions[1].bytes,
            (std::vector<uint8_t>{TCA9538_REG_OUTPUT, (uint8_t)((1u << kWflExpTermCan0) | (1u << kWflExpTermCan1))}));

  EXPECT_TRUE(hal.set_interface_termination(0, false));
  EXPECT_EQ(Wire.transmissions[2].bytes, (std::vector<uint8_t>{TCA9538_REG_OUTPUT, (uint8_t)(1u << kWflExpTermCan1)}));

  EXPECT_TRUE(hal.set_interface_termination(2, true));
  EXPECT_EQ(Wire.transmissions[3].bytes,
            (std::vector<uint8_t>{TCA9538_REG_OUTPUT, (uint8_t)((1u << kWflExpTermCan1) | (1u << kWflExpTermRs0))}));

  EXPECT_TRUE(hal.set_interface_termination(3, true));
  EXPECT_EQ(Wire.transmissions[4].bytes,
            (std::vector<uint8_t>{TCA9538_REG_OUTPUT,
                                  (uint8_t)((1u << kWflExpTermCan1) | (1u << kWflExpTermRs0) | (1u << kWflExpTermRs1))}));

  EXPECT_FALSE(hal.set_interface_termination(4, true));
}

TEST_F(WflBoardTest, LedRoleMap) {
  EXPECT_EQ(hal.LED_COUNT(), kWflLedCount);
  EXPECT_EQ(hal.LED_STATUS_INDEX(), kWflLedStatus);
  // Looks backwards but isn't: the vendored NeoPixel fork's fixed byte offsets
  // invert the enum, so RGB emits GRB on the wire — which is what this board's
  // WS2812s are.
  EXPECT_EQ(hal.LED_COLOR_ORDER(), led_color_order::RGB);
  EXPECT_EQ(hal.LED_INTERFACE_ACTIVITY_INDEX(0), kWflLedCan0);
  EXPECT_EQ(hal.LED_INTERFACE_ACTIVITY_INDEX(1), kWflLedCan1);
  EXPECT_EQ(hal.LED_INTERFACE_ACTIVITY_INDEX(2), kWflLedRs0);
  EXPECT_EQ(hal.LED_INTERFACE_ACTIVITY_INDEX(3), kWflLedRs1);
  EXPECT_EQ(hal.LED_INTERFACE_ACTIVITY_INDEX(4), -1);
}

TEST_F(WflBoardTest, LoadSwitchBindingsFollowConfiguredRoles) {
  esp32hal = &hal;
  init_events();
  SPIClass::frames.clear();
  SPIClass::rx_queue.clear();
  gpio_events.clear();

  wfl_load_switch.set_channel_role(0, LoadSwitchRole::PositiveContactor);
  wfl_load_switch.set_channel_role(1, LoadSwitchRole::Manual);
  wfl_load_switch.set_channel_role(2, LoadSwitchRole::BmsPower);
  wfl_load_switch.set_channel_role(3, LoadSwitchRole::Disabled);

  for (int i = 0; i < 4; i++) {
    SPIClass::rx_queue.push_back({0x00, 0x00, 0x01});  // parity-valid zero frames
  }
  SPIClass::rx_queue.push_back({0x00, PC3_VN9D5D20FN, 0x00});

  hal.board_init();

  ASSERT_TRUE(wfl_load_switch.status().device_ok);
  EXPECT_EQ(hal.load_switch(), &wfl_load_switch);
  EXPECT_EQ(hal.switched_outputs().count, 2u);
  EXPECT_EQ(hal.switched_output(OutputRole::PositiveContactor), &wfl_load_switch_outputs[0]);
  EXPECT_EQ(hal.switched_output(OutputRole::BmsPower), &wfl_load_switch_outputs[2]);
  EXPECT_EQ(hal.switched_output(OutputRole::NegativeContactor), nullptr);
  EXPECT_EQ(hal.switched_output(OutputRole::Precharge), nullptr);

  EXPECT_EQ(hal.LED_SWITCHED_OUTPUT_INDEX(0), 1);
  EXPECT_EQ(hal.LED_SWITCHED_OUTPUT_INDEX(3), 4);
  EXPECT_EQ(hal.LED_SWITCHED_OUTPUT_INDEX(4), -1);

  // Expander bring-up: all outputs (config 0x00), outputs low first.
  ASSERT_GE(Wire.transmissions.size(), 2u);
  EXPECT_EQ(Wire.transmissions[0].bytes, (std::vector<uint8_t>{TCA9538_REG_OUTPUT, 0x00}));
  EXPECT_EQ(Wire.transmissions[1].bytes, (std::vector<uint8_t>{TCA9538_REG_CONFIG, 0x00}));
}
