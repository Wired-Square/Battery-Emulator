#ifndef BATTERY_SLOT_CONTEXT_H
#define BATTERY_SLOT_CONTEXT_H

#include <stdint.h>
#include <soc/gpio_num.h>
#include "Battery.h"
#include "../datalayer/datalayer.h"
#include "../devboard/hal/interface_descriptor.h"

struct BatterySlotContext {
  uint8_t slot;
  DATALAYER_BATTERY_TYPE* datalayer;
  bool* contactor_flag;
  const InterfaceDescriptor* can_interface;
  gpio_num_t wakeup_pin;

  bool is_primary() const { return slot == 0; }
};

#endif
