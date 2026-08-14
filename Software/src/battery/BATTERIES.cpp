#include "BATTERIES.h"
#include <iterator>     // std::size
#include <type_traits>  // std::is_constructible_v
#include "../datalayer/datalayer_extended.h"
#include "../devboard/hal/hal.h"
#include "../devboard/utils/events.h"
#include "../devboard/utils/logging.h"
#include "CanBattery.h"
#include "RS485Battery.h"

#include "BMW-I3-BATTERY.h"
#include "BMW-IX-BATTERY.h"
#include "BMW-PHEV-BATTERY.h"
#include "BMW-SBOX.h"
#include "BOLT-AMPERA-BATTERY.h"
#include "BYD-ATTO-3-BATTERY.h"
#include "CELLPOWER-BMS.h"
#include "CHADEMO-BATTERY.h"
#include "CHADEMO-CT.h"
#include "CHADEMO-SHUNTS.h"
#include "CHARGEBYTE-CCS.h"
#include "CMFA-EV-BATTERY.h"
#include "CMP-SMART-CAR-BATTERY.h"
#include "DALY-BMS.h"
#include "ECMP-BATTERY.h"
#include "ENNOID-BMS.h"
#include "FORD-MACH-E-BATTERY.h"
#include "FOXESS-BATTERY.h"
#include "GEELY-GEOMETRY-C-BATTERY.h"
#include "GEELY-SEA-BATTERY.h"
#include "GROWATT-HV-ARK-BATTERY.h"
#include "HYUNDAI-IONIQ-28-BATTERY.h"
#include "IMIEV-CZERO-ION-BATTERY.h"
#include "JAGUAR-IPACE-BATTERY.h"
#include "KIA-64FD-BATTERY.h"
#include "KIA-E-GMP-BATTERY.h"
#include "KIA-HYUNDAI-64-BATTERY.h"
#include "KIA-HYUNDAI-HYBRID-BATTERY.h"
#include "MEB-BATTERY.h"
#include "MG-5-BATTERY.h"
#include "MG-GEN1-BATTERY.h"
#include "NISSAN-LEAF-BATTERY.h"
#include "ORION-BMS.h"
#include "PYLON-BATTERY.h"
#include "RANGE-ROVER-PHEV-BATTERY.h"
#include "RELION-LV-BATTERY.h"
#include "RENAULT-KANGOO-BATTERY.h"
#include "RENAULT-TWIZY.h"
#include "RENAULT-ZOE-GEN1-BATTERY.h"
#include "RENAULT-ZOE-GEN2-BATTERY.h"
#include "RIVIAN-BATTERY.h"
#include "RJXZS-BMS.h"
#include "SAMSUNG-SDI-LV-BATTERY.h"
#include "SANTA-FE-PHEV-BATTERY.h"
#include "SIMPBMS-BATTERY.h"
#include "SONO-BATTERY.h"
#include "STELLANTIS-SMALL-WIDE-4x4.h"
#include "TESLA-BATTERY.h"
#include "TESLA-LEGACY-BATTERY.h"
#include "TEST-FAKE-BATTERY.h"
#include "THINK-BATTERY.h"
#include "THUNDERSTRUCK-BMS.h"
#include "VOLVO-SPA-BATTERY.h"
#include "VOLVO-SPA-HYBRID-BATTERY.h"

Battery* batteries[kMaxBatterySlots] = {};

const char* name_for_chemistry(battery_chemistry_enum chem) {
  switch (chem) {
    case battery_chemistry_enum::Autodetect:
      return "Autodetect";
    case battery_chemistry_enum::LFP:
      return "LFP";
    case battery_chemistry_enum::NCA:
      return "NCA";
    case battery_chemistry_enum::NMC:
      return "NMC";
    case battery_chemistry_enum::ZEBRA:
      return "Molten Salt";
    default:
      return nullptr;
  }
}

static constexpr uint8_t kSingleSlot = 1;
static constexpr uint8_t kDualSlot = 2;
// triple-capable rows track kMaxBatterySlots — no local kTripleSlot

