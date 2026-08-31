#include "Rs485Port.h"

#include <Arduino.h>

#include "../../devboard/hal/hal.h"
#include "comm_rs485.h"

void Rs485Port::preinit() {
  if (pins_.de == GPIO_NUM_NC) {
    return;
  }
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
}

bool Rs485Port::begin(const char* owner, uint32_t baud, uint32_t config) {
  // Before the UART starts, so a port whose DE pin was never claimed is refused
  // without leaving a running transceiver that cannot release the bus.
  if (pins_.de != GPIO_NUM_NC && !de_available_) {
    DEBUG_PRINTF("RS485 DE pin unavailable, refusing to open the port\n");
    return false;
  }

  if (!esp32hal->alloc_pins(owner, pins_.rx, pins_.tx)) {
    return false;
  }

  serial_.begin(baud, config, pins_.rx, pins_.tx);

  // Programmed after serial_.begin(), which reassigns the UART pins. The port is
  // closed again on failure because callers are not obliged to read this return.
  if (de_available_ && !esp32hal->configure_rs485_half_duplex(*this)) {
    DEBUG_PRINTF("RS485 half-duplex setup failed, closing the port\n");
    serial_.end();
    return false;
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
