#ifndef _SMA_INVERTER_BASE_H
#define _SMA_INVERTER_BASE_H

#include <Arduino.h>
#include "../datalayer/datalayer.h"
#include "../devboard/hal/hal.h"
#include "CanInverterProtocol.h"

class SmaInverterBase : public CanInverterProtocol {
 public:
  SmaInverterBase() { contactorEnablePin = esp32hal->INVERTER_CONTACTOR_ENABLE_PIN(); }
  bool allows_contactor_closing() override { return digitalRead(contactorEnablePin) == 1; }

  // SMA inverters can take a while to boot before they start sending CAN.
  bool needs_can_startup_grace() override { return true; }

  virtual bool setup() override {
    datalayer.system.status.inverter_allows_contactor_closing = false;  // The inverter needs to allow first

    if (!esp32hal->alloc_pins("SMA inverter", contactorEnablePin)) {
      return false;
    }
    pinMode(contactorEnablePin, INPUT);

    contactorLedPin = esp32hal->INVERTER_CONTACTOR_ENABLE_LED_PIN();
    if (contactorLedPin != GPIO_NUM_NC) {
      if (!esp32hal->alloc_pins("SMA inverter", contactorLedPin)) {
        return false;
      }
      pinMode(contactorLedPin, OUTPUT);
      digitalWrite(contactorLedPin, LOW);  // Turn LED off, until inverter allows contactor closing
    }

    return true;
  }

 protected:
  static constexpr uint8_t READY_STATE = 0x03;
  static constexpr uint8_t STOP_STATE = 0x02;
  static constexpr uint16_t THIRTY_MINUTES = 1200;

  // current_dA is the per-model current source (reported_current_dA for H/HVS, current_dA for SBS).
  void fill_measurement_frames(CAN_frame& f358, CAN_frame& f3D8, CAN_frame& f4D8, CAN_frame& f518, CAN_frame& f458,
                               int16_t current_dA);

  void check_enable_line();

  int16_t temperature_average = 0;
  uint16_t ampere_hours_remaining = 0;  // retained: recomputed only when voltage_dV > 10
  uint16_t timeWithoutInverterAllowsContactorClosing = 0;

  void control_contactor_led() {
    if (contactorLedPin != GPIO_NUM_NC) {
      if (datalayer.system.status.inverter_allows_contactor_closing) {
        digitalWrite(contactorLedPin,
                     HIGH);  // Turn on LED to indicate that SMA inverter allows contactor closing
      } else {
        digitalWrite(contactorLedPin,
                     LOW);  // Turn off LED to indicate that SMA inverter does not allow contactor closing
      }
    }
  }

 private:
  gpio_num_t contactorEnablePin;
  gpio_num_t contactorLedPin;
};

#endif
