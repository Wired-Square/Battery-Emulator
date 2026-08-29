#ifndef INVERTER_PROTOCOL_H
#define INVERTER_PROTOCOL_H

#include <cstdint>

#include "../devboard/webserver/settings_field.h"

enum class InverterProtocolType {
  None = 0,
  AforeCan = 1,
  BydCan = 2,
  BydModbus = 3,
  FerroampCan = 4,
  Foxess = 5,
  GrowattHv = 6,
  GrowattLv = 7,
  GrowattWit = 8,
  Kostal = 9,
  Pylon = 10,
  PylonLv = 11,
  Schneider = 12,
  SmaBydH = 14,
  SmaLv = 15,
  SmaBydHvs = 16,
  Sofar = 17,
  Solax = 18,
  Solxpow = 19,
  SolArkLv = 20,
  Sungrow = 21,
  VCU = 22,
  PylonLV485 = 23,
  SmaSBSByd = 24,
  Highest
};

using InverterCapabilities = uint16_t;

namespace InverterCapability {
inline constexpr InverterCapabilities PackGeometry = 1 << 0;
inline constexpr InverterCapabilities ModuleCount = 1 << 1;
inline constexpr InverterCapabilities ContactorWorkaround = 1 << 2;
}  // namespace InverterCapability

extern InverterProtocolType user_selected_inverter_protocol;

extern const char* name_for_inverter_type(InverterProtocolType type);

enum class InverterInterfaceType { Can, Rs485, Modbus };

// The abstract base class for all inverter protocols
class InverterProtocol {
 public:
  const char* name() const { return name_; }
  void set_name(const char* name) { name_ = name; }  // set by the registry factory
  virtual bool setup() { return true; }
  virtual const char* interface_name() = 0;
  virtual InverterInterfaceType interface_type() = 0;

  // This function maps all the values fetched from battery to the correct battery emulator data structures
  virtual void update_values() = 0;

  // If true, this inverter supports a signal to control contactor (allows_contactor_closing)
  virtual bool controls_contactor() { return false; }

  virtual bool allows_contactor_closing() { return false; }

  virtual bool supports_battery_id() { return false; }

  virtual bool provides_shunt() { return false; }
  virtual void enable_shunt() {}

  // Some inverters are slow to boot; suppress the CAN-missing fault during a startup grace window.
  virtual bool needs_can_startup_grace() { return false; }

 private:
  const char* name_ = nullptr;
};

extern InverterProtocol* inverter;

#endif
