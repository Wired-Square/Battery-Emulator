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
#include "../../inverter/InverterProtocol.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "../hal/hal.h"
#include "../utils/types.h"
#include "../wifi/wifi.h"
#include "web_json.h"

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

constexpr const char* kNetwork = "network";
constexpr const char* kWebauth = "webauth";
constexpr const char* kBattery = "battery";
constexpr const char* kInverter = "inverter";
constexpr const char* kOptional = "optional";
constexpr const char* kHardware = "hardware";
constexpr const char* kConnectivity = "connectivity";
constexpr const char* kDebug = "debug";
constexpr const char* kInterface = "interface";

constexpr double kDeciUnitsPerUnit = 10.0;
constexpr uint32_t kMillisecondsPerSecond = 1000;
constexpr uint32_t kPercentMax = 100;
constexpr int32_t kPptPerPercent = 100;
constexpr int32_t kDeciPerUnit = 10;
constexpr int32_t kMsPerMinute = 60000;
constexpr int32_t kBydAutoCalDriftMin = 1;
constexpr int32_t kBydAutoCalDriftMax = 20;

constexpr const char* kLive = "live";
constexpr const char* kSecChargeLimits = "chargelimits";
constexpr const char* kSecCharger = "charger";
constexpr const char* kSecBydCal = "bydautocal";
constexpr const char* kSecRecovery = "recoverymode";
constexpr const char* kSecCanIdCutoff = "canidcutoff";
constexpr const char* kSecBalancing = "balancing";

void seed_slot_capacities(uint8_t) {
  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    datalayer.battery_slot(slot).info.total_capacity_Wh = datalayer.battery.settings.user_set_total_capacity_Wh;
  }
}

void mirror_keep_iso_disabled(uint8_t) {
  datalayer_extended.bydAtto3_2.keep_iso_disabled = datalayer_extended.bydAtto3.keep_iso_disabled;
}

const char* check_charger_field(const SettingField& field, uint8_t, JsonObjectConst fields) {
  if (charger == nullptr) {
    return "No charger configured";
  }
  if (std::strcmp(field.key, "setpoint_v") == 0) {
    const float volts = fields[field.key].as<float>();
    if (volts < CHARGER_MIN_HV || volts > CHARGER_MAX_HV) {
      return "Invalid value";
    }
  } else if (std::strcmp(field.key, "setpoint_a") == 0) {
    const float amps = fields[field.key].as<float>();
    JsonVariantConst requested_volts = fields["setpoint_v"];
    const float volts =
        requested_volts.isNull() ? datalayer.charger.charger_setpoint_HV_VDC : requested_volts.as<float>();
    if (amps < 0 || amps > CHARGER_MAX_A || amps * kDeciPerUnit > datalayer.battery.settings.max_user_set_charge_dA ||
        amps * volts > CHARGER_MAX_POWER) {
      return "Invalid value";
    }
  }
  return nullptr;
}

const char* check_balancing_field(const SettingField& field, uint8_t slot, JsonObjectConst fields) {
  return validate_balancing_field(datalayer.battery_slot(slot).info.chemistry, field.key, fields[field.key]);
}

struct BatterySlotKeys {
  uint8_t slot;
  const char* type_key;
  const char* comm_key;
  const char* contactor_key;
};
constexpr BatterySlotKeys kBatterySlotKeys[kMaxBatterySlots] = {
    {0, "BATTTYPE", "BATTCOMM", "CNTCTRL"},
    {1, "BATT2TYPE", "BATT2COMM", "CNTCTRLDBL"},
    {2, "BATT3TYPE", "BATT3COMM", "CNTCTRLTRI"},
};
}  // namespace

