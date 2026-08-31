#include "settings_api.h"
#include "web_ui_selection.h"

#include "../../battery/BATTERIES.h"
#include "../../battery/Battery.h"
#include "../../battery/Shunt.h"
#include "../../charger/CHARGERS.h"
#include "../../charger/CanCharger.h"
#include "../../communication/equipmentstopbutton/comm_equipmentstopbutton.h"
#include "../../communication/nvm/comm_nvm.h"
#include "../../datalayer/datalayer.h"
#include "../../datalayer/datalayer_extended.h"
#include "../../inverter/INVERTERS.h"
#include "../hal/hal.h"
#include "../utils/types.h"
#include "../wifi/wifi.h"
#include "battery_slot_api.h"

#include <cmath>
#include <cstring>
#include <map>
#include <type_traits>
#include <vector>

// Edit-card fields, internal keys (IFSCHEMA/EQUIPMENT_STOP) and the form-only
// HTTPPASSCONFIRM are deliberately absent: they are not /saveSettings scalars.

extern uint16_t user_selected_CAN_ID_cutoff_filter;

namespace {
using ST = SettingType;
using SA = SettingApplies;
using SS = SettingStorage;
using SR = SettingRam;
using SCP = SettingScope;

template <typename E>
constexpr auto to_underlying(E e) noexcept {
  return static_cast<std::underlying_type_t<E>>(e);
}

template <typename EnumType>
std::vector<EnumType> enum_values() {
  static_assert(std::is_enum_v<EnumType>, "Template argument must be an enum type.");

  constexpr auto count = to_underlying(EnumType::Highest);
  std::vector<EnumType> values;
  for (int i = 1; i < count; ++i) {
    values.push_back(static_cast<EnumType>(i));
  }
  return values;
}

#ifdef HW_LILYGO2CAN
const std::map<int, String> led_modes = {{0, "Classic"},     {1, "Energy Flow"},     {2, "Heartbeat"},
                                         {3, "GRB Classic"}, {4, "GRB Energy Flow"}, {5, "GRB Heartbeat"}};
#else
const std::map<int, String> led_modes = {{0, "Classic"}, {1, "Energy Flow"}, {2, "Heartbeat"}};
#endif

const std::map<int, String> bms_reset_intervals = {{24, "24h"}, {48, "48h"}};

const char* name_for_button_type(STOP_BUTTON_BEHAVIOR behavior) {
  switch (behavior) {
    case STOP_BUTTON_BEHAVIOR::LATCHING_SWITCH:
      return "Latching";
    case STOP_BUTTON_BEHAVIOR::MOMENTARY_SWITCH:
      return "Momentary";
    case STOP_BUTTON_BEHAVIOR::NOT_CONNECTED:
      return "Not connected";
    default:
      return nullptr;
  }
}

constexpr double kDeciUnitsPerUnit = 10.0;
constexpr uint32_t kMillisecondsPerSecond = 1000;
constexpr uint32_t kPercentMax = 100;
constexpr int32_t kPptPerPercent = 100;
constexpr int32_t kDeciPerUnit = 10;
constexpr int32_t kMsPerMinute = 60000;

void seed_slot_capacities(uint8_t, double) {
  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    datalayer.battery_slot(slot).info.total_capacity_Wh = datalayer.battery.settings.user_set_total_capacity_Wh;
  }
}

const char* check_charger_field(const SettingField& field, uint8_t, const ValueSource& fields) {
  if (charger == nullptr) {
    return "No charger configured";
  }
  if (std::strcmp(field.key, "setpoint_v") == 0) {
    const float volts = static_cast<float>(fields.value(field.key).as_number());
    if (volts < CHARGER_MIN_HV || volts > CHARGER_MAX_HV) {
      return "Invalid value";
    }
  } else if (std::strcmp(field.key, "setpoint_a") == 0) {
    const float amps = static_cast<float>(fields.value(field.key).as_number());
    const DocumentValue requested_volts = fields.value("setpoint_v");
    const float volts = requested_volts.missing() ? datalayer.charger.charger_setpoint_HV_VDC
                                                  : static_cast<float>(requested_volts.as_number());
    if (amps < 0 || amps > CHARGER_MAX_A || amps * kDeciPerUnit > datalayer.battery.settings.max_user_set_charge_dA ||
        amps * volts > CHARGER_MAX_POWER) {
      return "Invalid value";
    }
  }
  return nullptr;
}

const char* check_balancing_field(const SettingField& field, uint8_t slot, const ValueSource& fields) {
  return validate_balancing_field(datalayer.battery_slot(slot).info.chemistry, field.key, fields.value(field.key));
}

const char* check_field(const SettingField& field, uint8_t slot, const ValueSource& fields) {
  if (field.section == kSecCharger) {
    return check_charger_field(field, slot, fields);
  }
  if (field.section == kSecBalancing) {
    return check_balancing_field(field, slot, fields);
  }
  return nullptr;
}

struct BatterySlotKeys {
  uint8_t slot;
  const char* type_key;
  const char* comm_key;
  const char* contactor_key;
};
constexpr const char* kBatteriesPath = "dynamic.batteries";
constexpr const char* kBalancingPath = "dynamic.balancing";
constexpr const char* kDynamicPrefix = "dynamic.";

// Names both the schema row's "scope" and the dynamic section carrying its
// entries. Global has no section, so it must answer null, not "".
const char* scope_name(SettingScope scope) {
  switch (scope) {
    case SCP::BatterySlot:
      return "battery";
    case SCP::Interface:
      return "interface";
    case SCP::LoadSwitchChannel:
      return "loadswitchchannel";
    case SCP::Global:
    case SCP::Highest:
      break;
  }
  return nullptr;
}

// Reproduces the keys comm_nvm wrote by hand: TERMIF0, LSROLE0, LSDUTY0, LSDIV0.
String scoped_key(const SettingField& field, uint8_t index) {
  return field.live.scope == SCP::Global ? String(field.key)
                                         : String(field.key) + String(static_cast<unsigned>(index));
}

bool in_bounds(const SettingField& field, double value) {
  return (field.min_value == kNoMin || value >= field.min_value) &&
         (field.max_value == kNoMax || value <= field.max_value);
}

constexpr BatterySlotKeys kBatterySlotKeys[kMaxBatterySlots] = {
    {0, "BATTTYPE", "BATTCOMM", "CNTCTRL"},
    {1, "BATT2TYPE", "BATT2COMM", "CNTCTRLDBL"},
    {2, "BATT3TYPE", "BATT3COMM", "CNTCTRLTRI"},
};
}  // namespace

