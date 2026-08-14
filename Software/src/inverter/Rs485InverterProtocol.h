#ifndef _RS485INVERTER_PROTOCOL_H
#define _RS485INVERTER_PROTOCOL_H

#include "InverterProtocol.h"

#include "../communication/can/comm_can.h"
#include "../communication/rs485/Rs485Port.h"
#include "../communication/rs485/comm_rs485.h"

class Rs485InverterProtocol : public InverterProtocol, Rs485Receiver {
 public:
  virtual const char* interface_name() { return "RS485"; }
  InverterInterfaceType interface_type() { return InverterInterfaceType::Rs485; }
  virtual int baud_rate() = 0;

  Rs485InverterProtocol() : port_(resolve_rs485_port(can_config.inverter)) {
    if (port_ != nullptr) {
      port_->register_receiver(this);
    }
  }

 protected:
  Rs485Port* port_;
};

#endif
