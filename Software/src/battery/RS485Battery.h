#ifndef RS485_BATTERY_H
#define RS485_BATTERY_H

#include "Battery.h"

#include "../communication/Transmitter.h"
#include "../communication/can/comm_can.h"
#include "../communication/rs485/Rs485Port.h"
#include "../communication/rs485/comm_rs485.h"
#include "../devboard/utils/types.h"

// Abstract base class for batteries using the RS485 interface
class RS485Battery : public Battery, Transmitter, Rs485Receiver {
 public:
  virtual void transmit_rs485(unsigned long currentMillis) = 0;

  const char* interface_name() { return "RS485"; }

  void transmit(unsigned long currentMillis) { transmit_rs485(currentMillis); }

  RS485Battery(const InterfaceDescriptor* interface) : port_(resolve_rs485_port(interface)) {
    register_transmitter(this);
    if (port_ != nullptr) {
      port_->register_receiver(this);
    }
  }

 protected:
  Rs485Port* port_;
};

#endif
