#include "tca9538.h"

bool Tca9538::begin(uint8_t config_mask, uint8_t output_state) {
  ready_ = write_register(TCA9538_REG_OUTPUT, output_state) && write_register(TCA9538_REG_CONFIG, config_mask);
  if (ready_) {
    output_state_ = output_state;
  }
  return ready_;
}

bool Tca9538::write_pin(uint8_t pin, bool level) {
  if (!ready_ || pin >= TCA9538_PIN_COUNT) {
    return false;
  }
  uint8_t new_state = level ? (output_state_ | (uint8_t)(1u << pin)) : (output_state_ & (uint8_t)~(1u << pin));
  if (!write_register(TCA9538_REG_OUTPUT, new_state)) {
    return false;
  }
  output_state_ = new_state;
  return true;
}

bool Tca9538::write_register(uint8_t reg, uint8_t value) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  wire_.write(value);
  return wire_.endTransmission() == 0;
}
