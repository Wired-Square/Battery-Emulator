#include <gtest/gtest.h>

#include <Arduino.h>

#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_stark.h"
#include "../Software/src/devboard/hal/hw_devkit.h"
#include "../Software/src/devboard/hal/hw_3LB.h"

TEST(StarkSwitchedOutputs, TableMatchesRetiredAccessors) {
  constexpr size_t kOpt5Group = 0;
  constexpr uint8_t kOpt5BmsPower23 = 0;
  constexpr uint8_t kOpt5BmsPower25 = 1;
  StarkHal hal;
  hal.set_gpio_option_value(kOpt5Group, kOpt5BmsPower23);
  SwitchedOutputList list = hal.switched_outputs();
  ASSERT_EQ(list.count, 6u);
  auto* pos = static_cast<GpioOutput*>(hal.switched_output(OutputRole::PositiveContactor));
  auto* neg = static_cast<GpioOutput*>(hal.switched_output(OutputRole::NegativeContactor));
  auto* prec = static_cast<GpioOutput*>(hal.switched_output(OutputRole::Precharge));
  auto* bms = static_cast<GpioOutput*>(hal.switched_output(OutputRole::BmsPower));
  auto* second = static_cast<GpioOutput*>(hal.switched_output(OutputRole::SecondBatteryContactors));
  auto* third = static_cast<GpioOutput*>(hal.switched_output(OutputRole::ThirdBatteryContactors));
  ASSERT_TRUE(pos && neg && prec && bms && second && third);
  EXPECT_EQ(pos->pin(), GPIO_NUM_32);
  EXPECT_EQ(pos->ledc_channel(), kLedcChannelPositive);
  EXPECT_EQ(neg->pin(), GPIO_NUM_33);
  EXPECT_EQ(neg->ledc_channel(), kLedcChannelNegative);
  EXPECT_EQ(prec->pin(), GPIO_NUM_25);
  EXPECT_EQ(prec->ledc_channel(), kGpioOutputNoLedcChannel);
  EXPECT_EQ(bms->pin(), GPIO_NUM_23);
  EXPECT_EQ(second->pin(), GPIO_NUM_19);
  EXPECT_EQ(third->pin(), GPIO_NUM_15);

  hal.set_gpio_option_value(kOpt5Group, kOpt5BmsPower25);
  prec = static_cast<GpioOutput*>(hal.switched_output(OutputRole::Precharge));
  bms = static_cast<GpioOutput*>(hal.switched_output(OutputRole::BmsPower));
  EXPECT_EQ(prec->pin(), GPIO_NUM_23);
  EXPECT_EQ(bms->pin(), GPIO_NUM_25);
}

TEST(DevKitSwitchedOutputs, TableMatchesRetiredAccessors) {
  DevKitHal hal;
  SwitchedOutputList list = hal.switched_outputs();
  ASSERT_EQ(list.count, 4u);
  auto* pos = static_cast<GpioOutput*>(hal.switched_output(OutputRole::PositiveContactor));
  auto* neg = static_cast<GpioOutput*>(hal.switched_output(OutputRole::NegativeContactor));
  auto* prec = static_cast<GpioOutput*>(hal.switched_output(OutputRole::Precharge));
  auto* second = static_cast<GpioOutput*>(hal.switched_output(OutputRole::SecondBatteryContactors));
  ASSERT_TRUE(pos && neg && prec && second);
  EXPECT_EQ(pos->pin(), GPIO_NUM_5);
  EXPECT_EQ(pos->ledc_channel(), kLedcChannelPositive);
  EXPECT_EQ(neg->pin(), GPIO_NUM_16);
  EXPECT_EQ(neg->ledc_channel(), kLedcChannelNegative);
  EXPECT_EQ(prec->pin(), GPIO_NUM_17);
  EXPECT_EQ(prec->ledc_channel(), kGpioOutputNoLedcChannel);
  EXPECT_EQ(second->pin(), GPIO_NUM_32);
  EXPECT_EQ(hal.switched_output(OutputRole::BmsPower), nullptr);
  EXPECT_EQ(hal.switched_output(OutputRole::ThirdBatteryContactors), nullptr);
}

TEST(ThreeLBSwitchedOutputs, TableMatchesRetiredAccessors) {
  ThreeLBHal hal;
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
  EXPECT_EQ(bms->pin(), GPIO_NUM_2);
  EXPECT_EQ(second->pin(), GPIO_NUM_13);
  EXPECT_EQ(hal.switched_output(OutputRole::ThirdBatteryContactors), nullptr);
}