constexpr SettingField kSettingFields[] = {
    setting("WEBAUTH", ST::Bool, kWebauth, SA::Boot),
    setting("HTTPUSER", ST::StringVal, kWebauth, SA::Boot).with_text("admin"),
    setting("HTTPPASS", ST::StringVal, kWebauth, SA::Boot).with_text(""),

    setting("WEBUI", ST::StringVal, kInterface, SA::Live).with_text(kDefaultUiShell).with_options("webui"),

    setting("SSID", ST::StringVal, kNetwork, SA::Boot).with_text(""),
    setting("PASSWORD", ST::StringVal, kNetwork, SA::Boot).with_text(""),

    setting("BATTCHEM", ST::EnumUint, kBattery, SA::Boot, 1).with_options("chemistry"),

    setting("INVTYPE", ST::EnumUint, kInverter, SA::Boot).with_options("inverter"),
    setting("INVCOMM", ST::InterfacePacked, kInverter, SA::Boot),
    setting("INVOFFGRID", ST::Bool, kInverter, SA::Boot),
    setting("LOWPASSFILTER", ST::Bool, kInverter, SA::Boot),
    setting("CHGTAPERSOC", ST::Bool, kInverter, SA::Boot),
    // Stored as start SOC in whole percent; comm_nvm derives the pptt band from it.
    setting("CHGTAPERSTART", ST::Uint, kInverter, SA::Boot, 95).with_range(50, 99),
    setting("CHGTAPERFLOOR", ST::Uint, kInverter, SA::Boot).with_range(0, 2000),
    setting("SLOWCANINV", ST::Bool, kInverter, SA::Boot),

    // CTOFFSET is a string to keep its sign; CTATTEN uses the boot default 3, not the
    // form's 0, so an unsaved device's full-set POST leaves the attenuation unchanged.
    setting("CHGTYPE", ST::EnumUint, kOptional, SA::Boot).with_options("charger"),
    setting("CHGCOMM", ST::InterfacePacked, kOptional, SA::Boot),
    setting("SHUNTTYPE", ST::EnumUint, kOptional, SA::Boot).with_options("shunt"),
    setting("SHUNTCOMM", ST::InterfacePacked, kOptional, SA::Boot),
    setting("CTOFFSET", ST::FloatString, kOptional, SA::Boot).with_text("-1.0"),
    setting("CTVNOM", ST::Uint, kOptional, SA::Boot, 40).with_range(0, 500),
    setting("CTANOM", ST::Uint, kOptional, SA::Boot, 100).with_range(0, 200),
    setting("CTATTEN", ST::EnumUint, kOptional, SA::Boot, 3).with_options("attenuation"),
    setting("CTINVERT", ST::Bool, kOptional, SA::Boot),

    // Board-gated rows sit inside the same #ifdef that gates them in comm_nvm.cpp.
    setting("CANFDASCAN", ST::Bool, kHardware, SA::Boot),
#if defined(HW_LILYGO2CAN) || defined(HW_STARK)
    setting("CANFD2ASCAN", ST::Bool, kHardware, SA::Boot),
#endif
    setting("EQSTOP", ST::EnumUint, kHardware, SA::Boot).with_options("button"),
    setting("PRECHGMS", ST::Uint, kHardware, SA::Boot, 100).with_range(1, 65000),
    setting("NCCONTACTOR", ST::Bool, kHardware, SA::Boot),
    setting("PWMCNTCTRL", ST::Bool, kHardware, SA::Boot),
    setting("PWMFREQ", ST::Uint, kHardware, SA::Boot, 20000).with_range(1, 65000),
    setting("PWMHOLD", ST::Uint, kHardware, SA::Boot, 250).with_range(1, 1023),
    setting("PERBMSRESET", ST::Bool, kHardware, SA::Boot),
    setting("PERBMSRESETH", ST::EnumUint, kHardware, SA::Boot, 24).with_options("bmsresetinterval"),
    setting("PERBMSDEFSOC", ST::Bool, kHardware, SA::Boot),
    setting("PERBMSSKIPBAL", ST::Bool, kHardware, SA::Boot),
    setting("EXTPRECHARGE", ST::Bool, kHardware, SA::Boot),
    setting("MAXPRETIME", ST::Uint, kHardware, SA::Boot, 15000),
    setting("MAXPREFREQ", ST::Uint, kHardware, SA::Boot, 34000),
    setting("NOINVDISC", ST::Bool, kHardware, SA::Boot),
    setting("LEDMODE", ST::EnumUint, kHardware, SA::Boot).with_options("ledmode"),

    setting("WIFIAPENABLED", ST::Bool, kConnectivity, SA::Boot, 1),
    setting("APPASSWORD", ST::StringVal, kConnectivity, SA::Boot).with_text("123456789"),
    setting("WIFICHANNEL", ST::Uint, kConnectivity, SA::Boot).with_range(0, 14),
    setting("HOSTNAME", ST::StringVal, kConnectivity, SA::Boot).with_text(""),
    setting("STATICIP", ST::Bool, kConnectivity, SA::Boot),
    setting("LOCALIP", ST::StringVal, kConnectivity, SA::Boot).with_text(""),
    setting("GATEWAY", ST::StringVal, kConnectivity, SA::Boot).with_text(""),
    setting("SUBNET", ST::StringVal, kConnectivity, SA::Boot).with_text(""),
    setting("DNS", ST::StringVal, kConnectivity, SA::Boot).with_text(""),
    setting("ESPNOWENABLED", ST::Bool, kConnectivity, SA::Boot),
    setting("ESPNOWMACS", ST::StringVal, kConnectivity, SA::Boot).with_text("").with_range(kNoMin, 180),
    setting("MQTTENABLED", ST::Bool, kConnectivity, SA::Boot),
    setting("MQTTSERVER", ST::StringVal, kConnectivity, SA::Boot).with_text(""),
    setting("MQTTPORT", ST::Uint, kConnectivity, SA::Boot, 1883).with_range(1, 65535),
    setting("MQTTUSER", ST::StringVal, kConnectivity, SA::Boot).with_text(""),
    setting("MQTTPASSWORD", ST::StringVal, kConnectivity, SA::Boot).with_text(""),
    setting("MQTTTIMEOUT", ST::Uint, kConnectivity, SA::Boot, 2000).with_range(1, 60000),
    setting("MQTTPUBLISHMS", ST::SecondsToMs, kConnectivity, SA::Boot, 5).with_range(1, 300),
    setting("MQTTCELLV", ST::Bool, kConnectivity, SA::Boot),
    setting("MQTTHEAP", ST::Bool, kConnectivity, SA::Boot),
    setting("REMBMSRESET", ST::Bool, kConnectivity, SA::Boot),
    setting("HADISC", ST::Bool, kConnectivity, SA::Boot),
    setting("HADISCFWU", ST::Bool, kConnectivity, SA::Boot),
    setting("HADISCTOPIC", ST::StringVal, kConnectivity, SA::Boot).with_text("homeassistant"),

    setting("PERFPROFILE", ST::Bool, kDebug, SA::Boot),
    setting("MEASURECPUTEMP", ST::Bool, kDebug, SA::Boot),
    setting("CPUTEMPOFFSET", ST::Int, kDebug, SA::Boot),
    setting("CANLOGUSB", ST::Bool, kDebug, SA::Boot),
    setting("USBENABLED", ST::Bool, kDebug, SA::Boot),
    setting("WEBENABLED", ST::Bool, kDebug, SA::Boot),
    setting("CANLOGSD", ST::Bool, kDebug, SA::Boot),
    setting("SDLOGENABLED", ST::Bool, kDebug, SA::Boot),
#ifndef SMALL_FLASH_DEVICE
    // Gate matches the comm_nvm reads and the syslog client.
    setting("SYSLOGEN", ST::Bool, kDebug, SA::Boot),
    setting("SYSLOGIP", ST::StringVal, kDebug, SA::Boot).with_text(""),
    setting("SYSLOGPORT", ST::Uint, kDebug, SA::Boot, 514).with_range(1, 65535),
    setting("SYSLOGFAC", ST::Uint, kDebug, SA::Boot, 1).with_range(0, 23),
#endif

    setting("BATTERY_WH_MAX", ST::Uint, kLive, SA::Live)
        .in_section(kSecChargeLimits)
        .bound({SR::U32, [](uint8_t) -> void* { return &datalayer.battery.settings.user_set_total_capacity_Wh; }, 1,
                SCP::Global,seed_slot_capacities}),
    setting("USE_SCALED_SOC", ST::Bool, kLive, SA::Live)
        .in_section(kSecChargeLimits)
        .bound({SR::Bool, [](uint8_t) -> void* { return &datalayer.battery.settings.soc_scaling_active; }}),
    setting("MAXPERCENTAGE", ST::DeciUnits, kLive, SA::Live)
        .in_section(kSecChargeLimits)
        .bound({SR::U16, [](uint8_t) -> void* { return &datalayer.battery.settings.max_percentage; }, kPptPerPercent}),
    setting("MINPERCENTAGE", ST::SignedDeciUnits, kLive, SA::Live)
        .in_section(kSecChargeLimits)
        .bound({SR::I16, [](uint8_t) -> void* { return &datalayer.battery.settings.min_percentage; }, kPptPerPercent}),
    setting("MAXCHARGEAMP", ST::DeciUnits, kLive, SA::Live)
        .in_section(kSecChargeLimits)
        .bound({SR::U16, [](uint8_t) -> void* { return &datalayer.battery.settings.max_user_set_charge_dA; },
                kDeciPerUnit}),
    setting("MAXDISCHARGEAMP", ST::DeciUnits, kLive, SA::Live)
        .in_section(kSecChargeLimits)
        .bound({SR::U16, [](uint8_t) -> void* { return &datalayer.battery.settings.max_user_set_discharge_dA; },
                kDeciPerUnit}),
    setting("USEVOLTLIMITS", ST::Bool, kLive, SA::Live)
        .in_section(kSecChargeLimits)
        .bound({SR::Bool, [](uint8_t) -> void* { return &datalayer.battery.settings.user_set_voltage_limits_active; }}),
    setting("TARGETCHVOLT", ST::DeciUnits, kLive, SA::Live)
        .in_section(kSecChargeLimits)
        .bound({SR::U16, [](uint8_t) -> void* { return &datalayer.battery.settings.max_user_set_charge_voltage_dV; },
                kDeciPerUnit}),
    setting("TARGETDISCHVOLT", ST::DeciUnits, kLive, SA::Live)
        .in_section(kSecChargeLimits)
        .bound({SR::U16, [](uint8_t) -> void* { return &datalayer.battery.settings.max_user_set_discharge_voltage_dV; },
                kDeciPerUnit}),
    setting("BMSRESETDUR", ST::SecondsToMs, kLive, SA::Live)
        .in_section(kSecChargeLimits)
        .bound({SR::U32, [](uint8_t) -> void* { return &datalayer.battery.settings.user_set_bms_reset_duration_ms; },
                kMillisecondsPerSecond}),

    setting("hv_enabled", ST::Bool, kLive, SA::Live)
        .volatile_storage()
        .in_section(kSecCharger)
        .bound({SR::Bool, [](uint8_t) -> void* { return &datalayer.charger.charger_HV_enabled; }}),
    setting("aux12v_enabled", ST::Bool, kLive, SA::Live)
        .volatile_storage()
        .in_section(kSecCharger)
        .bound({SR::Bool, [](uint8_t) -> void* { return &datalayer.charger.charger_aux12V_enabled; }}),
    setting("setpoint_v", ST::Float, kLive, SA::Live)
        .volatile_storage()
        .in_section(kSecCharger)
        .bound({SR::F32, [](uint8_t) -> void* { return &datalayer.charger.charger_setpoint_HV_VDC; }}),
    setting("setpoint_a", ST::Float, kLive, SA::Live)
        .volatile_storage()
        .in_section(kSecCharger)
        .bound({SR::F32, [](uint8_t) -> void* { return &datalayer.charger.charger_setpoint_HV_IDC; }}),
    setting("end_a", ST::Float, kLive, SA::Live)
        .volatile_storage()
        .in_section(kSecCharger)
        .bound({SR::F32, [](uint8_t) -> void* { return &datalayer.charger.charger_setpoint_HV_IDC_END; }}),

    setting("recovery_mode", ST::Bool, kLive, SA::Live)
        .volatile_storage()
        .in_section(kSecRecovery)
        .bound({SR::Bool,
                [](uint8_t) -> void*{
                  return &datalayer.battery.settings.user_requests_forced_charging_recovery_mode;
                }}),

    setting("cutoff", ST::Uint, kLive, SA::Live)
        .volatile_storage()
        .in_section(kSecCanIdCutoff)
        .bound({SR::U16, [](uint8_t) -> void* { return &user_selected_CAN_ID_cutoff_filter; }}),
};

