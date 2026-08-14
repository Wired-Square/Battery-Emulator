#include "INVERTERS.h"

#include <iterator>
#include "AFORE-CAN.h"
#include "BYD-CAN.h"
#include "BYD-MODBUS.h"
#include "FERROAMP-CAN.h"
#include "FOXESS-CAN.h"
#include "GROWATT-HV-CAN.h"
#include "GROWATT-LV-CAN.h"
#include "GROWATT-WIT-CAN.h"
#include "KOSTAL-RS485.h"
#include "PYLON-CAN.h"
#include "PYLON-LV-CAN.h"
#include "PYLON-LV-RS485.h"
#include "SCHNEIDER-CAN.h"
#include "SMA-BYD-H-CAN.h"
#include "SMA-BYD-HVS-CAN.h"
#include "SMA-LV-CAN.h"
#include "SMA-SBS-BYD-CAN.h"
#include "SOFAR-CAN.h"
#include "SOL-ARK-LV-CAN.h"
#include "SOLAX-CAN.h"
#include "SOLXPOW-CAN.h"
#include "SUNGROW-CAN.h"
#include "VCU-CAN.h"

InverterProtocol* inverter = nullptr;

InverterProtocolType user_selected_inverter_protocol = InverterProtocolType::BydModbus;

// Some user-configurable settings that can be used by inverters. These
// inverters should use sensible defaults if the corresponding user_selected
// value is zero.
uint16_t user_selected_pylon_send = 0;
uint16_t user_selected_inverter_cells = 0;
uint16_t user_selected_inverter_modules = 0;
uint16_t user_selected_inverter_cells_per_module = 0;
uint16_t user_selected_inverter_voltage_level = 0;
uint16_t user_selected_inverter_ah_capacity = 0;
uint16_t user_selected_inverter_battery_type = 0;
uint16_t user_selected_inverter_sungrow_type = 0;
uint16_t user_selected_inverter_foxess_type = 0;
uint16_t user_selected_inverter_foxess_subtype = 0;
uint16_t user_selected_inverter_foxess_modules = 0;
uint16_t user_selected_inverter_pylon_type = 0;
inverter_contactor_mode_enum user_selected_inverter_contactor_mode = inverter_contactor_mode_enum::NoWorkaround;
bool user_selected_inverter_long_CAN_timeout = false;
bool user_selected_pylon_30koffset = false;
bool user_selected_pylon_invert_byteorder = false;
bool user_selected_inverter_deye_workaround = false;
bool user_selected_primo_gen24 =
    false;  //Used by BYD-Modbus (Fronius Primo Gen24) inverters to determine if we should cap voltage to 450V or not

bool inverter_low_pass_filter = false;  //Should the charge/discharge limits be filtered with a low pass filter?

bool charge_taper_soc = false;  //Should the charge power limit be tapered based on scaled SOC near full?
uint16_t charge_taper_band_pptt =
    500;  //Taper band in pptt. 500 = taper starts at 95.00% scaled SOC, reaching 0W at 100.00%
uint16_t charge_taper_floor_W =
    0;  //Minimum charge power in W held during tapering until 100.00% scaled SOC. 0 = disabled, taper goes linearly to 0W

template <typename T>
static InverterProtocol* make(const char* name) {
  auto* p = new T();
  p->set_name(name);
  return p;
}

struct InverterTypeInfo {
  InverterProtocolType id;
  const char* name;
  InverterProtocol* (*make)(const char*);  // nullptr => not constructible (None)
};

