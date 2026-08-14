#ifndef _PWM_TONE_H_
#define _PWM_TONE_H_

#include <soc/gpio_num.h>
#include <stdint.h>

// 50%-duty LEDC tone output; stop() drives the pin low as plain GPIO.
class PwmTone {
 public:
  constexpr PwmTone(gpio_num_t pin, uint8_t resolution_bits, int8_t ledc_channel)
      : pin_(pin), resolution_bits_(resolution_bits), ledc_channel_(ledc_channel) {}

  void start(uint32_t freq_hz);
  void write_tone(uint32_t freq_hz);
  void stop();

 private:
  gpio_num_t pin_;
  uint8_t resolution_bits_;
  int8_t ledc_channel_;
};

#endif
