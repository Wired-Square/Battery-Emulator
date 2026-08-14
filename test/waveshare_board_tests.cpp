#include <gtest/gtest.h>

#include <Arduino.h>

#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_waveshare.h"

TEST(WaveshareSwitchedOutputs, TableMatchesRetiredAccessors) {
  WaveshareS3Rs485CanHal hal;
  SwitchedOutputList list = hal.switched_outputs();
  ASSERT_EQ(list.count, 5u);
  auto* pos = static_cast<GpioOutput*>(hal.switched_output(OutputRole::PositiveContactor));
  auto* neg = static_cast<GpioOutput*>(hal.switched_output(OutputRole::NegativeContactor));
  auto* prec = static_cast<GpioOutput*>(hal.switched_output(OutputRole::Precharge));
  auto* bms = static_cast<GpioOutput*>(hal.switched_output(OutputRole::BmsPower));
  auto* second = static_cast<GpioOutput*>(hal.switched_output(OutputRole::SecondBatteryContactors));
  ASSERT_TRUE(pos && neg && prec && bms && second);
  EXPECT_EQ(pos->pin(), GPIO_NUM_3);
  EXPECT_EQ(pos->ledc_channel(), kLedcChannelPositive);
  EXPECT_EQ(neg->pin(), GPIO_NUM_4);
  EXPECT_EQ(neg->ledc_channel(), kLedcChannelNegative);
  EXPECT_EQ(prec->pin(), GPIO_NUM_5);
  EXPECT_EQ(prec->ledc_channel(), kGpioOutputNoLedcChannel);
  EXPECT_EQ(bms->pin(), GPIO_NUM_6);
  EXPECT_EQ(second->pin(), GPIO_NUM_8);
  EXPECT_EQ(hal.switched_output(OutputRole::ThirdBatteryContactors), nullptr);
}

TEST(WaveshareGpioOption6, PinsMatchRetiredLadder) {
  constexpr size_t kOpt6Group = 0;
  constexpr uint8_t kOpt6StatusLed = 0;
  constexpr uint8_t kOpt6Display = 1;
  WaveshareS3Rs485CanHal hal;

  hal.set_gpio_option_value(kOpt6Group, kOpt6StatusLed);
  EXPECT_EQ(hal.LED_PIN(), GPIO_NUM_2);
  EXPECT_EQ(hal.DISPLAY_SDA_PIN(), GPIO_NUM_NC);
  EXPECT_EQ(hal.DISPLAY_SCL_PIN(), GPIO_NUM_NC);

  hal.set_gpio_option_value(kOpt6Group, kOpt6Display);
  EXPECT_EQ(hal.LED_PIN(), GPIO_NUM_NC);
  EXPECT_EQ(hal.DISPLAY_SDA_PIN(), GPIO_NUM_1);
  EXPECT_EQ(hal.DISPLAY_SCL_PIN(), GPIO_NUM_2);
}