template <typename T>
static Battery* make(const BatterySlotContext& ctx) {
  if constexpr (std::is_constructible_v<T, const BatterySlotContext&>) {
    return new T(ctx);
  } else {
    return new T();
  }
}

struct BatteryTypeInfo {
  BatteryType id;
  const char* name;
  uint8_t max_slots;
  BusRequirement bus;
  Battery* (*make)(const BatterySlotContext&);  // null for None (no driver class)
};

static constexpr BatteryTypeInfo kBatteryRegistry[] = {
    {BatteryType::None, "None", kSingleSlot, BusRequirement::None, nullptr},
    {BatteryType::BmwI3, "BMW i3", kDualSlot, BusRequirement::Can, &make<BmwI3Battery>},
    {BatteryType::BmwIX, "BMW iX and i4-7 platform", kSingleSlot, BusRequirement::CanFd, &make<BmwIXBattery>},
    {BatteryType::BoltAmpera, "Chevrolet Bolt EV/Opel Ampera-e", kDualSlot, BusRequirement::Can,
     &make<BoltAmperaBattery>},
    {BatteryType::BydAtto3, "BYD Atto 3/Seal/Dolphin", kDualSlot, BusRequirement::Can, &make<BydAttoBattery>},
    {BatteryType::CellPowerBms, "Cellpower BMS", kSingleSlot, BusRequirement::Can, &make<CellPowerBms>},
    {BatteryType::Chademo, "Chademo V2X mode", kSingleSlot, BusRequirement::Can, &make<ChademoBattery>},
    {BatteryType::CmfaEv, "CMFA platform, 27 kWh battery", kDualSlot, BusRequirement::Can, &make<CmfaEvBattery>},
    {BatteryType::Foxess, "FoxESS HV2600/ECS4100 OEM battery", kSingleSlot, BusRequirement::Can, &make<FoxessBattery>},
    {BatteryType::GeelyGeometryC, "Geely Geometry C", kSingleSlot, BusRequirement::Can, &make<GeelyGeometryCBattery>},
    {BatteryType::OrionBms, "DIY battery with Orion BMS (Victron setting)", kSingleSlot, BusRequirement::Can,
     &make<OrionBms>},
    {BatteryType::Sono, "Sono Motors Sion 64kWh LFP ", kSingleSlot, BusRequirement::Can, &make<SonoBattery>},
    {BatteryType::StellantisEcmp, "Stellantis ECMP battery", kMaxBatterySlots, BusRequirement::Can, &make<EcmpBattery>},
    {BatteryType::ImievCZeroIon, "I-Miev / C-Zero / Ion Triplet", kSingleSlot, BusRequirement::Can,
     &make<ImievCZeroIonBattery>},
    {BatteryType::JaguarIpace, "Jaguar I-PACE", kSingleSlot, BusRequirement::Can, &make<JaguarIpaceBattery>},
    {BatteryType::KiaEGmp, "Kia/Hyundai EGMP platform", kSingleSlot, BusRequirement::CanFd, &make<KiaEGmpBattery>},
    {BatteryType::KiaHyundai64, "Kia/Hyundai 64/40kWh battery", kDualSlot, BusRequirement::Can,
     &make<KiaHyundai64Battery>},
    {BatteryType::KiaHyundaiHybrid, "Kia/Hyundai Hybrid", kSingleSlot, BusRequirement::Can,
     &make<KiaHyundaiHybridBattery>},
    {BatteryType::Meb, "Volkswagen Group MEB platform via CAN-FD", kSingleSlot, BusRequirement::CanFd,
     &make<MebBattery>},
#ifndef SMALL_FLASH_DEVICE
    {BatteryType::Mg5, "MG 5 battery", kSingleSlot, BusRequirement::Can, &make<Mg5Battery>},
#endif
    {BatteryType::NissanLeaf, "Nissan LEAF battery", kMaxBatterySlots, BusRequirement::Can, &make<NissanLeafBattery>},
    {BatteryType::Pylon, "Pylon / Dyness compatible battery", kDualSlot, BusRequirement::Can, &make<PylonBattery>},
    {BatteryType::DalyBms, "DALY RS485", kSingleSlot, BusRequirement::Rs485, &make<DalyBms>},
    {BatteryType::RjxzsBms, "RJXZS BMS, DIY battery", kSingleSlot, BusRequirement::Can, &make<RjxzsBms>},
    {BatteryType::RangeRoverPhev, "Range Rover 13kWh PHEV battery (L494/L405)", kSingleSlot, BusRequirement::Can,
     &make<RangeRoverPhevBattery>},
    {BatteryType::RenaultKangoo, "Renault Kangoo", kSingleSlot, BusRequirement::Can, &make<RenaultKangooBattery>},
    {BatteryType::RenaultTwizy, "Renault Twizy", kSingleSlot, BusRequirement::Can, &make<RenaultTwizyBattery>},
    {BatteryType::RenaultZoe1, "Renault Zoe Gen1 22/40kWh", kDualSlot, BusRequirement::Can,
     &make<RenaultZoeGen1Battery>},
    {BatteryType::RenaultZoe2, "Renault Zoe Gen2 50kWh", kDualSlot, BusRequirement::Can, &make<RenaultZoeGen2Battery>},
    {BatteryType::SantaFePhev, "Santa Fe PHEV", kDualSlot, BusRequirement::Can, &make<SantaFePhevBattery>},
    {BatteryType::SimpBms, "SIMPBMS battery", kSingleSlot, BusRequirement::Can, &make<SimpBmsBattery>},
    {BatteryType::TeslaModel3Y, "Tesla Model 3/Y", kDualSlot, BusRequirement::Can, &make<TeslaBattery>},
    {BatteryType::TeslaModelSX, "Tesla Model S/X", kDualSlot, BusRequirement::Can, &make<TeslaBattery>},
    {BatteryType::TestFake, "Fake battery for testing purposes", kDualSlot, BusRequirement::Can,
     &make<TestFakeBattery>},
    {BatteryType::VolvoSpa, "Volvo / Polestar 69/78kWh SPA battery", kSingleSlot, BusRequirement::Can,
     &make<VolvoSpaBattery>},
    {BatteryType::VolvoSpaHybrid, "Volvo PHEV battery", kSingleSlot, BusRequirement::Can, &make<VolvoSpaHybridBattery>},
    {BatteryType::MgGen1, MgGen1Battery::Name, kDualSlot, BusRequirement::Can, &make<MgGen1Battery>},
    {BatteryType::SamsungSdiLv, "Samsung SDI LV Battery", kSingleSlot, BusRequirement::Can, &make<SamsungSdiLVBattery>},
    {BatteryType::HyundaiIoniq28, "Hyundai Ioniq Electric 28kWh", kSingleSlot, BusRequirement::Can,
     &make<HyundaiIoniq28Battery>},
    {BatteryType::Kia64FD, "Kia 64kWh FD battery", kSingleSlot, BusRequirement::CanFd, &make<Kia64FDBattery>},
    {BatteryType::RelionBattery, "Relion LV protocol via 250kbps CAN", kMaxBatterySlots, BusRequirement::Can,
     &make<RelionBattery>},
    {BatteryType::RivianBattery, "Rivian R1T large 135kWh battery", kSingleSlot, BusRequirement::Can,
     &make<RivianBattery>},
    {BatteryType::BmwPhev, "BMW PHEV Battery", kSingleSlot, BusRequirement::Can, &make<BmwPhevBattery>},
    {BatteryType::FordMachE, "Ford Mustang Mach-E battery", kSingleSlot, BusRequirement::Can, &make<FordMachEBattery>},
    {BatteryType::CmpSmartCar, "Stellantis CMP Smart Car Battery", kDualSlot, BusRequirement::Can,
     &make<CmpSmartCarBattery>},
    {BatteryType::ThinkCity, "Think City", kSingleSlot, BusRequirement::Can, &make<ThinkBattery>},
    {BatteryType::TeslaLegacy, "Tesla Model S/X 2012-2020", kSingleSlot, BusRequirement::Can,
     &make<TeslaLegacyBattery>},
    {BatteryType::GrowattHvArk, "Growatt HV ARK battery (battery-facing CAN)", kSingleSlot, BusRequirement::Can,
     &make<GrowattHvArkBattery>},
    {BatteryType::GeelySea, "Volvo/Zeekr/Geely SEA battery", kSingleSlot, BusRequirement::Can, &make<GeelySeaBattery>},
    {BatteryType::ThunderstruckBMS, "Thunderstruck BMS", kSingleSlot, BusRequirement::Can, &make<ThunderstruckBMS>},
    {BatteryType::EnnoidBMS, "ENNOID BMS via VESC, DIY battery", kSingleSlot, BusRequirement::Can, &make<EnnoidBms>},
    {BatteryType::StellantisSmallWide4x4, "Stellantis FCA Small Wide 4x4", kSingleSlot, BusRequirement::Can,
     &make<StellantisSmallWide4x4Battery>},
#ifndef SMALL_FLASH_DEVICE
    {BatteryType::ChargebyteCCSBattery, ChargebyteCCSBattery::Name, kSingleSlot, BusRequirement::Can,
     &make<ChargebyteCCSBattery>},
#endif
    {BatteryType::VAGMqbEvo, MqbEvoBattery::Name, kSingleSlot, BusRequirement::CanFd, &make<MqbEvoBattery>},
};