const size_t kSettingFieldCount = sizeof(kSettingFields) / sizeof(kSettingFields[0]);
static_assert(fields_valid(kSettingFields), "a setting key is empty or too long for NVS, or its range is inverted");
static_assert(keys_unique(kSettingFields), "two settings share a key");

namespace {
constexpr DeviceSetting battery_row(SettingField field, const char* label, BatteryCapabilities capability) {
  return {field, label, kAnySlot, SettingDomain::Battery, capability.bits, nullptr};
}

constexpr DeviceSetting inverter_row(SettingField field, const char* label, InverterCapabilities capability) {
  return {field, label, kAnySlot, SettingDomain::Inverter, capability.bits, nullptr};
}

constexpr DeviceSetting kFamilySettingFields[] = {
    battery_row(setting("BATTPVMAX", ST::DeciUnits, kBattery, SA::Boot), "Battery max design voltage (V)",
                BatteryCapability::DesignVoltages),
    battery_row(setting("BATTPVMIN", ST::DeciUnits, kBattery, SA::Boot), "Battery min design voltage (V)",
                BatteryCapability::DesignVoltages),
    battery_row(setting("BATTCVMAX", ST::Uint, kBattery, SA::Boot), "Cell max design voltage (mV)",
                BatteryCapability::DesignVoltages),
    battery_row(setting("BATTCVMIN", ST::Uint, kBattery, SA::Boot), "Cell min design voltage (mV)",
                BatteryCapability::DesignVoltages),
    battery_row(setting("SOCESTIMATED", ST::Bool, kBattery, SA::Boot), "Use estimated SOC",
                BatteryCapability::EstimatedSoc),
    battery_row(setting("CHGESTIMATED", ST::Bool, kBattery, SA::Boot), "Use estimated charge/discharge limits",
                BatteryCapability::EstimatedChargeLimits),
    battery_row(setting("CHGPOWER", ST::Uint, kBattery, SA::Boot, 1000).with_range(0, 65000),
                "Manual charging power, watt", BatteryCapability::EstimatedLimits),
    battery_row(setting("DCHGPOWER", ST::Uint, kBattery, SA::Boot, 1000).with_range(0, 65000),
                "Manual discharge power, watt", BatteryCapability::EstimatedLimits),
    inverter_row(setting("INVCELLS", ST::Uint, kInverter, SA::Boot), "Reported cell count (0 for default)",
                 InverterCapability::PackGeometry),
    inverter_row(setting("INVMODULES", ST::Uint, kInverter, SA::Boot), "Reported module count (0 for default)",
                 InverterCapability::ModuleCount),
    inverter_row(setting("INVCELLSPER", ST::Uint, kInverter, SA::Boot),
                 "Reported cells per module (0 for default)", InverterCapability::PackGeometry),
    inverter_row(setting("INVVLEVEL", ST::Uint, kInverter, SA::Boot), "Reported voltage level (0 for default)",
                 InverterCapability::PackGeometry),
    inverter_row(setting("INVCAPACITY", ST::Uint, kInverter, SA::Boot), "Reported Ah capacity (0 for default)",
                 InverterCapability::PackGeometry),
    inverter_row(setting("INVICNT", ST::EnumUint, kInverter, SA::Boot).with_options("contactor"),
                 "Inverter Contactor Workaround", InverterCapability::ContactorWorkaround),
    battery_row(setting("max_time_min", ST::Float, kLive, SA::Live)
        .volatile_storage()
        .in_section(kSecBalancing)
        .bound({SR::U32,
                [](uint8_t slot) -> void*{
                  return &datalayer.battery_slot(slot).settings.balancing_max_time_ms;
                },kMsPerMinute,SCP::BatterySlot}),
                "Balancing max time (min)", BatteryCapability::ForcedBalancing | BatteryCapability::UserBalancing),
    battery_row(setting("max_cell_mv", ST::Uint, kLive, SA::Live)
        .volatile_storage()
        .in_section(kSecBalancing)
        .bound({SR::U16,
                [](uint8_t slot) -> void*{
                  return &datalayer.battery_slot(slot).settings.balancing_max_cell_voltage_mV;
                },1,SCP::BatterySlot}),
                "Max cell voltage (mV)", BatteryCapability::ForcedBalancing),
    battery_row(setting("max_dev_mv", ST::Uint, kLive, SA::Live)
        .volatile_storage()
        .in_section(kSecBalancing)
        .bound({SR::U16,
                [](uint8_t slot) -> void*{
                  return &datalayer.battery_slot(slot).settings.balancing_max_deviation_cell_voltage_mV;
                },1,SCP::BatterySlot}),
                "Max cell deviation (mV)", BatteryCapability::ForcedBalancing),
    battery_row(setting("max_pack_v", ST::Float, kLive, SA::Live)
        .volatile_storage()
        .in_section(kSecBalancing)
        .bound({SR::U16,
                [](uint8_t slot) -> void*{
                  return &datalayer.battery_slot(slot).settings.balancing_max_pack_voltage_dV;
                },kDeciPerUnit,SCP::BatterySlot}),
                "Max pack voltage (V)", BatteryCapability::ForcedBalancing),
    battery_row(setting("float_power_w", ST::Uint, kLive, SA::Live)
        .volatile_storage()
        .in_section(kSecBalancing)
        .bound({SR::U16,
                [](uint8_t slot) -> void*{
                  return &datalayer.battery_slot(slot).settings.balancing_float_power_W;
                },1,SCP::BatterySlot}),
                "Float power (W)", BatteryCapability::ForcedBalancing),
};

constexpr size_t kFamilySettingFieldCount = sizeof(kFamilySettingFields) / sizeof(kFamilySettingFields[0]);
static_assert(fields_valid(kFamilySettingFields), "a family setting key is invalid, or its range is inverted");

DeviceSettingSource device_source_at(SettingDomain domain, size_t index) {
  return domain == SettingDomain::Battery ? battery_type_settings_at(index).settings
                                          : inverter_type_settings_at(index).settings;
}

size_t device_type_count(SettingDomain domain) {
  return domain == SettingDomain::Battery ? battery_type_settings_count() : inverter_type_settings_count();
}

uint16_t device_capabilities_at(SettingDomain domain, size_t index) {
  return domain == SettingDomain::Battery ? battery_type_settings_at(index).capabilities.bits
                                          : inverter_type_settings_at(index).capabilities.bits;
}

uint32_t device_type_id_at(SettingDomain domain, size_t index) {
  return domain == SettingDomain::Battery ? static_cast<uint32_t>(battery_type_settings_at(index).id)
                                          : static_cast<uint32_t>(inverter_type_settings_at(index).id);
}

bool source_already_visited(SettingDomain domain, size_t upto, DeviceSettingSource source) {
  for (size_t i = 0; i < upto; i++) {
    if (device_source_at(domain, i) == source) {
      return true;
    }
  }
  return false;
}

size_t domain_row_count(SettingDomain domain) {
  size_t total = 0;
  const size_t type_count = device_type_count(domain);
  for (size_t i = 0; i < type_count; i++) {
    const DeviceSettingSource source = device_source_at(domain, i);
    if (source != nullptr && !source_already_visited(domain, i, source)) {
      total += source().count;
    }
  }
  return total;
}

DeviceSettingList board_settings() {
  return esp32hal != nullptr ? esp32hal->settings() : DeviceSettingList{};
}

bool scope_has_rows(SettingScope scope) {
  const DeviceSettingList board = board_settings();
  for (size_t i = 0; i < board.count; i++) {
    if (board.data[i].field.live.scope == scope) {
      return true;
    }
  }
  return false;
}

bool scope_has_index(const ScopeEntries& entries, uint8_t index) {
  for (size_t i = 0; i < entries.count; i++) {
    if (entries.data[i].index == index) {
      return true;
    }
  }
  return false;
}

SettingRef domain_row_at(SettingDomain domain, size_t index) {
  const size_t type_count = device_type_count(domain);
  for (size_t i = 0; i < type_count; i++) {
    const DeviceSettingSource source = device_source_at(domain, i);
    if (source == nullptr || source_already_visited(domain, i, source)) {
      continue;
    }
    const DeviceSettingList list = source();
    if (index < list.count) {
      return {&list.data[index].field, &list.data[index], source, domain};
    }
    index -= list.count;
  }
  return {nullptr, nullptr, nullptr, SettingDomain::None};
}
}  // namespace

size_t setting_count() {
  return kSettingFieldCount + kFamilySettingFieldCount + domain_row_count(SettingDomain::Battery) +
         domain_row_count(SettingDomain::Inverter) + board_settings().count;
}

