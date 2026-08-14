#include <gtest/gtest.h>

#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_lilygo.h"

// The host build compiles HW_LILYGO; these tests pin the LilyGo descriptor
// table and its migration mapping against the real board data.

class HalInterfaceTest : public testing::Test {
 public:
  void SetUp() override { init_hal(); }
};

TEST_F(HalInterfaceTest, LilyGoDescriptorTable) {
  InterfaceList list = esp32hal->interfaces();
  ASSERT_EQ(list.count, 5u);
  const InterfaceType types[] = {InterfaceType::Modbus, InterfaceType::Rs485, InterfaceType::CanNative,
                                 InterfaceType::CanMcp2515, InterfaceType::CanMcp2517fd};
  const comm_interface legacy[] = {comm_interface::Modbus, comm_interface::RS485, comm_interface::CanNative,
                                   comm_interface::CanAddonMcp2515, comm_interface::CanFdAddonMcp2518};
  const char* names[] = {"Modbus", "RS485", "CAN (Native)", "CAN (MCP2515 add-on)", "CAN FD (MCP2518 add-on)"};
  CanBus* const buses[] = {nullptr, nullptr, &lilygo_can_bus, &lilygo_mcp2515_bus, &lilygo_mcp2517fd_bus};
  Rs485Port* const ports[] = {&lilygo_rs485_port, &lilygo_rs485_port, nullptr, nullptr, nullptr};
  for (size_t i = 0; i < list.count; i++) {
    EXPECT_EQ(list.data[i].type, types[i]) << "index " << i;
    EXPECT_EQ(list.data[i].legacy_id, legacy[i]) << "index " << i;
    EXPECT_STREQ(descriptor_name(list.data[i]), names[i]) << "index " << i;
    EXPECT_EQ(list.data[i].can_bus, buses[i]) << "index " << i;
    EXPECT_EQ(list.data[i].rs485_port, ports[i]) << "index " << i;
  }
}

TEST_F(HalInterfaceTest, AllDescriptorsNamed) {
  InterfaceList list = esp32hal->interfaces();
  for (size_t i = 0; i < list.count; i++) {
    EXPECT_STRNE(descriptor_name(list.data[i]), "") << "index " << i;
  }
}

TEST_F(HalInterfaceTest, LilyGoMigrationMatrix) {
  InterfaceList list = esp32hal->interfaces();
  EXPECT_EQ(default_interface_config(list), pack_interface_config(InterfaceType::CanNative, 2));
  EXPECT_EQ(migrate_interface_config(list, 1), pack_interface_config(InterfaceType::Modbus, 0));
  EXPECT_EQ(migrate_interface_config(list, 2), pack_interface_config(InterfaceType::Rs485, 1));
  EXPECT_EQ(migrate_interface_config(list, 3), pack_interface_config(InterfaceType::CanNative, 2));
  // CanFdNative (4) and CanFdAddonMcp2518_2 (7) are not on LilyGo.
  EXPECT_EQ(migrate_interface_config(list, 4), default_interface_config(list));
  EXPECT_EQ(migrate_interface_config(list, 5), pack_interface_config(InterfaceType::CanMcp2515, 3));
  EXPECT_EQ(migrate_interface_config(list, 6), pack_interface_config(InterfaceType::CanMcp2517fd, 4));
  EXPECT_EQ(migrate_interface_config(list, 7), default_interface_config(list));
}

TEST_F(HalInterfaceTest, LilyGoModbusConsolidation) {
  InterfaceList list = esp32hal->interfaces();
  EXPECT_FALSE(descriptor_selectable(list.data[0]));
  uint32_t stored_modbus = migrate_interface_config(list, 1);
  EXPECT_EQ(consolidate_modbus_config(list, stored_modbus), pack_interface_config(InterfaceType::Rs485, 1));
  uint32_t stored_can = migrate_interface_config(list, 3);
  EXPECT_EQ(consolidate_modbus_config(list, stored_can), stored_can);
}

// Group indices mirror the kLilyGoGpioOptions ordering in hw_lilygo.h.
constexpr size_t kOpt2Group = 0;
constexpr uint8_t kOpt2BmsPower18 = 0;
constexpr uint8_t kOpt2BmsPower25 = 1;
constexpr size_t kOpt3Group = 1;
constexpr uint8_t kOpt3SmaEnable05 = 0;
constexpr uint8_t kOpt3SmaEnable33 = 1;
constexpr size_t kOpt4Group = 2;
constexpr uint8_t kOpt4SdCard = 0;
constexpr uint8_t kOpt4Display = 1;