const SettingField kSettingFields[] = {
    {"WEBAUTH", ST::Bool, kWebauth, SA::Boot, 0, nullptr},
    {"HTTPUSER", ST::StringVal, kWebauth, SA::Boot, 0, "admin"},
    {"HTTPPASS", ST::StringVal, kWebauth, SA::Boot, 0, ""},

    {"WEBUI", ST::StringVal, kInterface, SA::Live, 0, kDefaultUiShell, "webui"},

    {"SSID", ST::StringVal, kNetwork, SA::Boot, 0, ""},
    {"PASSWORD", ST::StringVal, kNetwork, SA::Boot, 0, ""},

    {"BATTCHEM", ST::EnumUint, kBattery, SA::Boot, 1, nullptr, "chemistry"},
    {"BATTPVMAX", ST::FloatX10, kBattery, SA::Boot, 0, nullptr},
    {"BATTPVMIN", ST::FloatX10, kBattery, SA::Boot, 0, nullptr},
    {"BATTCVMAX", ST::Uint, kBattery, SA::Boot, 0, nullptr},
    {"BATTCVMIN", ST::Uint, kBattery, SA::Boot, 0, nullptr},
    {"PYLONBAUD", ST::Uint, kBattery, SA::Boot, 500, nullptr},
    {"INTERLOCKREQ", ST::Bool, kBattery, SA::Boot, 0, nullptr},
    {"SOCESTIMATED", ST::Bool, kBattery, SA::Boot, 0, nullptr},
    {"DALYPWRPCT", ST::Uint, kBattery, SA::Boot, 50, nullptr, nullptr, 1, 10000},
    {"DALYPWRDV", ST::Uint, kBattery, SA::Boot, 50, nullptr, nullptr, 1, 10000},
    {"DALYDVSTART", ST::Uint, kBattery, SA::Boot, 20, nullptr, nullptr, 1, 200},
    {"DALYPWRDEG", ST::Uint, kBattery, SA::Boot, 60, nullptr, nullptr, 1, 10000},
    {"DALYPWR0C", ST::Uint, kBattery, SA::Boot, 800, nullptr, nullptr, 0, 100000},
    {"DIGITALHVIL", ST::Bool, kBattery, SA::Boot, 0, nullptr},
    {"GTWRHD", ST::Bool, kBattery, SA::Boot, kTeslaGtwRightHandDriveDefault, nullptr},
    {"GTWCOUNTRY", ST::EnumUint, kBattery, SA::Boot, kTeslaGtwCountryDefault, nullptr, "country"},
    {"GTWMAPREG", ST::EnumUint, kBattery, SA::Boot, kTeslaGtwMapRegionDefault, nullptr, "mapregion"},
    {"GTWCHASSIS", ST::EnumUint, kBattery, SA::Boot, kTeslaGtwChassisTypeDefault, nullptr, "chassis"},
    {"GTWPACK", ST::EnumUint, kBattery, SA::Boot, kTeslaGtwPackEnergyDefault, nullptr, "pack"},
    {"CHGESTIMATED", ST::Bool, kBattery, SA::Boot, 0, nullptr},
    {"CHGPOWER", ST::Uint, kBattery, SA::Boot, 1000, nullptr, nullptr, 0, 65000},
    {"DCHGPOWER", ST::Uint, kBattery, SA::Boot, 1000, nullptr, nullptr, 0, 65000},
    {"RAMPDOWNSOC", ST::Uint, kBattery, SA::Boot, 9000, nullptr, nullptr, 7000, 9000},
    {"SOFAR_ID", ST::Uint, kBattery, SA::Boot, 0, nullptr, nullptr, 0, 99},

    {"INVTYPE", ST::EnumUint, kInverter, SA::Boot, 0, nullptr, "inverter"},
    {"INVCOMM", ST::InterfacePacked, kInverter, SA::Boot, 0, nullptr},
    {"INVOFFGRID", ST::Bool, kInverter, SA::Boot, 0, nullptr},
    {"LOWPASSFILTER", ST::Bool, kInverter, SA::Boot, 0, nullptr},
    {"CHGTAPERSOC", ST::Bool, kInverter, SA::Boot, 0, nullptr},
    // Stored as start SOC in whole percent; comm_nvm derives the pptt band from it.
    {"CHGTAPERSTART", ST::Uint, kInverter, SA::Boot, 95, nullptr, nullptr, 50, 99},
    {"CHGTAPERFLOOR", ST::Uint, kInverter, SA::Boot, 0, nullptr, nullptr, 0, 2000},
    {"SLOWCANINV", ST::Bool, kInverter, SA::Boot, 0, nullptr},
    {"PYLONSEND", ST::Uint, kInverter, SA::Boot, 0, nullptr},
    {"PYLONOFFSET", ST::Bool, kInverter, SA::Boot, 0, nullptr},
    {"PYLONORDER", ST::Bool, kInverter, SA::Boot, 0, nullptr},
    {"PYLONBRAND", ST::EnumUint, kInverter, SA::Boot, 0, nullptr, "pylonbrand"},
    {"DEYEBYD", ST::Bool, kInverter, SA::Boot, 0, nullptr},
    {"PRIMOGEN24", ST::Bool, kInverter, SA::Boot, 0, nullptr},
    {"INVCELLS", ST::Uint, kInverter, SA::Boot, 0, nullptr},
    {"INVMODULES", ST::Uint, kInverter, SA::Boot, 0, nullptr},
    {"INVCELLSPER", ST::Uint, kInverter, SA::Boot, 0, nullptr},
    {"INVVLEVEL", ST::Uint, kInverter, SA::Boot, 0, nullptr},
    {"INVCAPACITY", ST::Uint, kInverter, SA::Boot, 0, nullptr},
    {"INVBTYPE", ST::Uint, kInverter, SA::Boot, 0, nullptr},
    {"INVSUNTYPE", ST::EnumUint, kInverter, SA::Boot, 0, nullptr, "sungrow"},
    {"INVICNT", ST::EnumUint, kInverter, SA::Boot, 0, nullptr, "contactor"},
    {"FOXESSTYPE", ST::Uint, kInverter, SA::Boot, 0, nullptr},
    {"FOXESSSUBTYPE", ST::Uint, kInverter, SA::Boot, 0, nullptr},
    {"FOXESSMODULES", ST::Uint, kInverter, SA::Boot, 0, nullptr},

    // CTOFFSET is a string to keep its sign; CTATTEN uses the boot default 3, not the
    // form's 0, so an unsaved device's full-set POST leaves the attenuation unchanged.
    {"CHGTYPE", ST::EnumUint, kOptional, SA::Boot, 0, nullptr, "charger"},
    {"CHGCOMM", ST::InterfacePacked, kOptional, SA::Boot, 0, nullptr},
    {"SHUNTTYPE", ST::EnumUint, kOptional, SA::Boot, 0, nullptr, "shunt"},
    {"SHUNTCOMM", ST::InterfacePacked, kOptional, SA::Boot, 0, nullptr},
    {"CTOFFSET", ST::FloatString, kOptional, SA::Boot, 0, "-1.0"},
    {"CTVNOM", ST::Uint, kOptional, SA::Boot, 40, nullptr, nullptr, 0, 500},
    {"CTANOM", ST::Uint, kOptional, SA::Boot, 100, nullptr, nullptr, 0, 200},
    {"CTATTEN", ST::EnumUint, kOptional, SA::Boot, 3, nullptr, "attenuation"},
    {"CTINVERT", ST::Bool, kOptional, SA::Boot, 0, nullptr},

    // Board-gated rows sit inside the same #ifdef that gates them in comm_nvm.cpp.
    {"CANFDASCAN", ST::Bool, kHardware, SA::Boot, 0, nullptr},
#if defined(HW_LILYGO2CAN) || defined(HW_STARK)
    {"CANFD2ASCAN", ST::Bool, kHardware, SA::Boot, 0, nullptr},
#endif
    {"EQSTOP", ST::EnumUint, kHardware, SA::Boot, 0, nullptr, "button"},
    {"PRECHGMS", ST::Uint, kHardware, SA::Boot, 100, nullptr, nullptr, 1, 65000},
    {"NCCONTACTOR", ST::Bool, kHardware, SA::Boot, 0, nullptr},
    {"PWMCNTCTRL", ST::Bool, kHardware, SA::Boot, 0, nullptr},
    {"PWMFREQ", ST::Uint, kHardware, SA::Boot, 20000, nullptr, nullptr, 1, 65000},
    {"PWMHOLD", ST::Uint, kHardware, SA::Boot, 250, nullptr, nullptr, 1, 1023},
    {"PERBMSRESET", ST::Bool, kHardware, SA::Boot, 0, nullptr},
    {"PERBMSRESETH", ST::EnumUint, kHardware, SA::Boot, 24, nullptr, "bmsresetinterval"},
    {"PERBMSDEFSOC", ST::Bool, kHardware, SA::Boot, 0, nullptr},
    {"PERBMSSKIPBAL", ST::Bool, kHardware, SA::Boot, 0, nullptr},
    {"EXTPRECHARGE", ST::Bool, kHardware, SA::Boot, 0, nullptr},
    {"MAXPRETIME", ST::Uint, kHardware, SA::Boot, 15000, nullptr},
    {"MAXPREFREQ", ST::Uint, kHardware, SA::Boot, 34000, nullptr},
    {"NOINVDISC", ST::Bool, kHardware, SA::Boot, 0, nullptr},
    {"LEDMODE", ST::EnumUint, kHardware, SA::Boot, 0, nullptr, "ledmode"},

    {"WIFIAPENABLED", ST::Bool, kConnectivity, SA::Boot, 1, nullptr},
    {"APPASSWORD", ST::StringVal, kConnectivity, SA::Boot, 0, "123456789"},
    {"WIFICHANNEL", ST::Uint, kConnectivity, SA::Boot, 0, nullptr, nullptr, 0, 14},
    {"HOSTNAME", ST::StringVal, kConnectivity, SA::Boot, 0, ""},
    {"STATICIP", ST::Bool, kConnectivity, SA::Boot, 0, nullptr},
    {"LOCALIP", ST::StringVal, kConnectivity, SA::Boot, 0, ""},
    {"GATEWAY", ST::StringVal, kConnectivity, SA::Boot, 0, ""},
    {"SUBNET", ST::StringVal, kConnectivity, SA::Boot, 0, ""},
    {"DNS", ST::StringVal, kConnectivity, SA::Boot, 0, ""},
    {"ESPNOWENABLED", ST::Bool, kConnectivity, SA::Boot, 0, nullptr},
    {"ESPNOWMACS", ST::StringVal, kConnectivity, SA::Boot, 0, "", nullptr, kNoMin, 180},
    {"MQTTENABLED", ST::Bool, kConnectivity, SA::Boot, 0, nullptr},
    {"MQTTSERVER", ST::StringVal, kConnectivity, SA::Boot, 0, ""},
    {"MQTTPORT", ST::Uint, kConnectivity, SA::Boot, 1883, nullptr, nullptr, 1, 65535},
    {"MQTTUSER", ST::StringVal, kConnectivity, SA::Boot, 0, ""},
    {"MQTTPASSWORD", ST::StringVal, kConnectivity, SA::Boot, 0, ""},
    {"MQTTTIMEOUT", ST::Uint, kConnectivity, SA::Boot, 2000, nullptr, nullptr, 1, 60000},
    {"MQTTPUBLISHMS", ST::SecondsToMs, kConnectivity, SA::Boot, 5, nullptr, nullptr, 1, 300},
    {"MQTTCELLV", ST::Bool, kConnectivity, SA::Boot, 0, nullptr},
    {"MQTTHEAP", ST::Bool, kConnectivity, SA::Boot, 0, nullptr},
    {"REMBMSRESET", ST::Bool, kConnectivity, SA::Boot, 0, nullptr},
    {"HADISC", ST::Bool, kConnectivity, SA::Boot, 0, nullptr},
    {"HADISCFWU", ST::Bool, kConnectivity, SA::Boot, 0, nullptr},
    {"HADISCTOPIC", ST::StringVal, kConnectivity, SA::Boot, 0, "homeassistant"},

    {"PERFPROFILE", ST::Bool, kDebug, SA::Boot, 0, nullptr},
    {"MEASURECPUTEMP", ST::Bool, kDebug, SA::Boot, 0, nullptr},
    {"CPUTEMPOFFSET", ST::Int, kDebug, SA::Boot, 0, nullptr},
    {"CANLOGUSB", ST::Bool, kDebug, SA::Boot, 0, nullptr},
    {"USBENABLED", ST::Bool, kDebug, SA::Boot, 0, nullptr},
    {"WEBENABLED", ST::Bool, kDebug, SA::Boot, 0, nullptr},
    {"CANLOGSD", ST::Bool, kDebug, SA::Boot, 0, nullptr},
    {"SDLOGENABLED", ST::Bool, kDebug, SA::Boot, 0, nullptr},
#ifndef SMALL_FLASH_DEVICE
    // Gate matches the comm_nvm reads and the syslog client.
    {"SYSLOGEN", ST::Bool, kDebug, SA::Boot, 0, nullptr},
    {"SYSLOGIP", ST::StringVal, kDebug, SA::Boot, 0, ""},
    {"SYSLOGPORT", ST::Uint, kDebug, SA::Boot, 514, nullptr, nullptr, 1, 65535},
    {"SYSLOGFAC", ST::Uint, kDebug, SA::Boot, 1, nullptr, nullptr, 0, 23},
#endif

    {"BATTERY_WH_MAX",
     ST::Uint,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecChargeLimits,
     {SR::U32, [](uint8_t) -> void* { return &datalayer.battery.settings.user_set_total_capacity_Wh; }, 1, SCP::Global,
      seed_slot_capacities}},
    {"USE_SCALED_SOC",
     ST::Bool,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecChargeLimits,
     {SR::Bool, [](uint8_t) -> void* { return &datalayer.battery.settings.soc_scaling_active; }}},
    {"MAXPERCENTAGE",
     ST::FloatX10,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecChargeLimits,
     {SR::U16, [](uint8_t) -> void* { return &datalayer.battery.settings.max_percentage; }, kPptPerPercent}},
    {"MINPERCENTAGE",
     ST::SignedFloatX10,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecChargeLimits,
     {SR::I16, [](uint8_t) -> void* { return &datalayer.battery.settings.min_percentage; }, kPptPerPercent}},
    {"MAXCHARGEAMP",
     ST::FloatX10,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecChargeLimits,
     {SR::U16, [](uint8_t) -> void* { return &datalayer.battery.settings.max_user_set_charge_dA; }, kDeciPerUnit}},
    {"MAXDISCHARGEAMP",
     ST::FloatX10,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecChargeLimits,
     {SR::U16, [](uint8_t) -> void* { return &datalayer.battery.settings.max_user_set_discharge_dA; }, kDeciPerUnit}},
    {"USEVOLTLIMITS",
     ST::Bool,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecChargeLimits,
     {SR::Bool, [](uint8_t) -> void* { return &datalayer.battery.settings.user_set_voltage_limits_active; }}},
    {"TARGETCHVOLT",
     ST::FloatX10,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecChargeLimits,
     {SR::U16, [](uint8_t) -> void* { return &datalayer.battery.settings.max_user_set_charge_voltage_dV; },
      kDeciPerUnit}},
    {"TARGETDISCHVOLT",
     ST::FloatX10,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecChargeLimits,
     {SR::U16, [](uint8_t) -> void* { return &datalayer.battery.settings.max_user_set_discharge_voltage_dV; },
      kDeciPerUnit}},
    {"BMSRESETDUR",
     ST::SecondsToMs,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecChargeLimits,
     {SR::U32, [](uint8_t) -> void* { return &datalayer.battery.settings.user_set_bms_reset_duration_ms; },
      kMillisecondsPerSecond}},

    {"hv_enabled",
     ST::Bool,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecCharger,
     {SR::Bool, [](uint8_t) -> void* { return &datalayer.charger.charger_HV_enabled; }, 1, SCP::Global, nullptr,
      check_charger_field}},
    {"aux12v_enabled",
     ST::Bool,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecCharger,
     {SR::Bool, [](uint8_t) -> void* { return &datalayer.charger.charger_aux12V_enabled; }, 1, SCP::Global, nullptr,
      check_charger_field}},
    {"setpoint_v",
     ST::Float,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecCharger,
     {SR::F32, [](uint8_t) -> void* { return &datalayer.charger.charger_setpoint_HV_VDC; }, 1, SCP::Global, nullptr,
      check_charger_field}},
    {"setpoint_a",
     ST::Float,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecCharger,
     {SR::F32, [](uint8_t) -> void* { return &datalayer.charger.charger_setpoint_HV_IDC; }, 1, SCP::Global, nullptr,
      check_charger_field}},
    {"end_a",
     ST::Float,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecCharger,
     {SR::F32, [](uint8_t) -> void* { return &datalayer.charger.charger_setpoint_HV_IDC_END; }, 1, SCP::Global, nullptr,
      check_charger_field}},

    {"BYDAUTOCALEN",
     ST::Bool,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecBydCal,
     {SR::Bool, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3.auto_calibrate_soc_enabled; }}},
    {"BYDAUTOCALDRIFT",
     ST::Uint,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kBydAutoCalDriftMin,
     kBydAutoCalDriftMax,
     SS::Nvs,
     kSecBydCal,
     {SR::U8, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3.auto_calibrate_soc_drift_percent; }}},
    {"BYDAUTOCALEN2",
     ST::Bool,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecBydCal,
     {SR::Bool, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3_2.auto_calibrate_soc_enabled; }}},
    {"BYDAUTOCALDRFT2",
     ST::Uint,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kBydAutoCalDriftMin,
     kBydAutoCalDriftMax,
     SS::Nvs,
     kSecBydCal,
     {SR::U8, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3_2.auto_calibrate_soc_drift_percent; }}},
    {"BYDKEEPISOOFF",
     ST::Bool,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Nvs,
     kSecBydCal,
     {SR::Bool, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3.keep_iso_disabled; }, 1, SCP::Global,
      mirror_keep_iso_disabled}},
    {"cal_target_soc",
     ST::Uint,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecBydCal,
     {SR::U16, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3.calibrationTargetSOC; }}},
    {"cal_target_ah",
     ST::Uint,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecBydCal,
     {SR::U16, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3.calibrationTargetAH; }}},
    {"cal_target_soc2",
     ST::Uint,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecBydCal,
     {SR::U16, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3_2.calibrationTargetSOC; }}},
    {"cal_target_ah2",
     ST::Uint,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecBydCal,
     {SR::U16, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3_2.calibrationTargetAH; }}},

    {"recovery_mode",
     ST::Bool,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecRecovery,
     {SR::Bool,
      [](uint8_t) -> void* { return &datalayer.battery.settings.user_requests_forced_charging_recovery_mode; }}},

    {"cutoff",
     ST::Uint,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecCanIdCutoff,
     {SR::U16, [](uint8_t) -> void* { return &user_selected_CAN_ID_cutoff_filter; }}},

    {"max_time_min",
     ST::Float,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecBalancing,
     {SR::U32, [](uint8_t slot) -> void* { return &datalayer.battery_slot(slot).settings.balancing_max_time_ms; },
      kMsPerMinute, SCP::BatterySlot, nullptr, check_balancing_field}},
    {"max_cell_mv",
     ST::Uint,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecBalancing,
     {SR::U16,
      [](uint8_t slot) -> void* { return &datalayer.battery_slot(slot).settings.balancing_max_cell_voltage_mV; }, 1,
      SCP::BatterySlot, nullptr, check_balancing_field}},
    {"max_dev_mv",
     ST::Uint,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecBalancing,
     {SR::U16,
      [](uint8_t slot)
          -> void* { return &datalayer.battery_slot(slot).settings.balancing_max_deviation_cell_voltage_mV; },
      1, SCP::BatterySlot, nullptr, check_balancing_field}},
    {"max_pack_v",
     ST::Float,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecBalancing,
     {SR::U16,
      [](uint8_t slot) -> void* { return &datalayer.battery_slot(slot).settings.balancing_max_pack_voltage_dV; },
      kDeciPerUnit, SCP::BatterySlot, nullptr, check_balancing_field}},
    {"float_power_w",
     ST::Uint,
     kLive,
     SA::Live,
     0,
     nullptr,
     nullptr,
     kNoMin,
     kNoMax,
     SS::Volatile,
     kSecBalancing,
     {SR::U16, [](uint8_t slot) -> void* { return &datalayer.battery_slot(slot).settings.balancing_float_power_W; }, 1,
      SCP::BatterySlot, nullptr, check_balancing_field}},
};

