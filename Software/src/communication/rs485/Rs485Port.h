#ifndef _RS485_PORT_H_
#define _RS485_PORT_H_

#include <HardwareSerial.h>
#include <soc/gpio_num.h>
#include <stdint.h>
#include <vector>
#include "driver/uart.h"

class Rs485Receiver;

struct Rs485PortPins {
  gpio_num_t tx;
  gpio_num_t rx;
  // DE/~RE direction pin (active high), driven as RTS by the UART driver in
  // half-duplex mode; GPIO_NUM_NC for transceivers with automatic direction.
  gpio_num_t de;
};

// Defined here but only emitted into images whose board overrides
// configure_rs485_half_duplex(); moving it to a .cpp links the UART driver's
// pin and mode calls into every board instead.
inline bool rs485_configure_half_duplex(uart_port_t uart_num, const Rs485PortPins& pins) {
  return uart_set_pin(uart_num, pins.tx, pins.rx, pins.de, UART_PIN_NO_CHANGE) == ESP_OK &&
         uart_set_mode(uart_num, UART_MODE_RS485_HALF_DUPLEX) == ESP_OK;
}

class Rs485Port {
 public:
  Rs485Port(HardwareSerial& serial, uart_port_t uart_num, Rs485PortPins pins)
      : serial_(serial), uart_num_(uart_num), pins_(pins) {}

  uart_port_t uart_num() const { return uart_num_; }
  const Rs485PortPins& pins() const { return pins_; }

  // Claims the DE pin and parks it in receive state before any protocol
  // owns the port.
  void preinit();
  bool begin(const char* owner, uint32_t baud, uint32_t config = SERIAL_8N1);
  HardwareSerial& serial() { return serial_; }
  void register_receiver(Rs485Receiver* receiver);
  bool has_receivers() const { return !receivers_.empty(); }
  void poll();
  bool recently_received(uint32_t hold_ms) const;
  void mark_activity();

 private:
  HardwareSerial& serial_;
  uart_port_t uart_num_;
  Rs485PortPins pins_;
  std::vector<Rs485Receiver*> receivers_;
  bool de_available_ = false;
  uint32_t last_rx_ms_ = 0;
};

#endif
