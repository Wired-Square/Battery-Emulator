#include "comm_rs485.h"

#include "../../devboard/hal/hal.h"
#include "Rs485Port.h"

bool init_rs485() {
  for_each_unique_binding(esp32hal->interfaces(), &InterfaceDescriptor::rs485_port, [](Rs485Port* port) {
    port->preinit();
    return true;
  });
  return true;
}

void receive_rs485() {
  for_each_unique_binding(esp32hal->interfaces(), &InterfaceDescriptor::rs485_port, [](Rs485Port* port) {
    port->poll();
    return true;
  });
}

Rs485Port* resolve_rs485_port(const InterfaceDescriptor* selected) {
  if (selected != nullptr && selected->rs485_port != nullptr) {
    return selected->rs485_port;
  }
  const InterfaceDescriptor* fallback = find_by_type(esp32hal->interfaces(), InterfaceType::Rs485);
  return fallback != nullptr ? fallback->rs485_port : nullptr;
}
