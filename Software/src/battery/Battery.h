#ifndef BATTERY_H
#define BATTERY_H

#include "../../src/devboard/utils/types.h"
#include "../system_settings.h"
#include "battery_advanced_status.h"
#include "battery_command.h"

enum class BatteryType {
  None = 0,
  BmwI3 = 2,
  BmwIX = 3,
  BoltAmpera = 4,
  BydAtto3 = 5,
  CellPowerBms = 6,
  Chademo = 7,
  CmfaEv = 8,
  Foxess = 9,
  GeelyGeometryC = 10,
  OrionBms = 11,
  Sono = 12,
  StellantisEcmp = 13,
  ImievCZeroIon = 14,
  JaguarIpace = 15,
  KiaEGmp = 16,
  KiaHyundai64 = 17,
  KiaHyundaiHybrid = 18,
  Meb = 19,
  Mg5 = 20,
  NissanLeaf = 21,
  Pylon = 22,
  DalyBms = 23,
  RjxzsBms = 24,
  RangeRoverPhev = 25,
  RenaultKangoo = 26,
  RenaultTwizy = 27,
  RenaultZoe1 = 28,
  RenaultZoe2 = 29,
  SantaFePhev = 30,
  SimpBms = 31,
  TeslaModel3Y = 32,
  TeslaModelSX = 33,
  TestFake = 34,
  VolvoSpa = 35,
  VolvoSpaHybrid = 36,
  MgGen1 = 37,
  SamsungSdiLv = 38,
  HyundaiIoniq28 = 39,
  Kia64FD = 40,
  RelionBattery = 41,
  RivianBattery = 42,
  BmwPhev = 43,
  FordMachE = 44,
  CmpSmartCar = 45,
  ThinkCity = 47,
  TeslaLegacy = 48,
  GrowattHvArk = 49,
  GeelySea = 50,
  ThunderstruckBMS = 51,
  EnnoidBMS = 52,
  StellantisSmallWide4x4 = 53,
  ChargebyteCCSBattery = 54,
  VAGMqbEvo = 55,
  Highest
};

extern const char* name_for_battery_type(BatteryType type);
extern const char* name_for_chemistry(battery_chemistry_enum chem);

extern BatteryType user_selected_battery_types[kMaxBatterySlots];
BatteryType battery_type_for_slot(uint8_t slot);
bool battery_slot_occupied(uint8_t slot);
bool any_battery_slot_occupied();
bool battery_type_allowed_in_slot(BatteryType type, uint8_t slot);

extern battery_chemistry_enum user_selected_battery_chemistry;

// Abstract base class for next-generation battery implementations.
// Defines the interface to call battery specific functionality.
class Battery {
 public:
  virtual void setup(void) = 0;
  virtual void update_values() = 0;

  // The name of the comm interface the battery is using.
  virtual const char* interface_name() = 0;

  /* True for battery types where the SOC-based charge power taper is
     mandatory: the taper cannot be disabled and the start SOC is restricted
     to 50-85%. Enforced at boot and reflected in the settings UI. */
  virtual bool mandatory_charge_taper() { return false; }

  virtual void handle_precharge() {}

  // This allows for battery specific SOC plausibility calculations to be performed.
  virtual bool soc_plausible() { return true; }

  // Battery reports total_charged_battery_Wh and total_discharged_battery_Wh
  virtual bool supports_charged_energy() { return false; }

  // Battery reports insulation/isolation resistance via
  // datalayer status insulation_resistance_kOhm
  virtual bool supports_insulation_resistance() { return false; }

  // Structured advanced status; drivers override to emit data.
  virtual BatteryAdvancedStatus get_advanced_status() { return {}; }

  virtual const char* get_dtc_json_filename() { return ""; }

  // Commands this battery offers. Built once per driver; availability is
  // evaluated per request via BatteryCommand::available, because some gates are
  // learned from the bus after construction and others track live state.
  virtual const std::vector<BatteryCommand>& get_commands() { return kNoCommands; }

 protected:
  static const std::vector<BatteryCommand> kNoCommands;
};

#endif