static constexpr bool registry_strictly_ascending() {
  for (size_t i = 1; i < std::size(kBatteryRegistry); i++) {
    if (kBatteryRegistry[i - 1].id >= kBatteryRegistry[i].id) {
      return false;
    }
  }
  return true;
}
static_assert(registry_strictly_ascending(), "battery registry rows must be sorted by enum value with no duplicates");

static const BatteryTypeInfo* find_battery_info(BatteryType type) {
  for (const auto& info : kBatteryRegistry) {
    if (info.id == type) {
      return &info;
    }
  }
  return nullptr;
}

const char* name_for_battery_type(BatteryType type) {
  const BatteryTypeInfo* info = find_battery_info(type);
  return info ? info->name : nullptr;
}

static BusRequirement bus_for_battery_type(BatteryType type) {
  const BatteryTypeInfo* info = find_battery_info(type);
  return info ? info->bus : BusRequirement::None;
}

bool board_supports_battery_type(BatteryType type) {
  if (esp32hal == nullptr) {
    return true;
  }
  return find_satisfying(esp32hal->interfaces(), bus_for_battery_type(type)) != nullptr;
}

bool interface_supports_battery_type(BatteryType type, const InterfaceDescriptor* interface) {
  return interface == nullptr || interface_satisfies(bus_for_battery_type(type), interface->type);
}

