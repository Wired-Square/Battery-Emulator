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
  DeciUnits,
  SignedDeciUnits,
  Float,
  FloatString,
  SecondsToMs,
  InterfacePacked
};

enum class SettingApplies : uint8_t { Boot, Live };

enum class SettingStorage : uint8_t { Nvs, Volatile };

enum class SettingScope : uint8_t { Global, BatterySlot, Interface, LoadSwitchChannel, Highest };

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
inline constexpr const char* kSecLoadSwitch = "loadswitch";

struct SettingLive {
  SettingRam kind = SettingRam::None;
  void* (*address)(uint8_t slot) = nullptr;
  int32_t ram_per_json = 1;
  SettingScope scope = SettingScope::Global;
  // Runs at boot for every row, and on save for SettingApplies::Live rows only.
  void (*on_apply)(uint8_t index, double value) = nullptr;
};

struct SettingField {
  const char* key;
  SettingType type;
  const char* category;
  SettingApplies applies;
  int32_t default_int;      // Bool(0/1), Uint, Int, EnumUint, DeciUnits, SecondsToMs (seconds)
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

  constexpr SettingField with_text(const char* value) const {
    SettingField f = *this;
    f.default_str = value;
    return f;
  }
  constexpr SettingField with_options(const char* value) const {
    SettingField f = *this;
    f.options_key = value;
    return f;
  }
  constexpr SettingField with_range(int32_t low, int32_t high) const {
    SettingField f = *this;
    f.min_value = low;
    f.max_value = high;
    return f;
  }
  constexpr SettingField in_section(const char* value) const {
    SettingField f = *this;
    f.section = value;
    return f;
  }
  constexpr SettingField volatile_storage() const {
    SettingField f = *this;
    f.storage = SettingStorage::Volatile;
    return f;
  }
  constexpr SettingField bound(SettingLive value) const {
    SettingField f = *this;
    f.live = value;
    return f;
  }
};

constexpr SettingField setting(const char* key, SettingType type, const char* category, SettingApplies applies,
                               int32_t default_int = 0) {
  return {key, type, category, applies, default_int, nullptr};
}

inline constexpr size_t kMaxNvsKeyLength = 15;

constexpr size_t key_length(const char* key) {
  size_t n = 0;
  while (key != nullptr && key[n] != '\0') {
    n++;
  }
  return n;
}

constexpr bool keys_equal(const char* a, const char* b) {
  size_t i = 0;
  while (a[i] != '\0' && a[i] == b[i]) {
    i++;
  }
  return a[i] == b[i];
}

// A volatile key never reaches NVS, so only persisted keys carry the length limit.
constexpr bool valid_field(const SettingField& f) {
  const size_t length = key_length(f.key);
  if (length == 0 || (f.storage != SettingStorage::Volatile && length > kMaxNvsKeyLength)) {
    return false;
  }
  return f.min_value == kNoMin || f.max_value == kNoMax || f.min_value <= f.max_value;
}

template <size_t N>
constexpr bool fields_valid(const SettingField (&table)[N]) {
  for (size_t i = 0; i < N; i++) {
    if (!valid_field(table[i])) {
      return false;
    }
  }
  return true;
}

template <size_t N>
constexpr bool keys_unique(const SettingField (&table)[N]) {
  for (size_t i = 0; i < N; i++) {
    for (size_t j = i + 1; j < N; j++) {
      if (keys_equal(table[i].key, table[j].key)) {
        return false;
      }
    }
  }
  return true;
}

struct DeviceSetting {
  SettingField field;
  const char* label = nullptr;
  uint8_t slot = kAnySlot;
  SettingDomain domain = SettingDomain::None;
  // Bits from the capability set `domain` names, and nothing here checks the two
  // agree. Write them through battery_row()/inverter_row(), never by hand.
  uint16_t capability_bits = 0;
  // null = the row applies to every entry in its scope.
  bool (*applies_to)(uint8_t index) = nullptr;
};

constexpr DeviceSetting board_row(SettingField field, const char* label,
                                  bool (*applies_to)(uint8_t index) = nullptr) {
  return {field, label, kAnySlot, SettingDomain::None, 0, applies_to};
}

template <size_t N>
constexpr bool fields_valid(const DeviceSetting (&table)[N]) {
  for (size_t i = 0; i < N; i++) {
    if (!valid_field(table[i].field)) {
      return false;
    }
  }
  return true;
}

struct ScopeEntry {
  uint8_t index;
  const char* label;
};

struct ScopeEntries {
  const ScopeEntry* data = nullptr;
  size_t count = 0;
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