SettingRef setting_at(size_t index) {
  if (index < kSettingFieldCount) {
    return {&kSettingFields[index], nullptr, nullptr, SettingDomain::None};
  }
  index -= kSettingFieldCount;
  if (index < kFamilySettingFieldCount) {
    const DeviceSetting& row = kFamilySettingFields[index];
    return {&row.field, &row, nullptr, row.domain};
  }
  index -= kFamilySettingFieldCount;
  const size_t battery_rows = domain_row_count(SettingDomain::Battery);
  if (index < battery_rows) {
    return domain_row_at(SettingDomain::Battery, index);
  }
  index -= battery_rows;
  const size_t inverter_rows = domain_row_count(SettingDomain::Inverter);
  if (index < inverter_rows) {
    return domain_row_at(SettingDomain::Inverter, index);
  }
  index -= inverter_rows;
  const DeviceSettingList board = board_settings();
  if (index < board.count) {
    const DeviceSetting& row = board.data[index];
    return {&row.field, &row, nullptr, SettingDomain::None};
  }
  return {nullptr, nullptr, nullptr, SettingDomain::None};
}

namespace {
// The stored password keys (mirrors the client PASSWORD_KEYS set). HTTPPASSCONFIRM is
// client-synthesised, not stored, so it is deliberately excluded.
bool is_password_key(const char* nvs_key) {
  return std::strcmp(nvs_key, "HTTPPASS") == 0 || std::strcmp(nvs_key, "PASSWORD") == 0 ||
         std::strcmp(nvs_key, "APPASSWORD") == 0 || std::strcmp(nvs_key, "MQTTPASSWORD") == 0;
}

// Mirrors options_for_enum_with_none: none first, then labels ascending.
// strcmp on the producers' static literals reproduces String's order without
// the host emul String, which has no operator<.
template <typename TEnum, typename Producer>
void emit_enum_options(SettingOptionSink& sink, const char* key, Producer name_for_type,
                       const TEnum* none = nullptr, int32_t (*slots_for)(TEnum) = nullptr) {
  std::vector<std::pair<const char*, TEnum>> pairs;
  for (TEnum value : enum_values<TEnum>()) {
    const char* name = name_for_type(value);
    if (name != nullptr && name[0] != '\0') {
      pairs.emplace_back(name, value);
    }
  }
  std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return std::strcmp(a.first, b.first) < 0; });
  if (none != nullptr) {
    const char* none_name = name_for_type(*none);
    const bool present = std::any_of(pairs.begin(), pairs.end(), [&](const auto& p) { return p.second == *none; });
    if (!present && none_name != nullptr && none_name[0] != '\0') {
      pairs.insert(pairs.begin(), {none_name, *none});
    }
  }

  sink.begin_list(key);
  for (const auto& [name, value] : pairs) {
    sink.option(static_cast<int>(value), name, slots_for != nullptr ? slots_for(value) : 0);
  }
  sink.end_list();
}

void emit_map_options(SettingOptionSink& sink, const char* key, const std::map<int, String>& value_names) {
  sink.begin_list(key);
  for (const auto& [value, name] : value_names) {
    sink.option(value, name.c_str(), 0);
  }
  sink.end_list();
}

// Parity with emit_enum_options: visible (non-null-label) choices, alphabetical
// by label. Keyed by nvs_key so the schema row's options reference matches.
// Emit-by-scan over the handful of choices, ordered by (label, address); keeps
// std::sort/std::vector out of every board, including option-less ones that
// never reclaim the code.
void emit_gpio_option_choices(SettingOptionSink& sink, const GpioOptionGroup& group) {
  sink.begin_list(group.nvs_key);
  const GpioOptionChoice* prev = nullptr;
  for (;;) {
    const GpioOptionChoice* next = nullptr;
    for (uint8_t c = 0; c < group.choice_count; c++) {
      const GpioOptionChoice& choice = group.choices[c];
      if (choice.label == nullptr || choice.label[0] == '\0') {
        continue;
      }
      if (prev != nullptr) {
        const int rel = std::strcmp(choice.label, prev->label);
        if (rel < 0 || (rel == 0 && &choice <= prev)) {
          continue;
        }
      }
      if (next != nullptr) {
        const int rel = std::strcmp(choice.label, next->label);
        if (rel > 0 || (rel == 0 && &choice > next)) {
          continue;
        }
      }
      next = &choice;
    }
    if (next == nullptr) {
      break;
    }
    sink.option(static_cast<int>(next->value), next->label, 0);
    prev = next;
  }
  sink.end_list();
}

const char* domain_name(SettingDomain domain) {
  switch (domain) {
    case SettingDomain::Battery:
      return "battery";
    case SettingDomain::Inverter:
      return "inverter";
    case SettingDomain::None:
      break;
  }
  return "";
}

void emit_device_ownership(ResponseWriter& out, const DeviceSetting& row, SettingDomain domain,
                           DeviceSettingSource source) {
  if (row.label != nullptr) {
    out.field("label", row.label);
  }
  if (row.slot != kAnySlot) {
    out.field("slot", row.slot);
  }
  if (domain == SettingDomain::None) {
    return;
  }
  out.field("domain", domain_name(domain));
  out.begin_array("owners");
  const size_t type_count = device_type_count(domain);
  for (size_t i = 0; i < type_count; i++) {
    const bool owned = source != nullptr
                           ? device_source_at(domain, i) == source
                           : row.capability_bits != 0 &&
                                 (device_capabilities_at(domain, i) & row.capability_bits) != 0;
    if (owned) {
      out.element(device_type_id_at(domain, i));
    }
  }
  out.end_array();
}

// Stable strings keep the client independent of the SettingType enum's numeric ordering.
const char* widget_type_name(SettingType type) {
  switch (type) {
    case ST::Bool:
      return "bool";
    case ST::Uint:
      return "uint";
    case ST::Int:
      return "int";
    case ST::StringVal:
      return "string";
    case ST::EnumUint:
      return "enum";
    case ST::DeciUnits:
    case ST::SignedDeciUnits:
    case ST::Float:
      return "float";
    case ST::FloatString:
      return "floatstring";
    case ST::SecondsToMs:
      return "seconds";
    case ST::InterfacePacked:
      return "interface";
  }
  return "";
}

double read_live(const SettingField& field, uint8_t slot) {
  void* address = field.live.address(slot);
  switch (field.live.kind) {
    case SR::Bool:
      return *static_cast<bool*>(address) ? 1.0 : 0.0;
    case SR::U8:
      return *static_cast<uint8_t*>(address);
    case SR::U16:
      return *static_cast<uint16_t*>(address);
    case SR::I16:
      return *static_cast<int16_t*>(address);
    case SR::U32:
      return *static_cast<uint32_t*>(address);
    case SR::F32:
      return *static_cast<float*>(address);
    case SR::None:
      break;
  }
  return 0.0;
}

void write_live(const SettingField& field, uint8_t slot, double json_value) {
  void* address = field.live.address(slot);
  const double scaled = json_value * field.live.ram_per_json;
  switch (field.live.kind) {
    case SR::Bool:
      *static_cast<bool*>(address) = json_value != 0.0;
      break;
    case SR::U8:
      *static_cast<uint8_t*>(address) = static_cast<uint8_t>(std::llround(scaled));
      break;
    case SR::U16:
      *static_cast<uint16_t*>(address) = static_cast<uint16_t>(std::llround(scaled));
      break;
    case SR::I16:
      *static_cast<int16_t*>(address) = static_cast<int16_t>(std::llround(scaled));
      break;
    case SR::U32:
      *static_cast<uint32_t*>(address) = static_cast<uint32_t>(std::llround(scaled));
      break;
    case SR::F32:
      *static_cast<float*>(address) = static_cast<float>(scaled);
      break;
    case SR::None:
      break;
  }
  if (field.live.on_apply != nullptr) {
    field.live.on_apply(slot, json_value);
  }
}

void emit_live_value(ResponseWriter& out, const SettingField& field, uint8_t slot) {
  const double value = read_live(field, slot) / field.live.ram_per_json;
  switch (field.type) {
    case ST::Bool:
      out.field(field.key, value != 0.0);
      break;
    case ST::DeciUnits:
    case ST::SignedDeciUnits:
    case ST::Float:
      out.field(field.key, value);
      break;
    default:
      out.field(field.key, static_cast<int64_t>(std::llround(value)));
      break;
  }
}

void emit_scoped_value(ResponseWriter& out, const SettingField& field, uint8_t index,
                       BatteryEmulatorSettingsStore& store) {
  if (field.live.kind != SR::None) {
    emit_live_value(out, field, index);
    return;
  }
  const String key = scoped_key(field, index);
  switch (field.type) {
    case ST::Bool:
      out.field(field.key, store.getBool(key.c_str(), field.default_int != 0));
      break;
    case ST::Uint:
    case ST::EnumUint:
      out.field(field.key, store.getUInt(key.c_str(), static_cast<uint32_t>(field.default_int)));
      break;
    case ST::Int:
      out.field(field.key, store.getInt(key.c_str(), field.default_int));
      break;
    case ST::Float:
      out.field(field.key, store.getFloat(key.c_str(), static_cast<float>(field.default_int)));
      break;
    case ST::DeciUnits:
    case ST::SignedDeciUnits:
    case ST::StringVal:
    case ST::FloatString:
    case ST::SecondsToMs:
    case ST::InterfacePacked:
      break;
  }
}