const size_t kSettingFieldCount = sizeof(kSettingFields) / sizeof(kSettingFields[0]);

namespace {
// The stored password keys (mirrors the client PASSWORD_KEYS set). HTTPPASSCONFIRM is
// client-synthesised, not stored, so it is deliberately excluded.
bool is_password_key(const char* nvs_key) {
  return std::strcmp(nvs_key, "HTTPPASS") == 0 || std::strcmp(nvs_key, "PASSWORD") == 0 ||
         std::strcmp(nvs_key, "APPASSWORD") == 0 || std::strcmp(nvs_key, "MQTTPASSWORD") == 0;
}

// ArduinoJson links (does not copy) a bare const char*, so a computed String
// must be copied into the document. On device it recognises Arduino String; on
// the host build it does not, so route through std::string, which it copies.
void set_json_string(JsonObject obj, const char* key, const String& value) {
#if ARDUINOJSON_ENABLE_ARDUINO_STRING
  obj[key] = value;
#else
  obj[key] = std::string(value.c_str());
#endif
}

// Mirrors options_for_enum_with_none: none first, then labels ascending.
// strcmp on the producers' static literals reproduces String's order without
// the host emul String, which has no operator<.
template <typename TEnum, typename Producer>
void emit_enum_options(JsonObject options, const char* key, Producer name_for_type, const TEnum* none = nullptr) {
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

  JsonArray arr = options[key].to<JsonArray>();
  for (const auto& [name, value] : pairs) {
    JsonObject opt = arr.add<JsonObject>();
    opt["v"] = static_cast<int>(value);
    opt["n"] = name;
  }
}

void emit_map_options(JsonObject options, const char* key, const std::map<int, String>& value_names) {
  JsonArray arr = options[key].to<JsonArray>();
  for (const auto& [value, name] : value_names) {
    JsonObject opt = arr.add<JsonObject>();
    opt["v"] = value;
    set_json_string(opt, "n", name);
  }
}

// Parity with emit_enum_options: visible (non-null-label) choices, alphabetical
// by label. Keyed by nvs_key so the schema row's options reference matches.
// Emit-by-scan over the handful of choices, ordered by (label, address); keeps
// std::sort/std::vector out of every board, including option-less ones that
// never reclaim the code.
void emit_gpio_option_choices(JsonObject options, const GpioOptionGroup& group) {
  JsonArray arr = options[group.nvs_key].to<JsonArray>();
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
    JsonObject opt = arr.add<JsonObject>();
    opt["v"] = static_cast<int>(next->value);
    opt["n"] = next->label;
    prev = next;
  }
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
    case ST::FloatX10:
    case ST::SignedFloatX10:
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

bool value_matches_option(SettingType type, JsonVariantConst value, JsonVariantConst option) {
  if (type == ST::StringVal) {
    return strcmp(value.as<const char*>(), option.as<const char*>()) == 0;
  }
  return value.as<uint32_t>() == option.as<uint32_t>();
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
    field.live.on_apply(slot);
  }
}

