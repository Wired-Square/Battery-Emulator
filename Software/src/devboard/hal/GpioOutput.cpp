#include "GpioOutput.h"

#include <Arduino.h>

#include "../../communication/contactorcontrol/comm_contactorcontrol.h"
#include "hal.h"

bool GpioOutput::init(const char* owner) {
  if (!esp32hal->alloc_pins(owner, pin_)) {
    return false;
  }
  if (pwm_contactor_control && ledc_channel_ != kGpioOutputNoLedcChannel) {
    ledcAttachChannel(pin_, pwm_frequency, PWM_RESOLUTION, ledc_channel_);
    pwm_active_ = true;
  } else {
    pinMode(pin_, OUTPUT);
  }
  return true;
}

void GpioOutput::set(bool on) {
  if (pwm_active_) {
    ledcWrite(pin_, on ? PWM_ON_DUTY : PWM_OFF_DUTY);
    return;
  }
  if (contactor_control_inverted_logic) {
    on = !on;
  }
  digitalWrite(pin_, on ? HIGH : LOW);
}

void GpioOutput::set_raw(bool on) {
  digitalWrite(pin_, on ? HIGH : LOW);
}

void GpioOutput::set_hold() {
  if (pwm_active_) {
    ledcWrite(pin_, pwm_hold_duty);
    return;
  }
  set(true);
}

bool GpioOutput::level() {
  return digitalRead(pin_) == HIGH;
}