// Live rows route through write_live, which runs on_apply itself.
bool apply_scoped_value(BatteryEmulatorSettingsStore& store, const SettingField& field, uint8_t index,
                        const DocumentValue& value) {
  const double number = field.type == ST::Bool ? (value.as_bool() ? 1.0 : 0.0) : value.as_number();
  if (!in_bounds(field, number)) {
    return false;
  }
  if (field.live.kind != SR::None) {
    write_live(field, index, number);
    return false;
  }
  const String key = scoped_key(field, index);
  bool changed = false;
  switch (field.type) {
    case ST::Bool: {
      const bool new_value = number != 0.0;
      changed = store.getBool(key.c_str(), field.default_int != 0) != new_value;
      store.saveBool(key.c_str(), new_value);
      break;
    }
    case ST::Uint:
    case ST::EnumUint: {
      const uint32_t new_value = static_cast<uint32_t>(std::llround(number));
      changed = store.getUInt(key.c_str(), static_cast<uint32_t>(field.default_int)) != new_value;
      store.saveUInt(key.c_str(), new_value);
      break;
    }
    case ST::Int: {
      const int32_t new_value = static_cast<int32_t>(std::llround(number));
      changed = store.getInt(key.c_str(), field.default_int) != new_value;
      store.saveInt(key.c_str(), new_value);
      break;
    }
    case ST::Float: {
      const float new_value = static_cast<float>(number);
      changed = store.getFloat(key.c_str(), static_cast<float>(field.default_int)) != new_value;
      store.saveFloat(key.c_str(), new_value);
      break;
    }
    case ST::DeciUnits:
    case ST::SignedDeciUnits:
    case ST::StringVal:
    case ST::FloatString:
    case ST::SecondsToMs:
    case ST::InterfacePacked:
      return false;
  }
  if (field.applies == SA::Live && field.live.on_apply != nullptr) {
    field.live.on_apply(index, number);
  }
  return field.applies == SA::Boot && changed;
}

void emit_asset_name_options(SettingOptionSink& sink, const char* options_key, AssetNameSpec spec) {
  sink.begin_list(options_key);
  const WebAssetTable table = default_web_asset_table();
  const size_t total = web_asset_name_count(table, spec);
  for (size_t i = 0; i < total; i++) {
    char name[kMaxAssetNameLen + 1];
    if (!web_asset_name_at(table, spec, i, name, sizeof(name))) {
      continue;
    }
    sink.text_option(name);
  }
  sink.end_list();
}

int32_t battery_slot_hint(BatteryType type) {
  if (type == BatteryType::None) {
    return 0;
  }
  return battery_type_allowed_in_slot(type, 2) ? 3 : battery_type_allowed_in_slot(type, 1) ? 2 : 1;
}

class WriterOptionSink : public SettingOptionSink {
 public:
  explicit WriterOptionSink(ResponseWriter& out) : out_(out) {}

  void begin_list(const char* key) override { out_.begin_array(key); }

  void option(int32_t value, const char* name, int32_t slots) override {
    out_.begin_object();
    out_.field("v", value);
    if (name != nullptr) {
      out_.field("n", name);
    }
    if (slots != 0) {
      out_.field("s", slots);
    }
    out_.end_object();
  }

  void text_option(const char* value) override {
    out_.begin_object();
    out_.field("v", value);
    out_.end_object();
  }

  void end_list() override { out_.end_array(); }

 private:
  ResponseWriter& out_;
};

}  // namespace

void emit_all_options(SettingOptionSink& sink) {
  const BatteryType battery_none = BatteryType::None;
  emit_enum_options(
      sink, "battery",
      [](BatteryType t) { return board_supports_battery_type(t) ? name_for_battery_type(t) : nullptr; },
      &battery_none, battery_slot_hint);
  emit_enum_options<battery_chemistry_enum>(sink, "chemistry", name_for_chemistry);
  const InverterProtocolType inverter_none = InverterProtocolType::None;
  emit_enum_options(sink, "inverter", name_for_inverter_type, &inverter_none);
  const ChargerType charger_none = ChargerType::None;
  emit_enum_options(sink, "charger", name_for_charger_type, &charger_none);
  const ShuntType shunt_none = ShuntType::None;
  emit_enum_options(sink, "shunt", name_for_shunt_type, &shunt_none);
  const adc_attenuation_enum attenuation_none = adc_attenuation_enum::ADC_0db;
  emit_enum_options(sink, "attenuation", name_for_adc_attenuation, &attenuation_none);
  const STOP_BUTTON_BEHAVIOR button_none = STOP_BUTTON_BEHAVIOR::NOT_CONNECTED;
  emit_enum_options(sink, "button", name_for_button_type, &button_none);

  if (esp32hal != nullptr) {
    GpioOptionCatalog catalog = esp32hal->gpio_options();
    for (size_t g = 0; g < catalog.group_count; g++) {
      emit_gpio_option_choices(sink, catalog.groups[g]);
    }
  }

  // Board-conditional (HW_LILYGO2CAN adds GRB variants), so kept server-side.
  emit_map_options(sink, "ledmode", led_modes);
  emit_map_options(sink, "bmsresetinterval", bms_reset_intervals);

  if (esp32hal != nullptr) {
    if (LoadSwitch* load_switch = esp32hal->load_switch()) {
      // Enum order (Disabled first, unsorted), matching the legacy per-channel select.
      sink.begin_list("loadswitchrole");
      for (uint32_t r = 0; r < static_cast<uint32_t>(LoadSwitchRole::Highest); r++) {
        const char* name = name_for_load_switch_role(static_cast<LoadSwitchRole>(r));
        if (name == nullptr || name[0] == '\0') {
          continue;
        }
        sink.option(static_cast<int32_t>(r), name, 0);
      }
      sink.end_list();

      sink.begin_list("loadswitchdivisor");
      for (uint8_t d = 0; d < kLoadSwitchDivisorCodes; d++) {
        const String label = String("÷") + String(load_switch_divisor_ratio(d)) + " (" +
                             String(load_switch_pwm_frequency_hz(load_switch->pwm_clock_hz(), d)) + " Hz)";
        sink.option(d, label.c_str(), 0);
      }
      sink.end_list();
    }
  }

  emit_asset_name_options(sink, "webui", kUiShellSpec);
}

void apply_stored_board_settings(BatteryEmulatorSettingsStore& store) {
  if (esp32hal == nullptr) {
    return;
  }
  static const ScopeEntry kGlobalEntry = {0, nullptr};
  const DeviceSettingList board = esp32hal->settings();
  for (size_t r = 0; r < board.count; r++) {
    const DeviceSetting& row = board.data[r];
    const SettingField& field = row.field;
    if (field.live.on_apply == nullptr || field.storage == SS::Volatile) {
      continue;
    }
    const ScopeEntries entries = field.live.scope == SCP::Global ? ScopeEntries{&kGlobalEntry, 1}
                                                                : esp32hal->scope_entries(field.live.scope);
    for (size_t e = 0; e < entries.count; e++) {
      const uint8_t index = entries.data[e].index;
      if (row.applies_to != nullptr && !row.applies_to(index)) {
        continue;
      }
      const String key = scoped_key(field, index);
      double value = field.default_int;
      switch (field.type) {
        case ST::Bool:
          value = store.getBool(key.c_str(), field.default_int != 0) ? 1.0 : 0.0;
          break;
        case ST::Uint:
        case ST::EnumUint:
          value = store.getUInt(key.c_str(), static_cast<uint32_t>(field.default_int));
          break;
        case ST::Int:
          value = store.getInt(key.c_str(), field.default_int);
          break;
        case ST::Float:
          value = store.getFloat(key.c_str(), static_cast<float>(field.default_int));
          break;
        case ST::DeciUnits:
        case ST::SignedDeciUnits:
        case ST::StringVal:
        case ST::FloatString:
        case ST::SecondsToMs:
        case ST::InterfacePacked:
          continue;
      }
      // Only a hand-edited NVS holds an out-of-range value; clamping it to the
      // bound would apply a setting the schema never offered.
      field.live.on_apply(index, in_bounds(field, value) ? value : field.default_int);
    }
  }
}

