#include "PwmTone.h"

#include <Arduino.h>

void PwmTone::start(uint32_t freq_hz) {
  ledcAttachChannel(pin_, freq_hz, resolution_bits_, ledc_channel_);
  ledcWriteTone(pin_, freq_hz);
}

void PwmTone::write_tone(uint32_t freq_hz) {
  ledcWriteTone(pin_, freq_hz);
}

void PwmTone::stop() {
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
}
