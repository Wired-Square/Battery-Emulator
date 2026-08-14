#include <gtest/gtest.h>

#include <freertos/FreeRTOS.h>
#include <Arduino.h>

#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_lilygo2can.h"

constexpr size_t kOpt1Group = 0;
constexpr uint8_t kOpt1Wup = 0;
constexpr uint8_t kOpt1Display = 1;
constexpr uint8_t kOpt1EstopBmsPower = 2;

TEST(LilyGo2CanSwitchedOutputs, TableMatchesRetiredAccessors) {
  LilyGo2CANHal hal;
  hal.set_gpio_option_value(kOpt1Group, kOpt1Wup);
  SwitchedOutputList list = hal.switched_outputs();
  ASSERT_EQ(list.count, 6u);
  auto* pos = static_cast<GpioOutput*>(hal.switched_output(OutputRole::PositiveContactor));
  auto* neg = static_cast<GpioOutput*>(hal.switched_output(OutputRole::NegativeContactor));
  auto* prec = static_cast<GpioOutput*>(hal.switched_output(OutputRole::Precharge));
  auto* bms = static_cast<GpioOutput*>(hal.switched_output(OutputRole::BmsPower));
  auto* second = static_cast<GpioOutput*>(hal.switched_output(OutputRole::SecondBatteryContactors));
  auto* third = static_cast<GpioOutput*>(hal.switched_output(OutputRole::ThirdBatteryContactors));
  ASSERT_TRUE(pos && neg && prec && bms && second && third);
  EXPECT_EQ(pos->pin(), GPIO_NUM_48);
  EXPECT_EQ(pos->ledc_channel(), kLedcChannelPositive);
  EXPECT_EQ(neg->pin(), GPIO_NUM_17);
  EXPECT_EQ(neg->ledc_channel(), kLedcChannelNegative);
  EXPECT_EQ(prec->pin(), GPIO_NUM_21);
  EXPECT_EQ(prec->ledc_channel(), kGpioOutputNoLedcChannel);
  EXPECT_EQ(bms->pin(), hal.is_fd() ? GPIO_NUM_45 : GPIO_NUM_3);
  EXPECT_EQ(second->pin(), GPIO_NUM_5);
  EXPECT_EQ(third->pin(), GPIO_NUM_4);

  hal.set_gpio_option_value(kOpt1Group, kOpt1EstopBmsPower);
  bms = static_cast<GpioOutput*>(hal.switched_output(OutputRole::BmsPower));
  EXPECT_EQ(bms->pin(), GPIO_NUM_2);
}

TEST(LilyGo2CanGpioOption1, PinsMatchRetiredLadders) {
  LilyGo2CANHal hal;

  hal.set_gpio_option_value(kOpt1Group, kOpt1Wup);
  EXPECT_EQ(hal.WUP_PIN1(), GPIO_NUM_1);
  EXPECT_EQ(hal.WUP_PIN2(), GPIO_NUM_2);
  EXPECT_EQ(hal.EQUIPMENT_STOP_PIN(), GPIO_NUM_36);
  EXPECT_EQ(hal.DISPLAY_SDA_PIN(), GPIO_NUM_NC);
  EXPECT_EQ(hal.DISPLAY_SCL_PIN(), GPIO_NUM_NC);

  hal.set_gpio_option_value(kOpt1Group, kOpt1Display);
  EXPECT_EQ(hal.WUP_PIN1(), GPIO_NUM_18);
  EXPECT_EQ(hal.WUP_PIN2(), GPIO_NUM_14);
  EXPECT_EQ(hal.EQUIPMENT_STOP_PIN(), GPIO_NUM_36);
  EXPECT_EQ(hal.DISPLAY_SDA_PIN(), GPIO_NUM_1);
  EXPECT_EQ(hal.DISPLAY_SCL_PIN(), GPIO_NUM_2);

  hal.set_gpio_option_value(kOpt1Group, kOpt1EstopBmsPower);
  EXPECT_EQ(hal.WUP_PIN1(), GPIO_NUM_18);
  EXPECT_EQ(hal.WUP_PIN2(), GPIO_NUM_14);
  EXPECT_EQ(hal.EQUIPMENT_STOP_PIN(), GPIO_NUM_1);
  EXPECT_EQ(hal.DISPLAY_SDA_PIN(), GPIO_NUM_NC);
  EXPECT_EQ(hal.DISPLAY_SCL_PIN(), GPIO_NUM_NC);
}