void emit_live_value(JsonObject out, const SettingField& field, uint8_t slot) {
  const double value = read_live(field, slot) / field.live.ram_per_json;
  switch (field.type) {
    case ST::Bool:
      out[field.key] = value != 0.0;
      break;
    case ST::FloatX10:
    case ST::SignedFloatX10:
    case ST::Float:
      out[field.key] = value;
      break;
    default:
      out[field.key] = static_cast<int64_t>(std::llround(value));
      break;
  }
}

void emit_asset_name_options(JsonObject options, const char* options_key, AssetNameSpec spec) {
  JsonArray names = options[options_key].to<JsonArray>();
  const WebAssetTable table = default_web_asset_table();
  const size_t total = web_asset_name_count(table, spec);
  for (size_t i = 0; i < total; i++) {
    char name[kMaxAssetNameLen + 1];
    if (!web_asset_name_at(table, spec, i, name, sizeof(name))) {
      continue;
    }
    names.add<JsonObject>()["v"] = name;
  }
}

void emit_all_options(JsonObject options) {
  const BatteryType battery_none = BatteryType::None;
  emit_enum_options(
      options, "battery",
      [](BatteryType t) { return board_supports_battery_type(t) ? name_for_battery_type(t) : nullptr; }, &battery_none);
  JsonArray battery_options = options["battery"].as<JsonArray>();
  for (JsonObject opt : battery_options) {
    const BatteryType type = static_cast<BatteryType>(opt["v"].as<uint32_t>());
    if (type != BatteryType::None) {
      opt["s"] = battery_type_allowed_in_slot(type, 2) ? 3 : battery_type_allowed_in_slot(type, 1) ? 2 : 1;
    }
  }
  emit_enum_options<battery_chemistry_enum>(options, "chemistry", name_for_chemistry);
  const InverterProtocolType inverter_none = InverterProtocolType::None;
  emit_enum_options(options, "inverter", name_for_inverter_type, &inverter_none);
  const ChargerType charger_none = ChargerType::None;
  emit_enum_options(options, "charger", name_for_charger_type, &charger_none);
  const ShuntType shunt_none = ShuntType::None;
  emit_enum_options(options, "shunt", name_for_shunt_type, &shunt_none);
  const adc_attenuation_enum attenuation_none = adc_attenuation_enum::ADC_0db;
  emit_enum_options(options, "attenuation", name_for_adc_attenuation, &attenuation_none);
  const STOP_BUTTON_BEHAVIOR button_none = STOP_BUTTON_BEHAVIOR::NOT_CONNECTED;
  emit_enum_options(options, "button", name_for_button_type, &button_none);

  if (esp32hal != nullptr) {
    GpioOptionCatalog catalog = esp32hal->gpio_options();
    for (size_t g = 0; g < catalog.group_count; g++) {
      emit_gpio_option_choices(options, catalog.groups[g]);
    }
  }

  // Board-conditional (HW_LILYGO2CAN adds GRB variants), so kept server-side.
  emit_map_options(options, "ledmode", led_modes);
  emit_map_options(options, "bmsresetinterval", bms_reset_intervals);

#ifdef BOARD_HAS_LOAD_SWITCH
  // Enum order (Disabled first, unsorted), matching the legacy per-channel select.
  JsonArray roles = options["loadswitchrole"].to<JsonArray>();
  for (uint32_t r = 0; r < static_cast<uint32_t>(LoadSwitchRole::Highest); r++) {
    const char* name = name_for_load_switch_role(static_cast<LoadSwitchRole>(r));
    if (name == nullptr || name[0] == '\0') {
      continue;
    }
    JsonObject opt = roles.add<JsonObject>();
    opt["v"] = r;
    opt["n"] = name;
  }
#endif

  emit_asset_name_options(options, "webui", kUiShellSpec);
}

}  // namespace