const battery_chemistry_enum battery_chemistry_default = battery_chemistry_enum::NMC;

battery_chemistry_enum user_selected_battery_chemistry = battery_chemistry_default;

BatteryType user_selected_battery_type = BatteryType::None;
bool user_selected_second_battery = false;
bool user_selected_triple_battery = false;

uint8_t active_battery_slots() {
  // Highest enabled slot + 1: the third battery is not gated on the second
  // (see setup_battery), so counting enabled flags would skip a live slot 2.
  if (user_selected_triple_battery) {
    return 3;
  }
  return user_selected_second_battery ? 2 : 1;
}

BatterySlotContext battery_slot_context(uint8_t slot) {
  const InterfaceDescriptor* iface = can_config.battery;
  if (slot == 1) {
    iface = can_config.battery_double;
  } else if (slot >= 2) {
    iface = can_config.battery_triple;
  }
  return BatterySlotContext{
      .slot = slot,
      .datalayer = &datalayer.battery_slot(slot),
      .contactor_flag = &datalayer.system.status.slot_allows_contactor_closing(slot),
      .can_interface = iface,
      .wakeup_pin = esp32hal->wakeup_pin(slot),
  };
}

Battery* create_battery(BatteryType type, const BatterySlotContext& ctx) {
  const BatteryTypeInfo* info = find_battery_info(type);
  return (info && info->make && ctx.slot < info->max_slots) ? info->make(ctx) : nullptr;
}

