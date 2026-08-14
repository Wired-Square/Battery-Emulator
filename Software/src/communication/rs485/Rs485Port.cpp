#include "Rs485Port.h"

#include <Arduino.h>

#include "../../devboard/hal/hal.h"
#include "comm_rs485.h"

void Rs485Port::preinit() {
  if (pins_.de == GPIO_NUM_NC) {
    return;
  }
#ifdef BOARD_HAS_RS485_DE
  if (!esp32hal->alloc_pins("RS485 DE", pins_.de)) {
    DEBUG_PRINTF("RS485 failed to allocate DE pin\n");
    return;
  }
  de_available_ = true;
  // Idle receive state until the UART driver takes over this pin as RTS/DE
  // in begin(). UART_MODE_RS485_HALF_DUPLEX asserts RTS while the UART FIFO
  // is transmitting and deasserts it after the last bit was sent.
  pinMode(pins_.de, OUTPUT);
  digitalWrite(pins_.de, LOW);
#else
  DEBUG_PRINTF("RS485 DE pin wired but BOARD_HAS_RS485_DE is not defined for this env\n");
#endif
}

bool Rs485Port::begin(const char* owner, uint32_t baud, uint32_t config) {
  if (!esp32hal->alloc_pins(owner, pins_.rx, pins_.tx)) {
    return false;
  }

  serial_.begin(baud, config, pins_.rx, pins_.tx);

  if (de_available_) {
#ifdef BOARD_HAS_RS485_DE
    // Configured after serial_.begin(), because begin() configures the UART
    // pins.
    const esp_err_t pin_result = uart_set_pin(uart_num_, pins_.tx, pins_.rx, pins_.de, UART_PIN_NO_CHANGE);
    const esp_err_t mode_result = uart_set_mode(uart_num_, UART_MODE_RS485_HALF_DUPLEX);
    if (pin_result != ESP_OK || mode_result != ESP_OK) {
      DEBUG_PRINTF("RS485 UART half-duplex setup failed, pin_result=%d, mode_result=%d\n",
                   static_cast<int>(pin_result), static_cast<int>(mode_result));
      return false;
    }
#endif
  }

  return true;
}

void Rs485Port::register_receiver(Rs485Receiver* receiver) {
  receivers_.push_back(receiver);
}

void Rs485Port::poll() {
  if (receivers_.empty()) {
    return;
  }
  if (serial_.available() > 0) {
    mark_activity();
  }
  for (Rs485Receiver* receiver : receivers_) {
    receiver->receive();
  }
}

bool Rs485Port::recently_received(uint32_t hold_ms) const {
  return last_rx_ms_ != 0 && millis() - last_rx_ms_ < hold_ms;
}

void Rs485Port::mark_activity() {
  last_rx_ms_ = millis();
}