void write_settings(ResponseWriter& out, BatteryEmulatorSettingsStore& store, bool reboot_required) {
  out.begin_object();
  out.begin_object("values");

  const size_t total_settings = setting_count();
  for (size_t i = 0; i < total_settings; i++) {
    const SettingField& field = *setting_at(i).field;
    if (field.live.scope != SCP::Global) {
      continue;
    }
    if (field.live.kind != SR::None) {
      emit_live_value(out, field, 0);
      continue;
    }
    switch (field.type) {
      case ST::Bool:
        out.field(field.key, store.getBool(field.key, field.default_int != 0));
        break;
      case ST::Uint:
      case ST::EnumUint:
        out.field(field.key, store.getUInt(field.key, static_cast<uint32_t>(field.default_int)));
        break;
      case ST::Int:
        out.field(field.key, store.getInt(field.key, field.default_int));
        break;
      case ST::StringVal:
      case ST::FloatString:
        out.field(field.key,
                  is_password_key(field.key) ? "" : store.getString(field.key, field.default_str).c_str());
        break;
      case ST::DeciUnits:
        out.field(field.key,
                  store.getUInt(field.key, static_cast<uint32_t>(field.default_int)) / kDeciUnitsPerUnit);
        break;
      case ST::SignedDeciUnits:
        out.field(field.key, store.getInt(field.key, field.default_int) / kDeciUnitsPerUnit);
        break;
      case ST::Float:
        if (field.storage != SS::Volatile) {
          out.field(field.key, store.getFloat(field.key, static_cast<float>(field.default_int)));
        }
        break;
      case ST::SecondsToMs:
        out.field(field.key,
                  store.getUInt(field.key, static_cast<uint32_t>(field.default_int) * kMillisecondsPerSecond) /
                      kMillisecondsPerSecond);
        break;
      case ST::InterfacePacked: {
        // A raw or unresolvable stored value would match no interfaces[].id and,
        // once re-saved, freeze invalid config into NVS; fall back to the default.
        uint32_t stored = store.getUInt(field.key, 0);
        if (esp32hal != nullptr) {
          InterfaceList list = esp32hal->interfaces();
          if (resolve_interface_config(list, stored) == nullptr) {
            stored = default_interface_config(list);
          }
        }
        out.field(field.key, stored);
        break;
      }
    }
  }

  if (esp32hal != nullptr) {
    GpioOptionCatalog catalog = esp32hal->gpio_options();
    for (size_t g = 0; g < catalog.group_count; g++) {
      // Report the choice the HAL actually runs: comm_nvm clamps an unknown stored
      // value on load, so echoing the raw one would show a state the pins are not in.
      const GpioOptionGroup& group = catalog.groups[g];
      const uint32_t stored = store.getUInt(group.nvs_key, group.default_value);
      out.field(group.nvs_key, find_gpio_option_choice(group, stored) != nullptr ? stored : group.default_value);
    }
  }
  out.end_object();

  out.begin_object("options");
  WriterOptionSink option_sink(out);
  emit_all_options(option_sink);
  out.end_object();

  // Presentation (labels, visibility) lives client-side; schema carries only identity
  // and widget wiring.
  out.begin_array("schema");
  for (size_t i = 0; i < total_settings; i++) {
    const SettingRef ref = setting_at(i);
    const SettingField& field = *ref.field;
    out.begin_object();
    out.field("key", field.key);
    out.field("category", field.category);
    out.field("type", widget_type_name(field.type));
    const char* options = field.options_key;
    if (options == nullptr && field.type == ST::InterfacePacked) {
      options = "interfaces";
    }
    out.field("options", options);  // null const char* serialises as JSON null
    out.field("section", field.section);
    if (const char* scope = scope_name(field.live.scope)) {
      out.field("scope", scope);
    }
    if (field.min_value != kNoMin) {
      out.field("min", field.min_value);
    }
    if (field.max_value != kNoMax) {
      out.field("max", field.max_value);
    }
    if (ref.device != nullptr) {
      emit_device_ownership(out, *ref.device, ref.domain, ref.source);
    }
    out.end_object();
  }

  if (esp32hal != nullptr) {
    GpioOptionCatalog catalog = esp32hal->gpio_options();
    for (size_t g = 0; g < catalog.group_count; g++) {
      const GpioOptionGroup& group = catalog.groups[g];
      out.begin_object();
      out.field("key", group.nvs_key);
      out.field("category", kHardware);
      out.field("type", widget_type_name(ST::EnumUint));
      out.field("options", group.nvs_key);
      out.field("label", group.label);
      out.end_object();
    }
  }
  out.end_array();

  // Hint shown when a field is left blank. Unlike a static default_str, the
  // hostname default is MAC-derived and known only at runtime.
  out.begin_object("placeholders");
  out.field("HOSTNAME", default_hostname().c_str());
  out.end_object();

  out.begin_object("dynamic");
  out.begin_array("batteries");
  for (const BatterySlotKeys& keys : kBatterySlotKeys) {
    out.begin_object();
    out.field("slot", keys.slot);
    out.field("type", store.getUInt(keys.type_key, static_cast<uint32_t>(BatteryType::None)));
    uint32_t comm = store.getUInt(keys.comm_key, 0);
    if (esp32hal != nullptr) {
      InterfaceList iface_list = esp32hal->interfaces();
      if (resolve_interface_config(iface_list, comm) == nullptr) {
        comm = default_interface_config(iface_list);
      }
    }
    out.field("comm", comm);
    out.field("contactor_control", store.getBool(keys.contactor_key, false));
    out.end_object();
  }
  out.end_array();

  out.begin_array("balancing");
  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    if (!battery_slot_addressable(slot)) {
      continue;
    }
    out.begin_object();
    out.field("slot", slot);
    for (size_t i = 0; i < total_settings; i++) {
      const SettingField& field = *setting_at(i).field;
      if (field.live.scope == SCP::BatterySlot) {
        emit_live_value(out, field, slot);
      }
    }
    out.end_object();
  }
  out.end_array();

  if (esp32hal != nullptr) {
    const DeviceSettingList board = board_settings();
    for (uint8_t s = 0; s < static_cast<uint8_t>(SCP::Highest); s++) {
      const SettingScope scope = static_cast<SettingScope>(s);
      const ScopeEntries entries = esp32hal->scope_entries(scope);
      if (entries.count == 0 || !scope_has_rows(scope)) {
        continue;
      }
      out.begin_array(scope_name(scope));
      for (size_t e = 0; e < entries.count; e++) {
        const ScopeEntry& entry = entries.data[e];
        out.begin_object();
        out.field("index", entry.index);
        out.field("label", entry.label);
        for (size_t r = 0; r < board.count; r++) {
          const DeviceSetting& row = board.data[r];
          if (row.field.live.scope != scope || (row.applies_to != nullptr && !row.applies_to(entry.index))) {
            continue;
          }
          emit_scoped_value(out, row.field, entry.index, store);
        }
        out.end_object();
      }
      out.end_array();
    }
  }
  out.end_object();

  if (esp32hal != nullptr) {
    InterfaceList list = esp32hal->interfaces();
    struct IfaceEntry {
      size_t index;
      uint32_t id;
      const char* name;
    };
    std::vector<IfaceEntry> entries;
    for (size_t i = 0; i < list.count; i++) {
      if (!descriptor_selectable(list.data[i])) {
        continue;
      }
      entries.push_back({i, pack_interface_config(list.data[i].type, i), descriptor_name(list.data[i])});
    }
    // Alphabetical by name, matching the legacy dropdown; index keeps the client
    // correlating with dynamic.termination[] rather than array position.
    std::sort(entries.begin(), entries.end(),
              [](const IfaceEntry& a, const IfaceEntry& b) { return std::strcmp(a.name, b.name) < 0; });
    out.begin_array("interfaces");
    for (const IfaceEntry& entry : entries) {
      out.begin_object();
      out.field("index", entry.index);
      out.field("id", entry.id);
      out.field("name", entry.name);
      out.end_object();
    }
    out.end_array();
  }

  out.begin_object("meta");
  out.field("reboot_required", reboot_required);
  out.end_object();
  out.end_object();
}

namespace {
// Resolved in one sweep ahead of the validation loop, not per field: the loop
// returns on its first failure, so the answer must already be known when it is
// reached or the reported error would no longer be the first in field order.
class OptionMembershipSink : public SettingOptionSink {
 public:
  struct Check {
    const char* list_key;
    bool text;
    uint32_t numeric;
    const char* text_value;
    bool list_seen = false;
    bool matched = false;
  };

  explicit OptionMembershipSink(std::vector<Check>& checks) : checks_(checks) {}

  void begin_list(const char* key) override { key_ = key; }

  void option(int32_t value, const char*, int32_t) override {
    for (Check& check : checks_) {
      if (std::strcmp(check.list_key, key_) != 0) {
        continue;
      }
      check.list_seen = true;
      if (!check.text && check.numeric == static_cast<uint32_t>(value)) {
        check.matched = true;
      }
    }
  }

  void text_option(const char* value) override {
    for (Check& check : checks_) {
      if (std::strcmp(check.list_key, key_) != 0) {
        continue;
      }
      check.list_seen = true;
      if (check.text && check.text_value != nullptr && std::strcmp(check.text_value, value) == 0) {
        check.matched = true;
      }
    }
  }

  void end_list() override { key_ = ""; }

 private:
  std::vector<Check>& checks_;
  const char* key_ = "";
};

bool value_matches_type(SettingType type, const DocumentValue& value) {
  switch (type) {
    case ST::Bool:
      return value.is_bool();
    case ST::Uint:
    case ST::EnumUint:
    case ST::InterfacePacked:
    case ST::SecondsToMs:
      return value.is_integer_in(0, UINT32_MAX);
    case ST::Int:
      return value.is_integer_in(INT32_MIN, INT32_MAX);
    case ST::StringVal:
    case ST::FloatString:
      return value.is_string();
    case ST::DeciUnits:
    case ST::SignedDeciUnits:
    case ST::Float:
      return value.is_number();
  }
  return false;
}
}  // namespace

