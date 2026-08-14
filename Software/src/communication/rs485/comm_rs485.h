#ifndef _COMM_RS485_H_
#define _COMM_RS485_H_

#include <Arduino.h>
#include <HardwareSerial.h>
#include <stdint.h>

class Rs485Port;
struct InterfaceDescriptor;

// Parks every board RS-485 port's DE pin in receive state.
// Safe to call even if rs485 is not used.
bool init_rs485();

// Interface for any object that needs to receive a signal to handle RS485
// comm.
class Rs485Receiver {
 public:
  virtual void receive() = 0;
};

// Polls every board RS-485 port that has receivers.
void receive_rs485();

// The port behind a role's selected interface. Selections without a port
// (CAN descriptors) fall back to the board's first RS-485 port, matching the
// pre-descriptor behaviour where every RS-485 protocol shared one UART.
Rs485Port* resolve_rs485_port(const InterfaceDescriptor* selected);

#endif