String build_settings_json(BatteryEmulatorSettingsStore& store, bool reboot_required) {
  JsonDocument doc;
  JsonObject values = doc["values"].to<JsonObject>();

  for (size_t i = 0; i < kSettingFieldCount; i++) {
    const SettingField& field = kSettingFields[i];
    if (field.live.kind != SR::None) {
      if (field.live.scope == SCP::Global) {
        emit_live_value(values, field, 0);
      }
      continue;
    }
    switch (field.type) {
      case ST::Bool:
        values[field.key] = store.getBool(field.key, field.default_int != 0);
        break;
      case ST::Uint:
      case ST::EnumUint:
        values[field.key] = store.getUInt(field.key, static_cast<uint32_t>(field.default_int));
        break;
      case ST::Int:
        values[field.key] = store.getInt(field.key, field.default_int);
        break;
      case ST::StringVal:
      case ST::FloatString:
        set_json_string(values, field.key,
                        is_password_key(field.key) ? String("") : store.getString(field.key, field.default_str));
        break;
      case ST::FloatX10:
        values[field.key] = store.getUInt(field.key, static_cast<uint32_t>(field.default_int)) / kDeciUnitsPerUnit;
        break;
      case ST::SignedFloatX10:
        values[field.key] = store.getInt(field.key, field.default_int) / kDeciUnitsPerUnit;
        break;
      case ST::Float:
        break;
      case ST::SecondsToMs:
        values[field.key] =
            store.getUInt(field.key, static_cast<uint32_t>(field.default_int) * kMillisecondsPerSecond) /
            kMillisecondsPerSecond;
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
        values[field.key] = stored;
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
      values[group.nvs_key] = find_gpio_option_choice(group, stored) != nullptr ? stored : group.default_value;
    }
  }

  emit_all_options(doc["options"].to<JsonObject>());

  // Presentation (labels, visibility) lives client-side; schema carries only identity
  // and widget wiring.
  JsonArray schema = doc["schema"].to<JsonArray>();
  for (size_t i = 0; i < kSettingFieldCount; i++) {
    const SettingField& field = kSettingFields[i];
    JsonObject entry = schema.add<JsonObject>();
    entry["key"] = field.key;
    entry["category"] = field.category;
    entry["type"] = widget_type_name(field.type);
    const char* options = field.options_key;
    if (options == nullptr && field.type == ST::InterfacePacked) {
      options = "interfaces";
    }
    entry["options"] = options;  // null const char* serialises as JSON null
    entry["section"] = field.section;
    if (field.live.scope == SCP::BatterySlot) {
      entry["scope"] = "battery";
    }
    if (field.min_value != kNoMin) {
      entry["min"] = field.min_value;
    }
    if (field.max_value != kNoMax) {
      entry["max"] = field.max_value;
    }
  }

  if (esp32hal != nullptr) {
    GpioOptionCatalog catalog = esp32hal->gpio_options();
    for (size_t g = 0; g < catalog.group_count; g++) {
      const GpioOptionGroup& group = catalog.groups[g];
      JsonObject entry = schema.add<JsonObject>();
      entry["key"] = group.nvs_key;
      entry["category"] = kHardware;
      entry["type"] = widget_type_name(ST::EnumUint);
      entry["options"] = group.nvs_key;
      entry["label"] = group.label;
    }
  }

  // Hint shown when a field is left blank. Unlike a static default_str, the
  // hostname default is MAC-derived and known only at runtime.
  JsonObject placeholders = doc["placeholders"].to<JsonObject>();
  set_json_string(placeholders, "HOSTNAME", default_hostname());

  JsonArray battery_slots = doc["dynamic"]["batteries"].to<JsonArray>();
  for (const BatterySlotKeys& keys : kBatterySlotKeys) {
    JsonObject slot = battery_slots.add<JsonObject>();
    slot["slot"] = keys.slot;
    slot["type"] = store.getUInt(keys.type_key, static_cast<uint32_t>(BatteryType::None));
    uint32_t comm = store.getUInt(keys.comm_key, 0);
    if (esp32hal != nullptr) {
      InterfaceList iface_list = esp32hal->interfaces();
      if (resolve_interface_config(iface_list, comm) == nullptr) {
        comm = default_interface_config(iface_list);
      }
    }
    slot["comm"] = comm;
    slot["contactor_control"] = store.getBool(keys.contactor_key, false);
  }

  JsonArray balancing_slots = doc["dynamic"]["balancing"].to<JsonArray>();
  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    if (!battery_slot_addressable(slot)) {
      continue;
    }
    JsonObject entry = balancing_slots.add<JsonObject>();
    entry["slot"] = slot;
    for (size_t i = 0; i < kSettingFieldCount; i++) {
      const SettingField& field = kSettingFields[i];
      if (field.live.scope == SCP::BatterySlot) {
        emit_live_value(entry, field, slot);
      }
    }
  }

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
    JsonArray interfaces = doc["interfaces"].to<JsonArray>();
    for (const IfaceEntry& entry : entries) {
      JsonObject iface = interfaces.add<JsonObject>();
      iface["index"] = entry.index;
      iface["id"] = entry.id;
      iface["name"] = entry.name;
    }

