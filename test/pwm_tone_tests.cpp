#include <gtest/gtest.h>

#include "../Software/src/devboard/hal/GpioOutput.h"
#include "../Software/src/devboard/hal/PwmTone.h"

#include "Arduino.h"

namespace {
constexpr uint8_t kTestResolutionBits = 8;
constexpr uint32_t kTestFreqHz = 11000;
}  // namespace

class PwmToneTest : public testing::Test {
 public:
  void SetUp() override {
    gpio_events.clear();
    gpio_levels = {};
  }
};

TEST_F(PwmToneTest, StartAttachesChannelThenWritesTone) {
  PwmTone tone(GPIO_NUM_25, kTestResolutionBits, kLedcChannelPrechargeTone);
  tone.start(kTestFreqHz);
  ASSERT_EQ(gpio_events.size(), 2u);
  EXPECT_EQ(gpio_events[0].type, GpioEvent::LedcAttach);
  EXPECT_EQ(gpio_events[0].pin, 25);
  EXPECT_EQ(gpio_events[0].value, kTestFreqHz);
  EXPECT_EQ(gpio_events[1].type, GpioEvent::LedcWriteTone);
  EXPECT_EQ(gpio_events[1].value, kTestFreqHz);
}

TEST_F(PwmToneTest, WriteToneOnlyUpdatesFrequency) {
  PwmTone tone(GPIO_NUM_25, kTestResolutionBits, kLedcChannelPrechargeTone);
  tone.write_tone(kTestFreqHz + 500);
  ASSERT_EQ(gpio_events.size(), 1u);
  EXPECT_EQ(gpio_events[0].type, GpioEvent::LedcWriteTone);
  EXPECT_EQ(gpio_events[0].value, kTestFreqHz + 500);
}

TEST_F(PwmToneTest, StopReturnsPinToDrivenLowGpio) {
  PwmTone tone(GPIO_NUM_25, kTestResolutionBits, kLedcChannelPrechargeTone);
  tone.stop();
  ASSERT_EQ(gpio_events.size(), 2u);
  EXPECT_EQ(gpio_events[0].type, GpioEvent::PinModeSet);
  EXPECT_EQ(gpio_events[0].value, OUTPUT);
  EXPECT_EQ(gpio_events[1].type, GpioEvent::DigitalWriteSet);
  EXPECT_EQ(gpio_events[1].value, LOW);
}