void setup_battery() {
  if (batteries[0]) {
    return;
  }

  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    datalayer.battery_slot(slot).info.chemistry = user_selected_battery_chemistry;
  }

  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    bool slot_enabled =
        (slot == 0) || (slot == 1 && user_selected_second_battery) || (slot == 2 && user_selected_triple_battery);
    if (!slot_enabled || batteries[slot]) {
      continue;
    }

    const BatterySlotContext ctx = battery_slot_context(slot);
    if (!interface_supports_battery_type(user_selected_battery_type, ctx.can_interface)) {
      DEBUG_PRINTF("Battery %u type %s needs an interface this one cannot provide, not starting it!\n", slot + 1,
                   name_for_battery_type(user_selected_battery_type));
      set_event(EVENT_BATTERY_INTERFACE_UNSUPPORTED, slot + 1);
      continue;
    }
    // Frames broadcast to every receiver on a bus and all slots share one
    // battery type, so a duplicate interface would decode one pack twice.
    bool interface_in_use = false;
    for (uint8_t lower = 0; lower < slot; lower++) {
      if (batteries[lower] && ctx.can_interface != nullptr &&
          battery_slot_context(lower).can_interface == ctx.can_interface) {
        interface_in_use = true;
      }
    }
    if (interface_in_use) {
      DEBUG_PRINTF("Battery %u shares its CAN interface with a lower slot, not starting it!\n", slot + 1);
      set_event(EVENT_BATTERY_INTERFACE_CONFLICT, slot + 1);
      continue;
    }

    Battery* created = create_battery(user_selected_battery_type, ctx);
    if (!created) {
      if (slot > 0) {
        DEBUG_PRINTF("User tried enabling %s battery on non-supported integration!\n", slot == 1 ? "double" : "triple");
        set_event(EVENT_BATTERY_SLOT_UNSUPPORTED, slot + 1);
      }
      continue;
    }

    batteries[slot] = created;
    created->setup();
    strncpy(datalayer.system.info.battery_protocol, name_for_battery_type(user_selected_battery_type),
            sizeof(datalayer.system.info.battery_protocol) - 1);
    datalayer.system.info.battery_protocol[sizeof(datalayer.system.info.battery_protocol) - 1] = '\0';
  }
}

/* User-selected Nissan LEAF settings */
bool user_selected_LEAF_interlock_mandatory = false;
/* User-selected Tesla settings */
bool user_selected_tesla_digital_HVIL = false;
uint16_t user_selected_tesla_GTW_country = kTeslaGtwCountryDefault;
bool user_selected_tesla_GTW_rightHandDrive = kTeslaGtwRightHandDriveDefault;
uint16_t user_selected_tesla_GTW_mapRegion = kTeslaGtwMapRegionDefault;
uint16_t user_selected_tesla_GTW_chassisType = kTeslaGtwChassisTypeDefault;
uint16_t user_selected_tesla_GTW_packEnergy = kTeslaGtwPackEnergyDefault;
/* User-selected DALY BMS settings */
int user_selected_daly_power_per_percent = 50;
int user_selected_daly_power_per_dV = 50;
int user_selected_daly_power_per_dV_start = 20;
int user_selected_daly_power_per_degree_C = 60;
int user_selected_daly_power_at_0_degree_C = 800;
/* User-selected EGMP+others settings */
bool user_selected_use_estimated_SOC = false;
bool user_selected_use_estimated_charge_limits = false;
uint16_t user_selected_pylon_baudrate = 500;
/* For custom BMS which need rampdown. SOC% to start ramping down from max charge power towards 0 at 100.00%*/
uint16_t user_set_rampdown_SOC = 9000;  //9000 = 90.00%
// Use 0V for user selected cell/pack voltage defaults (On boot will be replaced with saved values from NVM)
uint16_t user_selected_max_pack_voltage_dV = 0;
uint16_t user_selected_min_pack_voltage_dV = 0;
uint16_t user_selected_max_cell_voltage_mV = 0;
uint16_t user_selected_min_cell_voltage_mV = 0;
