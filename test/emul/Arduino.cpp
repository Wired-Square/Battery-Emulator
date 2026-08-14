#include "Arduino.h"

#include "../../Software/src/communication/can/comm_can.h"

// Provide the definition that was previously in USER_SETTINGS.cpp
volatile CAN_Configuration can_config = {};

std::vector<GpioEvent> gpio_events;
std::array<uint8_t, 256> gpio_levels = {};

void delay(unsigned long ms) {}
void delayMicroseconds(unsigned long us) {}
int digitalRead(uint8_t pin) {
  return gpio_levels[pin];
}
void digitalWrite(uint8_t pin, uint8_t val) {
  gpio_events.push_back({GpioEvent::DigitalWriteSet, pin, val});
  gpio_levels[pin] = val;
}

unsigned long micros() {
  return 0;
}
void pinMode(uint8_t pin, uint8_t mode) {
  gpio_events.push_back({GpioEvent::PinModeSet, pin, mode});
}

int max(int a, int b) {
  return (a > b) ? a : b;
}

bool ledcAttachChannel(uint8_t pin, uint32_t freq, uint8_t resolution, int8_t channel) {
  gpio_events.push_back({GpioEvent::LedcAttach, pin, freq});
  return true;
}
bool ledcWrite(uint8_t pin, uint32_t duty) {
  gpio_events.push_back({GpioEvent::LedcWriteSet, pin, duty});
  return true;
}

uint32_t ledcWriteTone(uint8_t pin, uint32_t freq) {
  gpio_events.push_back({GpioEvent::LedcWriteTone, pin, freq});
  return freq;
}

ESPClass ESP;
