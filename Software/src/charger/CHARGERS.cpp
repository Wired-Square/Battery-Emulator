#include "CHARGERS.h"
#include <iterator>
#include "CanCharger.h"

CanCharger* charger = nullptr;

ChargerType user_selected_charger_type = ChargerType::None;

/* Charger settings (Optional, when using generator charging) */
//TODO: These should be user configurable via webserver
volatile float CHARGER_SET_HV = 384;      // Reasonably appropriate 4.0v per cell charging of a 96s pack
volatile float CHARGER_MAX_HV = 420;      // Max permissible output (VDC) of charger
volatile float CHARGER_MIN_HV = 200;      // Min permissible output (VDC) of charger
volatile float CHARGER_MAX_POWER = 3300;  // Max power capable of charger, as a ceiling for validating config
volatile float CHARGER_MAX_A = 11.5f;     // Max current output (amps) of charger
volatile float CHARGER_END_A = 1.0f;      // Current at which charging is considered complete

template <typename T>
static CanCharger* make() {
  return new T();
}

struct ChargerTypeInfo {
  ChargerType id;
  const char* name;
  CanCharger* (*make)();  // nullptr => not constructible (None)
};

// A row's make<T> must construct the class its id and name denote; the pairing is convention, not compiler-checked.
static constexpr ChargerTypeInfo kChargerRegistry[] = {
    {ChargerType::None,       "None",                              nullptr},
    {ChargerType::NissanLeaf, "Nissan LEAF 2013-2024 PDM charger", &make<NissanLeafCharger>},
    {ChargerType::ChevyVolt,  "Chevy Volt Gen1 Charger",           &make<ChevyVoltCharger>},
    // Highest is a count sentinel, not a selectable type
};

static constexpr bool registry_strictly_ascending() {
  for (size_t i = 1; i < std::size(kChargerRegistry); i++)
    if (kChargerRegistry[i - 1].id >= kChargerRegistry[i].id) return false;
  return true;
}
static_assert(registry_strictly_ascending(),
              "charger registry rows must be sorted by enum value with no duplicates");

static const ChargerTypeInfo* find_charger_info(ChargerType type) {
  for (const auto& info : kChargerRegistry)
    if (info.id == type) return &info;
  return nullptr;
}

const char* name_for_charger_type(ChargerType type) {
  const ChargerTypeInfo* info = find_charger_info(type);
  return info ? info->name : nullptr;
}

void setup_charger() {
  const ChargerTypeInfo* info = find_charger_info(user_selected_charger_type);
  if (info && info->make) {
    charger = info->make();
  }
}
