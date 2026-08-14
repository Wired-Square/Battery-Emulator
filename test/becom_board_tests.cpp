#include <gtest/gtest.h>

#include <Arduino.h>

#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_becom.h"

TEST(BEComSwitchedOutputs, TableMatchesRetiredAccessors) {
  BEComHal hal;
  SwitchedOutputList list = hal.switched_outputs();
  ASSERT_EQ(list.count, 5u);
  auto* pos = static_cast<GpioOutput*>(hal.switched_output(OutputRole::PositiveContactor));
  auto* neg = static_cast<GpioOutput*>(hal.switched_output(OutputRole::NegativeContactor));
  auto* prec = static_cast<GpioOutput*>(hal.switched_output(OutputRole::Precharge));
  auto* bms = static_cast<GpioOutput*>(hal.switched_output(OutputRole::BmsPower));
  auto* second = static_cast<GpioOutput*>(hal.switched_output(OutputRole::SecondBatteryContactors));
  ASSERT_TRUE(pos && neg && prec && bms && second);
  EXPECT_EQ(pos->pin(), GPIO_NUM_47);
  EXPECT_EQ(pos->ledc_channel(), kLedcChannelPositive);
  EXPECT_EQ(neg->pin(), GPIO_NUM_48);
  EXPECT_EQ(neg->ledc_channel(), kLedcChannelNegative);
  EXPECT_EQ(prec->pin(), GPIO_NUM_45);
  EXPECT_EQ(prec->ledc_channel(), kGpioOutputNoLedcChannel);
  EXPECT_EQ(bms->pin(), GPIO_NUM_1);
  EXPECT_EQ(second->pin(), GPIO_NUM_37);
  EXPECT_EQ(hal.switched_output(OutputRole::ThirdBatteryContactors), nullptr);
}
