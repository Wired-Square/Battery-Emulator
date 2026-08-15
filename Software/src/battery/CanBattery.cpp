#include "CanBattery.h"

CanBattery::CanBattery(CAN_Speed speed, CanSpeedMode mode) : CanBattery(can_config.battery, speed, mode) {}

CanBattery::CanBattery(const InterfaceDescriptor* interface, CAN_Speed speed, CanSpeedMode mode) {
  can_interface = interface;
  register_transmitter(this);
  register_can_receiver(this, can_interface, CanRole::Battery, speed, mode);
}
