#ifndef _GPIO_OUTPUT_H_
#define _GPIO_OUTPUT_H_

#include <soc/gpio_num.h>
#include <stdint.h>

#include "SwitchedOutput.h"

inline constexpr int8_t kGpioOutputNoLedcChannel = -1;
// Distinct LEDC channels; economisation and precharge coexist on a board.
inline constexpr int8_t kLedcChannelPositive = 0;
inline constexpr int8_t kLedcChannelNegative = 1;
inline constexpr int8_t kLedcChannelPrechargeTone = 2;

class GpioOutput : public SwitchedOutput {
 public:
  constexpr GpioOutput(gpio_num_t pin, int8_t ledc_channel = kGpioOutputNoLedcChannel)
      : pin_(pin), ledc_channel_(ledc_channel) {}

  bool init(const char* owner) override;
  void set(bool on) override;
  void set_raw(bool on) override;
  void set_hold() override;
  bool level() override;

  gpio_num_t pin() const { return pin_; }
  int8_t ledc_channel() const { return ledc_channel_; }

 private:
  gpio_num_t pin_;
  int8_t ledc_channel_;
  bool pwm_active_ = false;
};

#endif