SettingsApplyResult apply_settings(BatteryEmulatorSettingsStore& store, const DocumentReader& body) {
  SettingsApplyResult result{true, String(), false, false};

  // Guard on the effective webauth state, not just whether this payload enables it: otherwise a
  // request that clears HTTPPASS while omitting WEBAUTH would leave auth on with no password (lockout).
  const DocumentValue webauth = body.value("WEBAUTH");
  const bool webauth_effective = webauth.is_bool() ? webauth.as_bool() : store.getBool("WEBAUTH", false);
  const DocumentValue user_value = body.value("HTTPUSER");
  const String http_user =
      user_value.is_string() ? String(user_value.as_text()) : store.getString("HTTPUSER", "admin");
  // Blank keeps the stored password; an explicit null clears it. Resolve to "" on a clear so the
  // webauth guard below treats a cleared password as "none".
  const DocumentValue pass_value = body.value("HTTPPASS");
  const bool http_pass_clear = pass_value.cleared();
  const bool http_pass_present = pass_value.is_string() && std::strlen(pass_value.as_text()) > 0;
  const String http_pass = http_pass_clear     ? String("")
                           : http_pass_present ? String(pass_value.as_text())
                                               : store.getString("HTTPPASS");
  const DocumentValue confirm_value = body.value("HTTPPASSCONFIRM");
  const bool http_pass_confirm_present = confirm_value.is_string() && std::strlen(confirm_value.as_text()) > 0;
  const String http_pass_confirm =
      http_pass_confirm_present ? String(confirm_value.as_text()) : http_pass;
  if (!(http_pass == http_pass_confirm)) {
    result.ok = false;
    result.error = "Web interface passwords do not match.";
    result.error_key = "error.webauth_password_mismatch";
    return result;
  }
  if (webauth_effective && (http_user.length() == 0 || http_pass.length() == 0)) {
    result.ok = false;
    result.error = "Set a username and password before enabling web interface password protection.";
    result.error_key = "error.webauth_credentials_required";
    return result;
  }

  // Validate every present field's type before writing anything, so a wrong-type
  // key rejects the whole values pass rather than partially applying it.
  const size_t total_settings = setting_count();
  std::vector<OptionMembershipSink::Check> option_checks;
  for (size_t i = 0; i < total_settings; i++) {
    const SettingField& field = *setting_at(i).field;
    if (field.options_key == nullptr || field.live.scope != SCP::Global) {
      continue;
    }
    const DocumentValue value = body.value(field.key);
    if (value.missing() || !value_matches_type(field.type, value)) {
      continue;
    }
    const bool text = field.type == ST::StringVal;
    option_checks.push_back({field.options_key, text, text ? 0u : static_cast<uint32_t>(value.integer),
                             text ? value.as_text() : nullptr});
  }
  if (!option_checks.empty()) {
    OptionMembershipSink membership(option_checks);
    emit_all_options(membership);
  }
  size_t next_check = 0;
  for (size_t i = 0; i < total_settings; i++) {
    const SettingField& field = *setting_at(i).field;
    if (field.live.scope == SCP::BatterySlot) {
      continue;
    }
    const DocumentValue value = body.value(field.key);
    if (value.missing()) {
      continue;
    }
    if (!value_matches_type(field.type, value)) {
      result.ok = false;
      result.error = String("Invalid type for setting ") + field.key;
      result.error_key = "error.setting_invalid_type";
      result.error_arg = field.key;
      return result;
    }
    // as_number() is safe: bounds sit only on numeric rows, already type-checked above.
    const bool has_min = field.min_value != kNoMin;
    const bool has_max = field.max_value != kNoMax;
    if (has_min || has_max) {
      const double numeric = value.as_number();
      if ((has_min && numeric < field.min_value) || (has_max && numeric > field.max_value)) {
        result.ok = false;
        result.error = String("Setting ") + field.key + " is out of range";
        result.error_key = "error.setting_out_of_range";
        result.error_arg = field.key;
        return result;
      }
    }
    if (field.options_key != nullptr) {
      const OptionMembershipSink::Check& check = option_checks[next_check++];
      // Some pick-lists are static enough to live in the client instead of costing
      // flash here. Membership is only checkable against a list this firmware
      // publishes; an absent one is client-owned, not an empty list of choices.
      if (check.list_seen && !check.matched) {
        result.ok = false;
        result.error = String("Setting ") + field.key + " is not an available option";
        result.error_key = "error.setting_not_an_option";
        result.error_arg = field.key;
        return result;
      }
    }
    if (const char* error = check_field(field, 0, body)) {
      result.ok = false;
      result.error = error;
      return result;
    }
  }

  const size_t balancing_rows = body.rows(kBalancingPath);
  for (size_t row = 0; row < balancing_rows; row++) {
    const DocumentRow entry(body, kBalancingPath, row);
    const DocumentValue slot_value = entry.value("slot");
    if (!slot_value.is_integer_in(0, UINT8_MAX) ||
        !battery_slot_addressable(static_cast<uint8_t>(slot_value.integer))) {
      result.ok = false;
      result.error = "Unknown battery slot";
      result.error_key = "error.battery_slot_unknown";
      return result;
    }
    const uint8_t slot = static_cast<uint8_t>(slot_value.integer);
    for (size_t i = 0; i < total_settings; i++) {
      const SettingField& field = *setting_at(i).field;
      if (field.live.scope != SCP::BatterySlot) {
        continue;
      }
      const DocumentValue value = entry.value(field.key);
      if (value.missing()) {
        continue;
      }
      if (!value_matches_type(field.type, value)) {
        result.ok = false;
        result.error = String("Invalid type for setting ") + field.key;
        result.error_key = "error.setting_invalid_type";
        result.error_arg = field.key;
        return result;
      }
      if (const char* error = check_field(field, slot, entry)) {
        result.ok = false;
        result.error = error;
        return result;
      }
    }
  }

  const size_t battery_rows = body.rows(kBatteriesPath);
  BatteryType effective_types[kMaxBatterySlots];
  for (const BatterySlotKeys& keys : kBatterySlotKeys) {
    effective_types[keys.slot] =
        static_cast<BatteryType>(store.getUInt(keys.type_key, static_cast<uint32_t>(BatteryType::None)));
  }
  for (size_t row = 0; row < battery_rows; row++) {
    const DocumentRow entry(body, kBatteriesPath, row);
    const DocumentValue slot_value = entry.value("slot");
    if (!slot_value.is_integer_in(0, UINT8_MAX) || slot_value.integer >= kMaxBatterySlots) {
      result.ok = false;
      result.error = "Unknown battery slot";
      result.error_key = "error.battery_slot_unknown";
      return result;
    }
    const uint8_t slot = static_cast<uint8_t>(slot_value.integer);
    const DocumentValue type_value = entry.value("type");
    if (!type_value.missing()) {
      if (!type_value.is_integer_in(0, UINT32_MAX)) {
        result.ok = false;
        result.error = "Invalid type for battery slot";
        result.error_key = "error.battery_slot_invalid_type";
        return result;
      }
      const BatteryType type = static_cast<BatteryType>(static_cast<uint32_t>(type_value.integer));
      if (!battery_type_allowed_in_slot(type, slot)) {
        result.ok = false;
        result.error = String("Battery ") + (slot + 1) + " cannot run the selected battery type on this hardware";
        result.error_key = "error.battery_type_unsupported";
        result.error_arg = String(slot + 1);
        return result;
      }
      effective_types[slot] = type;
    }
    const DocumentValue comm_value = entry.value("comm");
    if (!comm_value.missing() && !comm_value.is_integer_in(0, UINT32_MAX)) {
      result.ok = false;
      result.error = "Invalid interface for battery slot";
      result.error_key = "error.battery_slot_invalid_interface";
      return result;
    }
    const DocumentValue contactor_value = entry.value("contactor_control");
    if (!contactor_value.missing() && !contactor_value.is_bool()) {
      result.ok = false;
      result.error = "Invalid contactor control value for battery slot";
      result.error_key = "error.battery_slot_invalid_contactor";
      return result;
    }
  }
  if (body.has_rows(kBatteriesPath) && effective_types[0] == BatteryType::None &&
      (effective_types[1] != BatteryType::None || effective_types[2] != BatteryType::None)) {
    result.ok = false;
    result.error = "Configure the primary battery before adding extra batteries.";
    result.error_key = "error.battery_primary_required";
    return result;
  }

  if (esp32hal != nullptr) {
    GpioOptionCatalog catalog = esp32hal->gpio_options();
    for (size_t g = 0; g < catalog.group_count; g++) {
      const DocumentValue value = body.value(catalog.groups[g].nvs_key);
      if (!value.missing() && !value.is_integer_in(0, UINT32_MAX)) {
        result.ok = false;
        result.error = String("Invalid type for setting ") + catalog.groups[g].nvs_key;
        result.error_key = "error.setting_invalid_type";
        result.error_arg = catalog.groups[g].nvs_key;
        return result;
      }
    }
  }

  bool reboot_required = false;
  for (size_t i = 0; i < total_settings; i++) {
    const SettingField& field = *setting_at(i).field;
    const bool gates_reboot = field.applies == SA::Boot;
    if (field.live.scope != SCP::Global) {
      continue;
    }
    const DocumentValue value = body.value(field.key);
    if (value.missing()) {
      // Explicit JSON null on a present password key clears the secret; an absent key — or
      // null on any non-password key — preserves the stored value (the bool-wipe invariant).
      if (is_password_key(field.key) && value.cleared()) {
        reboot_required |= gates_reboot && (store.getString(field.key, "").length() > 0);
        store.saveString(field.key, "");
        if (std::strcmp(field.key, "PASSWORD") == 0) {
          password = store.getString("PASSWORD", "").c_str();
        }
      }
      continue;
    }
    if (field.live.kind != SR::None) {
      write_live(field, 0, field.type == ST::Bool ? (value.as_bool() ? 1.0 : 0.0) : value.as_number());
    }
    if (field.storage == SS::Volatile) {
      continue;
    }
    switch (field.type) {
      case ST::Bool: {
        const bool new_value = value.as_bool();
        reboot_required |= gates_reboot && (store.getBool(field.key, field.default_int != 0) != new_value);
        store.saveBool(field.key, new_value);
        break;
      }
      case ST::Uint:
      case ST::EnumUint:
      case ST::InterfacePacked: {
        const uint32_t new_value = static_cast<uint32_t>(value.integer);
        reboot_required |=
            gates_reboot && (store.getUInt(field.key, static_cast<uint32_t>(field.default_int)) != new_value);
        store.saveUInt(field.key, new_value);
        break;
      }
      case ST::Int: {
        const int32_t new_value = static_cast<int32_t>(value.integer);
        reboot_required |= gates_reboot && (store.getInt(field.key, field.default_int) != new_value);
        store.saveInt(field.key, new_value);
        break;
      }
      case ST::SecondsToMs: {
        const uint32_t new_value = static_cast<uint32_t>(value.integer) * kMillisecondsPerSecond;
        reboot_required |=
            gates_reboot &&
            (store.getUInt(field.key, static_cast<uint32_t>(field.default_int) * kMillisecondsPerSecond) != new_value);
        store.saveUInt(field.key, new_value);
        break;
      }
      case ST::DeciUnits: {
        const uint32_t new_value =
            static_cast<uint32_t>(std::lround(static_cast<float>(value.as_number()) * kDeciUnitsPerUnit));
        reboot_required |=
            gates_reboot && (store.getUInt(field.key, static_cast<uint32_t>(field.default_int)) != new_value);
        store.saveUInt(field.key, new_value);
        break;
      }
      case ST::SignedDeciUnits: {
        const int32_t new_value =
            static_cast<int32_t>(std::lround(static_cast<float>(value.as_number()) * kDeciUnitsPerUnit));
        reboot_required |= gates_reboot && (store.getInt(field.key, field.default_int) != new_value);
        store.saveInt(field.key, new_value);
        break;
      }
      case ST::Float: {
        const float new_value = static_cast<float>(value.as_number());
        reboot_required |=
            gates_reboot && (store.getFloat(field.key, static_cast<float>(field.default_int)) != new_value);
        store.saveFloat(field.key, new_value);
        break;
      }
      case ST::StringVal:
      case ST::FloatString: {
        const char* new_value = value.as_text();
        if (is_password_key(field.key) && (new_value == nullptr || new_value[0] == '\0')) {
          break;  // blank keeps the stored secret — skip the write and the live-global refresh
        }
        const char* baseline = field.default_str != nullptr ? field.default_str : "";
        reboot_required |= gates_reboot && (!(store.getString(field.key, baseline) == String(new_value)));
        store.saveString(field.key, new_value);
        // SSID/PASSWORD are boot-gated in NVS but the legacy handler also refreshed
        // the live WiFi globals; keep that so a reconnect sees the new credentials.
        if (std::strcmp(field.key, "SSID") == 0) {
          ssid = store.getString("SSID", "").c_str();
        } else if (std::strcmp(field.key, "PASSWORD") == 0) {
          password = store.getString("PASSWORD", "").c_str();
        }
        break;
      }
    }
  }

  if (esp32hal != nullptr) {
    GpioOptionCatalog catalog = esp32hal->gpio_options();
    for (size_t g = 0; g < catalog.group_count; g++) {
      const GpioOptionGroup& group = catalog.groups[g];
      const DocumentValue value = body.value(group.nvs_key);
      if (value.missing()) {
        continue;
      }
      const uint32_t requested = static_cast<uint32_t>(value.integer);
      const uint32_t applied = find_gpio_option_choice(group, requested) != nullptr ? requested : group.default_value;
      reboot_required |= store.getUInt(group.nvs_key, group.default_value) != applied;
      store.saveUInt(group.nvs_key, applied);
    }
  }

  for (size_t row = 0; row < battery_rows; row++) {
    const DocumentRow entry(body, kBatteriesPath, row);
    const int64_t slot = entry.value("slot").integer;
    if (slot < 0 || slot >= kMaxBatterySlots) {
      continue;
    }
    const BatterySlotKeys& keys = kBatterySlotKeys[slot];
    const DocumentValue type_value = entry.value("type");
    if (type_value.is_integer_in(0, UINT32_MAX)) {
      const uint32_t type = static_cast<uint32_t>(type_value.integer);
      reboot_required |= store.getUInt(keys.type_key, static_cast<uint32_t>(BatteryType::None)) != type;
      store.saveUInt(keys.type_key, type);
    }
    const DocumentValue comm_value = entry.value("comm");
    if (comm_value.is_integer_in(0, UINT32_MAX)) {
      const uint32_t comm = static_cast<uint32_t>(comm_value.integer);
      reboot_required |= store.getUInt(keys.comm_key, 0) != comm;
      store.saveUInt(keys.comm_key, comm);
    }
    const DocumentValue contactor_value = entry.value("contactor_control");
    if (contactor_value.is_bool()) {
      const bool contactor = contactor_value.as_bool();
      reboot_required |= store.getBool(keys.contactor_key, false) != contactor;
      store.saveBool(keys.contactor_key, contactor);
    }
  }

  if (esp32hal != nullptr) {
    const DeviceSettingList board = board_settings();
    for (uint8_t s = 0; s < static_cast<uint8_t>(SCP::Highest); s++) {
      const SettingScope scope = static_cast<SettingScope>(s);
      const ScopeEntries entries = esp32hal->scope_entries(scope);
      if (entries.count == 0 || !scope_has_rows(scope)) {
        continue;
      }
      const String path = String(kDynamicPrefix) + scope_name(scope);
      const size_t scoped_rows = body.rows(path.c_str());
      for (size_t row = 0; row < scoped_rows; row++) {
        const DocumentRow entry(body, path.c_str(), row);
        const DocumentValue index_value = entry.value("index");
        if (!index_value.is_integer_in(0, UINT8_MAX)) {
          continue;
        }
        const uint8_t index = static_cast<uint8_t>(index_value.integer);
        // An index the scope does not enumerate would otherwise write a garbage NVS key.
        if (!scope_has_index(entries, index)) {
          continue;
        }
        for (size_t r = 0; r < board.count; r++) {
          const DeviceSetting& board_row = board.data[r];
          if (board_row.field.live.scope != scope ||
              (board_row.applies_to != nullptr && !board_row.applies_to(index))) {
            continue;
          }
          const DocumentValue value = entry.value(board_row.field.key);
          // Omitted or wrong-typed preserves, never wipes (the bool-wipe invariant).
          if (value.missing() || !value_matches_type(board_row.field.type, value)) {
            continue;
          }
          reboot_required |= apply_scoped_value(store, board_row.field, index, value);
        }
      }
    }
  }

  for (size_t row = 0; row < balancing_rows; row++) {
    const DocumentRow entry(body, kBalancingPath, row);
    const uint8_t slot = static_cast<uint8_t>(entry.value("slot").integer);
    for (size_t i = 0; i < total_settings; i++) {
      const SettingField& field = *setting_at(i).field;
      if (field.live.scope != SCP::BatterySlot) {
        continue;
      }
      const DocumentValue value = entry.value(field.key);
      if (!value.missing()) {
        write_live(field, slot, field.type == ST::Bool ? (value.as_bool() ? 1.0 : 0.0) : value.as_number());
      }
    }
  }

  result.changed = store.were_settings_updated();
  result.reboot_required = reboot_required;
  return result;
}