#ifdef BOARD_HAS_INTERFACE_TERMINATION
    JsonArray termination = doc["dynamic"]["termination"].to<JsonArray>();
    for (size_t i = 0; i < list.count; i++) {
      if (!esp32hal->supports_interface_termination(i)) {
        continue;
      }
      JsonObject entry = termination.add<JsonObject>();
      entry["index"] = i;
      entry["name"] = descriptor_name(list.data[i]);
      entry["enabled"] = store.getBool(interface_termination_key(i).c_str(), false);
    }
#endif

#ifdef BOARD_HAS_LOAD_SWITCH
    if (LoadSwitch* load_switch = esp32hal->load_switch()) {
      JsonObject dynamic_load = doc["dynamic"]["loadswitch"].to<JsonObject>();
      JsonArray channels = dynamic_load["channels"].to<JsonArray>();
      for (uint8_t ch = 0; ch < kLoadSwitchConfigChannels; ch++) {
        // LSDUTY persists raw 10-bit duty; the client works in percent.
        uint32_t duty = store.getUInt(load_switch_duty_key(ch).c_str(), kLoadSwitchDutyMax);
        JsonObject channel = channels.add<JsonObject>();
        channel["channel"] = ch;
        channel["role"] =
            store.getUInt(load_switch_role_key(ch).c_str(), static_cast<uint32_t>(LoadSwitchRole::Disabled));
        channel["duty"] = (duty * 100 + kLoadSwitchDutyMax / 2) / kLoadSwitchDutyMax;
        channel["divisor"] = store.getUInt(load_switch_divisor_key(ch).c_str(), 0);
      }
      JsonArray divisors = dynamic_load["divisors"].to<JsonArray>();
      for (uint8_t d = 0; d < kLoadSwitchDivisorCodes; d++) {
        String label = String("÷") + String(load_switch_divisor_ratio(d)) + " (" +
                       String(load_switch_pwm_frequency_hz(load_switch->pwm_clock_hz(), d)) + " Hz)";
        JsonObject opt = divisors.add<JsonObject>();
        opt["v"] = d;
        set_json_string(opt, "n", label);
      }
    }
#endif
  }

  doc["meta"]["reboot_required"] = reboot_required;

  // Overflow means truncated JSON; the empty String tells the route to answer 500.
  if (doc.overflowed()) {
    return String();
  }

#if ARDUINOJSON_ENABLE_ARDUINO_STRING
  String out;
  serializeJson(doc, out);
  return out;
#else
  std::string out;
  serializeJson(doc, out);
  return String(out);
#endif
}

namespace {
bool value_matches_type(SettingType type, JsonVariantConst value) {
  switch (type) {
    case ST::Bool:
      return value.is<bool>();
    case ST::Uint:
    case ST::EnumUint:
    case ST::InterfacePacked:
    case ST::SecondsToMs:
      return value.is<uint32_t>();
    case ST::Int:
      return value.is<int32_t>();
    case ST::StringVal:
    case ST::FloatString:
      return value.is<const char*>();
    case ST::FloatX10:
    case ST::SignedFloatX10:
    case ST::Float:
      return value.is<float>();
  }
  return false;
}
}  // namespace

