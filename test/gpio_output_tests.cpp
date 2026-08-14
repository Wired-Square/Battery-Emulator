#include <gtest/gtest.h>

#include "../Software/src/communication/contactorcontrol/comm_contactorcontrol.h"
#include "../Software/src/devboard/hal/GpioOutput.h"
#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_lilygo.h"

#include "Arduino.h"

class GpioOutputTest : public testing::Test {
 public:
  void SetUp() override {
    // Fresh HAL per test: resets the pin-allocation map.
    esp32hal = new LilyGoHal();
    gpio_events.clear();
    gpio_levels = {};
    pwm_contactor_control = false;
    contactor_control_inverted_logic = false;
    pwm_frequency = 20000;
    pwm_hold_duty = 250;
  }
};

TEST_F(GpioOutputTest, DigitalInitAndSetSequence) {
  GpioOutput out(GPIO_NUM_32, kLedcChannelPositive);
  ASSERT_TRUE(out.init("Contactors"));
  ASSERT_EQ(gpio_events.size(), 1u);
  EXPECT_EQ(gpio_events[0].type, GpioEvent::PinModeSet);
  EXPECT_EQ(gpio_events[0].pin, 32);

  out.set(true);
  out.set(false);
  ASSERT_EQ(gpio_events.size(), 3u);
  EXPECT_EQ(gpio_events[1].type, GpioEvent::DigitalWriteSet);
  EXPECT_EQ(gpio_events[1].value, HIGH);
  EXPECT_EQ(gpio_events[2].value, LOW);
}

TEST_F(GpioOutputTest, InvertedLogicFlipsDigitalPathOnly) {
  contactor_control_inverted_logic = true;
  GpioOutput out(GPIO_NUM_33);
  ASSERT_TRUE(out.init("Contactors"));
  out.set(true);
  EXPECT_EQ(gpio_events.back().value, LOW);
  out.set(false);
  EXPECT_EQ(gpio_events.back().value, HIGH);
  // set_raw ignores inversion (BMS power semantics).
  out.set_raw(true);
  EXPECT_EQ(gpio_events.back().value, HIGH);
}

TEST_F(GpioOutputTest, PwmPathAttachesAndNeverInverts) {
  pwm_contactor_control = true;
  contactor_control_inverted_logic = true;
  GpioOutput out(GPIO_NUM_32, kLedcChannelPositive);
  ASSERT_TRUE(out.init("Contactors"));
  ASSERT_EQ(gpio_events.size(), 1u);
  EXPECT_EQ(gpio_events[0].type, GpioEvent::LedcAttach);
  EXPECT_EQ(gpio_events[0].value, pwm_frequency);

  out.set(true);
  EXPECT_EQ(gpio_events.back().type, GpioEvent::LedcWriteSet);
  EXPECT_EQ(gpio_events.back().value, PWM_ON_DUTY);
  out.set_hold();
  EXPECT_EQ(gpio_events.back().value, pwm_hold_duty);
  out.set(false);
  EXPECT_EQ(gpio_events.back().value, PWM_OFF_DUTY);
}

TEST_F(GpioOutputTest, NoLedcChannelStaysDigitalUnderPwmControl) {
  pwm_contactor_control = true;
  GpioOutput out(GPIO_NUM_25);
  ASSERT_TRUE(out.init("Contactors"));
  EXPECT_EQ(gpio_events[0].type, GpioEvent::PinModeSet);
  out.set(true);
  EXPECT_EQ(gpio_events.back().type, GpioEvent::DigitalWriteSet);
  // Hold without PWM re-asserts ON — the digital path has no hold level.
  out.set_hold();
  EXPECT_EQ(gpio_events.back().type, GpioEvent::DigitalWriteSet);
  EXPECT_EQ(gpio_events.back().value, HIGH);
}

TEST_F(GpioOutputTest, LevelReadsBackWrittenState) {
  GpioOutput out(GPIO_NUM_15);
  ASSERT_TRUE(out.init("Contactors"));
  out.set(true);
  EXPECT_TRUE(out.level());
  out.set(false);
  EXPECT_FALSE(out.level());
}

TEST_F(GpioOutputTest, InitFailsOnPinConflict) {
  GpioOutput a(GPIO_NUM_32);
  GpioOutput b(GPIO_NUM_32);
  ASSERT_TRUE(a.init("Contactors"));
  EXPECT_FALSE(b.init("BMS power"));
}

TEST_F(GpioOutputTest, HalLookupFindsBoundRoleAndMissesUnbound) {
  static GpioOutput out(GPIO_NUM_32);
  static const SwitchedOutputBinding bindings[] = {{OutputRole::PositiveContactor, &out}};
  struct TableHal : LilyGoHal {
    SwitchedOutputList switched_outputs() override { return make_switched_output_list(bindings); }
  };
  // switched_outputs is boot-stable board data; the test double stands in
  // for a board header table.
  TableHal hal;
  EXPECT_EQ(hal.switched_output(OutputRole::PositiveContactor), &out);
  EXPECT_EQ(hal.switched_output(OutputRole::BmsPower), nullptr);
}