static constexpr float DECI_PER_UNIT = 10.0f;

const char* validate_balancing_field(battery_chemistry_enum chemistry, const char* key,
                                     const DocumentValue& value) {
  if (value.missing()) {
    return nullptr;
  }
  const bool lfp = chemistry == battery_chemistry_enum::LFP;

  if (std::strcmp(key, "max_cell_mv") == 0) {
    if (!value.is_integer_in(0, UINT16_MAX)) {
      return "Bad Request";
    }
    const uint16_t mv = static_cast<uint16_t>(value.integer);
    const uint16_t cell_max_mV = lfp ? BALANCING_CELL_MAX_LFP_MV : BALANCING_CELL_MAX_NCM_MV;
    return (mv < BALANCING_CELL_MIN_MV || mv > cell_max_mV) ? "Cell voltage out of range" : nullptr;
  }
  if (std::strcmp(key, "max_dev_mv") == 0) {
    if (!value.is_integer_in(0, UINT16_MAX)) {
      return "Bad Request";
    }
    const uint16_t mv = static_cast<uint16_t>(value.integer);
    const uint16_t deviation_max_mV = lfp ? BALANCING_DEVIATION_MAX_LFP_MV : BALANCING_DEVIATION_MAX_NCM_MV;
    return (mv < BALANCING_DEVIATION_MIN_MV || mv > deviation_max_mV) ? "Cell deviation out of range" : nullptr;
  }
  if (std::strcmp(key, "float_power_w") == 0) {
    if (!value.is_integer_in(0, UINT16_MAX)) {
      return "Bad Request";
    }
    const uint16_t watts = static_cast<uint16_t>(value.integer);
    return (watts < BALANCING_FLOAT_POWER_MIN_W || watts > BALANCING_FLOAT_POWER_MAX_W) ? "Float power out of range"
                                                                                        : nullptr;
  }
  if (std::strcmp(key, "max_pack_v") == 0) {
    if (!value.is_number()) {
      return "Bad Request";
    }
    const uint16_t pack_max_dV =
        (lfp ? BALANCING_PACK_MAX_LFP_DV : BALANCING_PACK_MAX_NCM_DV) + BALANCING_PACK_HEADROOM_DV;
    const long dv = lroundf(static_cast<float>(value.as_number()) * DECI_PER_UNIT);
    return (dv < BALANCING_PACK_MIN_DV || dv > pack_max_dV) ? "Pack voltage out of range" : nullptr;
  }
  if (std::strcmp(key, "max_time_min") == 0 && !value.is_number()) {
    return "Bad Request";
  }
  return nullptr;
}