SettingsApplyResult apply_settings_json(BatteryEmulatorSettingsStore& store, JsonObjectConst root) {
  SettingsApplyResult result{true, String(), false, false};
  JsonObjectConst values = root["values"];

  // Guard on the effective webauth state, not just whether this payload enables it: otherwise a
  // request that clears HTTPPASS while omitting WEBAUTH would leave auth on with no password (lockout).
  const bool webauth_effective =
      values["WEBAUTH"].is<bool>() ? values["WEBAUTH"].as<bool>() : store.getBool("WEBAUTH", false);
  const String http_user = values["HTTPUSER"].is<const char*>() ? String(values["HTTPUSER"].as<const char*>())
                                                                : store.getString("HTTPUSER", "admin");
  // Blank keeps the stored password; an explicit null clears it. Resolve to "" on a clear so the
  // webauth guard below treats a cleared password as "none".
  const bool http_pass_clear = values["HTTPPASS"].isNull() && values.containsKey("HTTPPASS");
  const bool http_pass_present =
      values["HTTPPASS"].is<const char*>() && std::strlen(values["HTTPPASS"].as<const char*>()) > 0;
  const String http_pass = http_pass_clear     ? String("")
                           : http_pass_present ? String(values["HTTPPASS"].as<const char*>())
                                               : store.getString("HTTPPASS");
  const bool http_pass_confirm_present =
      values["HTTPPASSCONFIRM"].is<const char*>() && std::strlen(values["HTTPPASSCONFIRM"].as<const char*>()) > 0;
  const String http_pass_confirm =
      http_pass_confirm_present ? String(values["HTTPPASSCONFIRM"].as<const char*>()) : http_pass;
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
  JsonDocument options_doc;
  JsonObject all_options = options_doc.to<JsonObject>();
  bool options_built = false;
  for (size_t i = 0; i < kSettingFieldCount; i++) {
    const SettingField& field = kSettingFields[i];
    if (field.live.scope == SCP::BatterySlot) {
      continue;
    }
    JsonVariantConst value = values[field.key];
    if (value.isNull()) {
      continue;
    }
    if (!value_matches_type(field.type, value)) {
      result.ok = false;
      result.error = String("Invalid type for setting ") + field.key;
      result.error_key = "error.setting_invalid_type";
      result.error_arg = field.key;
      return result;
    }
    // as<double>() is safe: bounds sit only on numeric rows, already type-checked above.
    const bool has_min = field.min_value != kNoMin;
    const bool has_max = field.max_value != kNoMax;
    if (has_min || has_max) {
      const double numeric = value.as<double>();
      if ((has_min && numeric < field.min_value) || (has_max && numeric > field.max_value)) {
        result.ok = false;
        result.error = String("Setting ") + field.key + " is out of range";
        result.error_key = "error.setting_out_of_range";
        result.error_arg = field.key;
        return result;
      }
    }
    if (field.options_key != nullptr) {
      if (!options_built) {
        emit_all_options(all_options);
        options_built = true;
      }
      JsonArrayConst choices = all_options[field.options_key].as<JsonArrayConst>();
      // Some pick-lists are static enough to live in the client instead of costing
      // flash here. Membership is only checkable against a list this firmware
      // publishes; an absent one is client-owned, not an empty list of choices.
      if (!choices.isNull()) {
        bool found = false;
        for (JsonObjectConst opt : choices) {
          if (value_matches_option(field.type, value, opt["v"])) {
            found = true;
            break;
          }
        }
        if (!found) {
          result.ok = false;
          result.error = String("Setting ") + field.key + " is not an available option";
          result.error_key = "error.setting_not_an_option";
          result.error_arg = field.key;
          return result;
        }
      }
    }
    if (field.live.check != nullptr) {
      if (const char* error = field.live.check(field, 0, values)) {
        result.ok = false;
        result.error = error;
        return result;
      }
    }
  }

  for (JsonObjectConst entry : root["dynamic"]["balancing"].as<JsonArrayConst>()) {
    if (!entry["slot"].is<uint8_t>() || !battery_slot_addressable(entry["slot"].as<uint8_t>())) {
      result.ok = false;
      result.error = "Unknown battery slot";
      result.error_key = "error.battery_slot_unknown";
      return result;
    }
    const uint8_t slot = entry["slot"].as<uint8_t>();
    for (size_t i = 0; i < kSettingFieldCount; i++) {
      const SettingField& field = kSettingFields[i];
      if (field.live.scope != SCP::BatterySlot) {
        continue;
      }
      JsonVariantConst value = entry[field.key];
      if (value.isNull()) {
        continue;
      }
      if (!value_matches_type(field.type, value)) {
        result.ok = false;
        result.error = String("Invalid type for setting ") + field.key;
        result.error_key = "error.setting_invalid_type";
        result.error_arg = field.key;
        return result;
      }
      if (field.live.check != nullptr) {
        if (const char* error = field.live.check(field, slot, entry)) {
          result.ok = false;
          result.error = error;
          return result;
        }
      }
    }
  }

  JsonArrayConst battery_section = root["dynamic"]["batteries"].as<JsonArrayConst>();
  BatteryType effective_types[kMaxBatterySlots];
  for (const BatterySlotKeys& keys : kBatterySlotKeys) {
    effective_types[keys.slot] =
        static_cast<BatteryType>(store.getUInt(keys.type_key, static_cast<uint32_t>(BatteryType::None)));
  }
  for (JsonObjectConst entry : battery_section) {
    if (!entry["slot"].is<uint8_t>() || entry["slot"].as<uint8_t>() >= kMaxBatterySlots) {
      result.ok = false;
      result.error = "Unknown battery slot";
      result.error_key = "error.battery_slot_unknown";
      return result;
    }
    const uint8_t slot = entry["slot"].as<uint8_t>();
    JsonVariantConst type_value = entry["type"];
    if (!type_value.isNull()) {
      if (!type_value.is<uint32_t>()) {
        result.ok = false;
        result.error = "Invalid type for battery slot";
        result.error_key = "error.battery_slot_invalid_type";
        return result;
      }
      const BatteryType type = static_cast<BatteryType>(type_value.as<uint32_t>());
      if (!battery_type_allowed_in_slot(type, slot)) {
        result.ok = false;
        result.error = String("Battery ") + (slot + 1) + " cannot run the selected battery type on this hardware";
        result.error_key = "error.battery_type_unsupported";
        result.error_arg = String(slot + 1);
        return result;
      }
      effective_types[slot] = type;
    }
    if (!entry["comm"].isNull() && !entry["comm"].is<uint32_t>()) {
      result.ok = false;
      result.error = "Invalid interface for battery slot";
      result.error_key = "error.battery_slot_invalid_interface";
      return result;
    }
    if (!entry["contactor_control"].isNull() && !entry["contactor_control"].is<bool>()) {
      result.ok = false;
      result.error = "Invalid contactor control value for battery slot";
      result.error_key = "error.battery_slot_invalid_contactor";
      return result;
    }
  }
  if (!battery_section.isNull() && effective_types[0] == BatteryType::None &&
      (effective_types[1] != BatteryType::None || effective_types[2] != BatteryType::None)) {
    result.ok = false;
    result.error = "Configure the primary battery before adding extra batteries.";
    result.error_key = "error.battery_primary_required";
    return result;
  }

  if (esp32hal != nullptr) {
    GpioOptionCatalog catalog = esp32hal->gpio_options();
    for (size_t g = 0; g < catalog.group_count; g++) {
      JsonVariantConst value = values[catalog.groups[g].nvs_key];
      if (!value.isNull() && !value.is<uint32_t>()) {
        result.ok = false;
        result.error = String("Invalid type for setting ") + catalog.groups[g].nvs_key;
        result.error_key = "error.setting_invalid_type";
        result.error_arg = catalog.groups[g].nvs_key;
        return result;
      }
    }
  }

  bool reboot_required = false;
  for (size_t i = 0; i < kSettingFieldCount; i++) {
    const SettingField& field = kSettingFields[i];
    const bool gates_reboot = field.applies == SA::Boot;
    if (field.live.scope == SCP::BatterySlot) {
      continue;
    }
    JsonVariantConst value = values[field.key];
    if (value.isNull()) {
      // Explicit JSON null on a present password key clears the secret; an absent key — or
      // null on any non-password key — preserves the stored value (the bool-wipe invariant).
      if (is_password_key(field.key) && values.containsKey(field.key)) {
        reboot_required |= gates_reboot && (store.getString(field.key, "").length() > 0);
        store.saveString(field.key, "");
        if (std::strcmp(field.key, "PASSWORD") == 0) {
          password = store.getString("PASSWORD", "").c_str();
        }
      }
      continue;
    }
    if (field.live.kind != SR::None) {
      write_live(field, 0, field.type == ST::Bool ? (value.as<bool>() ? 1.0 : 0.0) : value.as<double>());
    }
    if (field.storage == SS::Volatile) {
      continue;
    }
    switch (field.type) {
      case ST::Bool: {
        const bool new_value = value.as<bool>();
        reboot_required |= gates_reboot && (store.getBool(field.key, field.default_int != 0) != new_value);
        store.saveBool(field.key, new_value);
        break;
      }
      case ST::Uint:
      case ST::EnumUint:
      case ST::InterfacePacked: {
        const uint32_t new_value = value.as<uint32_t>();
        reboot_required |=
            gates_reboot && (store.getUInt(field.key, static_cast<uint32_t>(field.default_int)) != new_value);
        store.saveUInt(field.key, new_value);
        break;
      }
      case ST::Int: {
        const int32_t new_value = value.as<int32_t>();
        reboot_required |= gates_reboot && (store.getInt(field.key, field.default_int) != new_value);
        store.saveInt(field.key, new_value);
        break;
      }
      case ST::SecondsToMs: {
        const uint32_t new_value = value.as<uint32_t>() * kMillisecondsPerSecond;
        reboot_required |=
            gates_reboot &&
            (store.getUInt(field.key, static_cast<uint32_t>(field.default_int) * kMillisecondsPerSecond) != new_value);
        store.saveUInt(field.key, new_value);
        break;
      }
      case ST::FloatX10: {
        const uint32_t new_value = static_cast<uint32_t>(std::lround(value.as<float>() * kDeciUnitsPerUnit));
        reboot_required |=
            gates_reboot && (store.getUInt(field.key, static_cast<uint32_t>(field.default_int)) != new_value);
        store.saveUInt(field.key, new_value);
        break;
      }
      case ST::SignedFloatX10: {
        const int32_t new_value = static_cast<int32_t>(std::lround(value.as<float>() * kDeciUnitsPerUnit));
        reboot_required |= gates_reboot && (store.getInt(field.key, field.default_int) != new_value);
        store.saveInt(field.key, new_value);
        break;
      }
      case ST::Float:
        break;
      case ST::StringVal:
      case ST::FloatString: {
        const char* new_value = value.as<const char*>();
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
      JsonVariantConst value = values[group.nvs_key];
      if (value.isNull()) {
        continue;
      }
      const uint32_t requested = value.as<uint32_t>();
      const uint32_t applied = find_gpio_option_choice(group, requested) != nullptr ? requested : group.default_value;
      reboot_required |= store.getUInt(group.nvs_key, group.default_value) != applied;
      store.saveUInt(group.nvs_key, applied);
    }
  }

  for (JsonObjectConst entry : root["dynamic"]["batteries"].as<JsonArrayConst>()) {
    const uint8_t slot = entry["slot"].as<uint8_t>();
    if (slot >= kMaxBatterySlots) {
      continue;
    }
    const BatterySlotKeys& keys = kBatterySlotKeys[slot];
    if (entry["type"].is<uint32_t>()) {
      const uint32_t type = entry["type"].as<uint32_t>();
      reboot_required |= store.getUInt(keys.type_key, static_cast<uint32_t>(BatteryType::None)) != type;
      store.saveUInt(keys.type_key, type);
    }
    if (entry["comm"].is<uint32_t>()) {
      const uint32_t comm = entry["comm"].as<uint32_t>();
      reboot_required |= store.getUInt(keys.comm_key, 0) != comm;
      store.saveUInt(keys.comm_key, comm);
    }
    if (entry["contactor_control"].is<bool>()) {
      const bool contactor = entry["contactor_control"].as<bool>();
      reboot_required |= store.getBool(keys.contactor_key, false) != contactor;
      store.saveBool(keys.contactor_key, contactor);
    }
  }

#ifdef BOARD_HAS_INTERFACE_TERMINATION
  if (esp32hal != nullptr) {
    for (JsonObjectConst entry : root["dynamic"]["termination"].as<JsonArrayConst>()) {
      const size_t index = entry["index"].as<size_t>();
      // Omitted/non-bool enabled must preserve, never wipe (bool-wipe fix); an
      // index the HAL cannot terminate would otherwise write a garbage NVS key.
      if (!entry["enabled"].is<bool>() || !esp32hal->supports_interface_termination(index)) {
        continue;
      }
      const bool enabled = entry["enabled"].as<bool>();
      const String key = interface_termination_key(index);
      if (store.getBool(key.c_str(), false) != enabled) {
        store.saveBool(key.c_str(), enabled);
        esp32hal->set_interface_termination(index, enabled);
      }
    }
  }
#endif

#ifdef BOARD_HAS_LOAD_SWITCH
  if (esp32hal != nullptr) {
    if (LoadSwitch* load_switch = esp32hal->load_switch()) {
      for (JsonObjectConst channel : root["dynamic"]["loadswitch"]["channels"].as<JsonArrayConst>()) {
        const uint8_t ch = channel["channel"].as<uint8_t>();
        // A phantom channel would write out-of-range LSROLE/LSDUTY keys and set
        // reboot_required before the HAL rejects the request.
        if (ch >= kLoadSwitchConfigChannels) {
          continue;
        }
        if (!channel["role"].isNull()) {
          const uint32_t role = channel["role"].as<uint32_t>();
          if (role < static_cast<uint32_t>(LoadSwitchRole::Highest)) {
            reboot_required |= store.getUInt(load_switch_role_key(ch).c_str(),
                                             static_cast<uint32_t>(LoadSwitchRole::Disabled)) != role;
            store.saveUInt(load_switch_role_key(ch).c_str(), role);
          }
        }
        if (!channel["duty"].isNull()) {
          uint32_t duty_pct = channel["duty"].as<uint32_t>();
          if (duty_pct > kPercentMax) {
            duty_pct = kPercentMax;
          }
          const uint32_t duty = (duty_pct * kLoadSwitchDutyMax + kPercentMax / 2) / kPercentMax;
          if (store.getUInt(load_switch_duty_key(ch).c_str(), kLoadSwitchDutyMax) != duty) {
            store.saveUInt(load_switch_duty_key(ch).c_str(), duty);
            load_switch->request_duty(ch, static_cast<uint16_t>(duty));
          }
        }
        if (!channel["divisor"].isNull()) {
          uint32_t divisor = channel["divisor"].as<uint32_t>();
          if (divisor >= kLoadSwitchDivisorCodes) {
            divisor = 0;
          }
          if (store.getUInt(load_switch_divisor_key(ch).c_str(), 0) != divisor) {
            store.saveUInt(load_switch_divisor_key(ch).c_str(), divisor);
            load_switch->request_divisor(ch, static_cast<uint8_t>(divisor));
          }
        }
      }
    }
  }
#endif

  for (JsonObjectConst entry : root["dynamic"]["balancing"].as<JsonArrayConst>()) {
    const uint8_t slot = entry["slot"].as<uint8_t>();
    for (size_t i = 0; i < kSettingFieldCount; i++) {
      const SettingField& field = kSettingFields[i];
      if (field.live.scope != SCP::BatterySlot) {
        continue;
      }
      JsonVariantConst value = entry[field.key];
      if (!value.isNull()) {
        write_live(field, slot, field.type == ST::Bool ? (value.as<bool>() ? 1.0 : 0.0) : value.as<double>());
      }
    }
  }

  result.changed = store.were_settings_updated();
  result.reboot_required = reboot_required;
  return result;
}

static constexpr float DECI_PER_UNIT = 10.0f;

const char* validate_balancing_field(battery_chemistry_enum chemistry, const char* key, JsonVariantConst value) {
  if (value.isNull()) {
    return nullptr;
  }
  const bool lfp = chemistry == battery_chemistry_enum::LFP;

  if (std::strcmp(key, "max_cell_mv") == 0) {
    if (!value.is<uint16_t>()) {
      return "Bad Request";
    }
    const uint16_t mv = value.as<uint16_t>();
    const uint16_t cell_max_mV = lfp ? BALANCING_CELL_MAX_LFP_MV : BALANCING_CELL_MAX_NCM_MV;
    return (mv < BALANCING_CELL_MIN_MV || mv > cell_max_mV) ? "Cell voltage out of range" : nullptr;
  }
  if (std::strcmp(key, "max_dev_mv") == 0) {
    if (!value.is<uint16_t>()) {
      return "Bad Request";
    }
    const uint16_t mv = value.as<uint16_t>();
    const uint16_t deviation_max_mV = lfp ? BALANCING_DEVIATION_MAX_LFP_MV : BALANCING_DEVIATION_MAX_NCM_MV;
    return (mv < BALANCING_DEVIATION_MIN_MV || mv > deviation_max_mV) ? "Cell deviation out of range" : nullptr;
  }
  if (std::strcmp(key, "float_power_w") == 0) {
    if (!value.is<uint16_t>()) {
      return "Bad Request";
    }
    const uint16_t watts = value.as<uint16_t>();
    return (watts < BALANCING_FLOAT_POWER_MIN_W || watts > BALANCING_FLOAT_POWER_MAX_W) ? "Float power out of range"
                                                                                        : nullptr;
  }
  if (std::strcmp(key, "max_pack_v") == 0) {
    if (!value.is<float>()) {
      return "Bad Request";
    }
    const uint16_t pack_max_dV =
        (lfp ? BALANCING_PACK_MAX_LFP_DV : BALANCING_PACK_MAX_NCM_DV) + BALANCING_PACK_HEADROOM_DV;
    const long dv = lroundf(value.as<float>() * DECI_PER_UNIT);
    return (dv < BALANCING_PACK_MIN_DV || dv > pack_max_dV) ? "Pack voltage out of range" : nullptr;
  }
  if (std::strcmp(key, "max_time_min") == 0 && !value.is<float>()) {
    return "Bad Request";
  }
  return nullptr;
}
