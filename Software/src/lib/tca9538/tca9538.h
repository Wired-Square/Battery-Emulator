/*
 * TI TCA9538 8-bit I2C IO expander.
 *
 * Register protocol and cached-output write-through model ported from
 * Zephyr's gpio_pca95xx driver (zephyrproject-rtos/zephyr,
 * SPDX-License-Identifier: Apache-2.0), reshaped onto Arduino Wire.
 */

#ifndef _TCA9538_H_
#define _TCA9538_H_

#include <Wire.h>
#include <stdint.h>

inline constexpr uint8_t TCA9538_PIN_COUNT = 8;
inline constexpr uint8_t TCA9538_REG_OUTPUT = 0x01;
inline constexpr uint8_t TCA9538_REG_CONFIG = 0x03;

// Write-only: outputs and pin directions. Writes are not serialised across
// tasks — callers must stay single-writer.
class Tca9538 {
 public:
  Tca9538(TwoWire& wire, uint8_t address) : wire_(wire), address_(address) {}

  // config_mask: 1 = input, 0 = output (register default is all inputs).
  // Output levels are written before pin directions so outputs assume a
  // defined level the moment they switch from the power-on input state.
  bool begin(uint8_t config_mask, uint8_t output_state);
  bool write_pin(uint8_t pin, bool level);
  bool ready() const { return ready_; }

 private:
  bool write_register(uint8_t reg, uint8_t value);

  TwoWire& wire_;
  uint8_t address_;
  uint8_t output_state_ = 0;
  bool ready_ = false;
};

#endif