TEST(LilyGoSwitchedOutputs, TableMatchesRetiredAccessors) {
  LilyGoHal hal;
  hal.set_gpio_option_value(kOpt2Group, kOpt2BmsPower18);
  SwitchedOutputList list = hal.switched_outputs();
  ASSERT_EQ(list.count, 5u);
  auto* pos = static_cast<GpioOutput*>(hal.switched_output(OutputRole::PositiveContactor));
  auto* neg = static_cast<GpioOutput*>(hal.switched_output(OutputRole::NegativeContactor));
  auto* prec = static_cast<GpioOutput*>(hal.switched_output(OutputRole::Precharge));
  auto* bms = static_cast<GpioOutput*>(hal.switched_output(OutputRole::BmsPower));
  auto* second = static_cast<GpioOutput*>(hal.switched_output(OutputRole::SecondBatteryContactors));
  ASSERT_TRUE(pos && neg && prec && bms && second);
  EXPECT_EQ(pos->pin(), GPIO_NUM_32);
  EXPECT_EQ(pos->ledc_channel(), kLedcChannelPositive);
  EXPECT_EQ(neg->pin(), GPIO_NUM_33);
  EXPECT_EQ(neg->ledc_channel(), kLedcChannelNegative);
  EXPECT_EQ(prec->pin(), GPIO_NUM_25);
  EXPECT_EQ(prec->ledc_channel(), kGpioOutputNoLedcChannel);
  EXPECT_EQ(bms->pin(), GPIO_NUM_18);
  EXPECT_EQ(second->pin(), GPIO_NUM_15);
  EXPECT_EQ(hal.switched_output(OutputRole::ThirdBatteryContactors), nullptr);

  hal.set_gpio_option_value(kOpt2Group, kOpt2BmsPower25);
  bms = static_cast<GpioOutput*>(hal.switched_output(OutputRole::BmsPower));
  EXPECT_EQ(bms->pin(), GPIO_NUM_25);
}

TEST(LilyGoGpioOptions, PinsMatchRetiredLadders) {
  LilyGoHal hal;

  hal.set_gpio_option_value(kOpt3Group, kOpt3SmaEnable05);
  EXPECT_EQ(hal.INVERTER_CONTACTOR_ENABLE_PIN(), GPIO_NUM_5);
  hal.set_gpio_option_value(kOpt3Group, kOpt3SmaEnable33);
  EXPECT_EQ(hal.INVERTER_CONTACTOR_ENABLE_PIN(), GPIO_NUM_33);

  hal.set_gpio_option_value(kOpt4Group, kOpt4SdCard);
  EXPECT_EQ(hal.SD_MISO_PIN(), GPIO_NUM_2);
  EXPECT_EQ(hal.SD_MOSI_PIN(), GPIO_NUM_15);
  EXPECT_EQ(hal.SD_SCLK_PIN(), GPIO_NUM_14);
  EXPECT_EQ(hal.SD_CS_PIN(), GPIO_NUM_13);
  EXPECT_EQ(hal.DISPLAY_SDA_PIN(), GPIO_NUM_NC);
  EXPECT_EQ(hal.DISPLAY_SCL_PIN(), GPIO_NUM_NC);

  hal.set_gpio_option_value(kOpt4Group, kOpt4Display);
  EXPECT_EQ(hal.SD_MISO_PIN(), GPIO_NUM_NC);
  EXPECT_EQ(hal.SD_MOSI_PIN(), GPIO_NUM_NC);
  EXPECT_EQ(hal.SD_SCLK_PIN(), GPIO_NUM_NC);
  EXPECT_EQ(hal.SD_CS_PIN(), GPIO_NUM_NC);
  EXPECT_EQ(hal.DISPLAY_SDA_PIN(), GPIO_NUM_15);
  EXPECT_EQ(hal.DISPLAY_SCL_PIN(), GPIO_NUM_14);
}

// Guards the exact frequencies the settings page renders (400 kHz clock).
static_assert(load_switch_pwm_frequency_hz(400000, 0) == 781);
static_assert(load_switch_pwm_frequency_hz(400000, 1) == 391);
static_assert(load_switch_pwm_frequency_hz(400000, 2) == 195);
static_assert(load_switch_pwm_frequency_hz(400000, 3) == 98);
static_assert(load_switch_divisor_ratio(0) == 512);
static_assert(load_switch_divisor_ratio(3) == 4096);
