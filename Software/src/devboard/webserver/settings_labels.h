#ifndef SETTINGS_LABELS_H
#define SETTINGS_LABELS_H

#include <WString.h>
#include <algorithm>
#include <map>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../communication/equipmentstopbutton/comm_equipmentstopbutton.h"
#include "../utils/types.h"

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

template <typename EnumType, typename Func>
std::vector<std::pair<String, EnumType>> enum_values_and_names(Func name_for_type,
                                                               const EnumType* noneValue = nullptr) {
  auto values = enum_values<EnumType>();

  std::vector<std::pair<String, EnumType>> pairs;

  for (auto& type : values) {
    auto name = name_for_type(type);
    if (name != nullptr) {
      pairs.push_back(std::pair(String(name), type));
    }
  }

  std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

  if (noneValue) {
    pairs.insert(pairs.begin(), std::pair(name_for_type(*noneValue), *noneValue));
  }

  return pairs;
}

template <typename TEnum, typename Func>
String options_for_enum_with_none(TEnum selected, Func name_for_type, TEnum noneValue) {
  String options;
  TEnum none = noneValue;
  auto values = enum_values_and_names<TEnum>(name_for_type, &none);
  for (const auto& [name, type] : values) {
    options +=
        ("<option value=\"" + String(static_cast<int>(type)) + "\"" + (selected == type ? " selected" : "") + ">");
    options += name;
    options += "</option>";
  }
  return options;
}

template <typename TEnum, typename Func>
String options_for_enum(TEnum selected, Func name_for_type) {
  String options;
  auto values = enum_values_and_names<TEnum>(name_for_type, nullptr);
  for (const auto& [name, type] : values) {
    if (name[0] == '\0')
      continue;
    options +=
        ("<option value=\"" + String(static_cast<int>(type)) + "\"" + (selected == type ? " selected" : "") + ">");
    options += name;
    options += "</option>";
  }
  return options;
}

template <typename TMap>
String options_from_map(int selected, const TMap& value_name_map) {
  String options;
  for (const auto& [value, name] : value_name_map) {
    options += "<option value=\"" + String(value) + "\"";
    if (selected == value) {
      options += " selected";
    }
    options += ">";
    options += name;
    options += "</option>";
  }
  return options;
}

extern const std::map<int, String> led_modes;

// Defined in settings_comm_options.cpp (device-only); reads esp32hal and so must
// never enter the host build.
String options_for_comm_interface(uint32_t selected_packed);

// Enum-label producers, host-available here so option lists build off-device.
const char* name_for_button_type(STOP_BUTTON_BEHAVIOR behavior);

#endif