// A row's make<T> must construct the class its id and name denote — the pairing is convention, not compiler-checked.
static constexpr InverterTypeInfo kInverterRegistry[] = {
    {InverterProtocolType::None,        "None",                                       nullptr},
    {InverterProtocolType::AforeCan,    "Afore battery over CAN",                     &make<AforeCanInverter>},
    {InverterProtocolType::BydCan,      "BYD Battery-Box Premium HVS over CAN Bus",   &make<BydCanInverter>},
    {InverterProtocolType::BydModbus,   "BYD 11kWh HVM battery over Modbus RTU",      &make<BydModbusInverter>},
    {InverterProtocolType::FerroampCan, "Ferroamp Pylon battery over CAN bus",        &make<FerroampCanInverter>},
    {InverterProtocolType::Foxess,      "FoxESS compatible HV2600/ECS4100 battery",   &make<FoxessCanInverter>},
    {InverterProtocolType::GrowattHv,   "Growatt High Voltage protocol via CAN",      &make<GrowattHvInverter>},
    {InverterProtocolType::GrowattLv,   "Growatt Low Voltage (48V) protocol via CAN", &make<GrowattLvInverter>},
    {InverterProtocolType::GrowattWit,  "Growatt WIT compatible battery via CAN",     &make<GrowattWitInverter>},
    {InverterProtocolType::Kostal,      "BYD battery via Kostal RS485",               &make<KostalInverterProtocol>},
    {InverterProtocolType::Pylon,       "Pylontech HV battery over CAN bus",          &make<PylonInverter>},
    {InverterProtocolType::PylonLv,     "Pylontech LV battery over CAN bus",          &make<PylonLvInverter>},
    {InverterProtocolType::Schneider,   "Schneider V2 SE BMS CAN",                    &make<SchneiderInverter>},
    // id 13 is an NVM-frozen gap: no row, so lookups return nullptr
    {InverterProtocolType::SmaBydH,     "SMA compatible BYD Battery-Box H",           &make<SmaBydHInverter>},
    {InverterProtocolType::SmaLv,       "SMA Low Voltage (48V) protocol via CAN",     &make<SmaLvInverter>},
    {InverterProtocolType::SmaBydHvs,   "SMA compatible BYD Battery-Box HVS",         &make<SmaBydHvsInverter>},
    {InverterProtocolType::Sofar,       "Sofar BMS (Extended) via CAN, Battery ID",   &make<SofarInverter>},
    {InverterProtocolType::Solax,       "SolaX Triple Power LFP over CAN bus",        &make<SolaxInverter>},
    {InverterProtocolType::Solxpow,     "Solxpow compatible battery",                 &make<SolxpowInverter>},
    {InverterProtocolType::SolArkLv,    "Sol-Ark LV protocol over CAN bus",           &make<SolArkLvInverter>},
    {InverterProtocolType::Sungrow,     "Sungrow SBRXXX emulation over CAN bus",      &make<SungrowInverter>},
    {InverterProtocolType::VCU,         "VCU mode: Nissan LEAF battery",              &make<VCUInverter>},
    {InverterProtocolType::PylonLV485,  "Pylon low voltage via RS485",                &make<PylonLV485InverterProtocol>},
    {InverterProtocolType::SmaSBSByd,   "SMA SBS compatible BYD Battery-Box HVS",     &make<SmaSBSBydHvsInverter>},
    // Highest is a count sentinel, not a selectable type
};

static constexpr bool registry_strictly_ascending() {
  for (size_t i = 1; i < std::size(kInverterRegistry); i++)
    if (kInverterRegistry[i - 1].id >= kInverterRegistry[i].id) return false;
  return true;
}
static_assert(registry_strictly_ascending(),
              "inverter registry rows must be sorted by enum value with no duplicates");

static const InverterTypeInfo* find_inverter_info(InverterProtocolType type) {
  for (const auto& info : kInverterRegistry)
    if (info.id == type) return &info;
  return nullptr;
}

extern const char* name_for_inverter_type(InverterProtocolType type) {
  const InverterTypeInfo* info = find_inverter_info(type);
  return info ? info->name : nullptr;
}

bool setup_inverter() {
  if (inverter) return true;
  const InverterTypeInfo* info = find_inverter_info(user_selected_inverter_protocol);
  if (!info) return false;         // gap 13 / Highest / unknown
  if (!info->make) return true;    // None: selected, nothing to construct
  inverter = info->make(info->name);
  return inverter->setup();
}
