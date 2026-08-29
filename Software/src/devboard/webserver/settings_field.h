#ifndef SETTINGS_FIELD_H
#define SETTINGS_FIELD_H

#include <cstddef>
#include <cstdint>

// Selects the JSON<->NVS transform. InterfacePacked carries a packed
// interface-config uint that resolves against the live descriptor table.
enum class SettingType : uint8_t {
  Bool,
  Uint,
  Int,
  StringVal,
  EnumUint,
  FloatX10,
  SignedFloatX10,
  Float,
  FloatString,
  SecondsToMs,
  InterfacePacked
};

enum class SettingApplies : uint8_t { Boot, Live };

enum class SettingStorage : uint8_t { Nvs, Volatile };

enum class SettingScope : uint8_t { Global, BatterySlot };

enum class SettingRam : uint8_t { None, Bool, U8, U16, I16, U32, F32 };

enum class SettingDomain : uint8_t { None, Battery, Inverter };

constexpr int32_t kNoMin = INT32_MIN;
constexpr int32_t kNoMax = INT32_MAX;

constexpr uint8_t kAnySlot = 0xFF;

inline constexpr const char* kNetwork = "network";
inline constexpr const char* kWebauth = "webauth";
inline constexpr const char* kBattery = "battery";
inline constexpr const char* kInverter = "inverter";
inline constexpr const char* kOptional = "optional";
inline constexpr const char* kHardware = "hardware";
inline constexpr const char* kConnectivity = "connectivity";
inline constexpr const char* kDebug = "debug";
inline constexpr const char* kInterface = "interface";
inline constexpr const char* kLive = "live";

inline constexpr const char* kSecChargeLimits = "chargelimits";
inline constexpr const char* kSecCharger = "charger";
inline constexpr const char* kSecBydCal = "bydautocal";
inline constexpr const char* kSecRecovery = "recoverymode";
inline constexpr const char* kSecCanIdCutoff = "canidcutoff";
inline constexpr const char* kSecBalancing = "balancing";

struct SettingLive {
  SettingRam kind = SettingRam::None;
  void* (*address)(uint8_t slot) = nullptr;
  int32_t ram_per_json = 1;
  SettingScope scope = SettingScope::Global;
  void (*on_apply)(uint8_t slot) = nullptr;
};

struct SettingField {
  const char* key;  // <= 15 chars (NVS key-length limit)
  SettingType type;
  const char* category;
  SettingApplies applies;
  int32_t default_int;      // Bool(0/1), Uint, Int, EnumUint, FloatX10 (deci-units), SecondsToMs (seconds)
  const char* default_str;  // StringVal / FloatString; nullptr otherwise
  // Pick-list key into the JSON "options" object; nullptr when the field is not
  // a dropdown. InterfacePacked fields leave this null and use "interfaces".
  const char* options_key = nullptr;
  // Inclusive numeric bounds (kNoMin/kNoMax = unbounded); text-field patterns are client-only.
  int32_t min_value = kNoMin;
  int32_t max_value = kNoMax;
  SettingStorage storage = SettingStorage::Nvs;
  const char* section = nullptr;
  SettingLive live = {};
};

struct DeviceSetting {
  SettingField field;
  const char* label = nullptr;
  uint8_t slot = kAnySlot;
  SettingDomain domain = SettingDomain::None;
  uint16_t capability = 0;
};

struct DeviceSettingList {
  const DeviceSetting* data = nullptr;
  size_t count = 0;
};

template <size_t N>
constexpr DeviceSettingList device_settings(const DeviceSetting (&rows)[N]) {
  return {rows, N};
}

#endif
