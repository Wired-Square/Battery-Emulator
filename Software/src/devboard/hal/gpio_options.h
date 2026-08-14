#ifndef GPIO_OPTIONS_H
#define GPIO_OPTIONS_H

#include <stddef.h>
#include <stdint.h>

// Board-scoped configurable GPIO options. Pins are int16_t, not gpio_num_t, so
// this header stays MCU-neutral; the HAL casts at the ESP32 boundary.

enum class GpioPinRole : uint8_t {
  DisplaySda,
  DisplayScl,
  EquipmentStop,
  Wup1,
  Wup2,
  InverterContactorEnable,
  SdMiso,
  SdMosi,
  SdSclk,
  SdCs,
  Led,
  Count
};

inline constexpr int16_t kGpioPinNotConnected = -1;
inline constexpr size_t kMaxGpioOptionGroups = 4;  // max on any board today is 3 (LilyGo)

struct PinAssignment {
  GpioPinRole role;
  int16_t pin;
};

struct GpioOptionChoice {
  uint8_t value;              // legacy enum ordinal; the NVS + wire value
  const char* label;          // nullptr = hidden (small-flash / unavailable)
  const PinAssignment* pins;
  uint8_t pin_count;
  uint8_t output_variant;     // 0 = primary switched-output table; 1+ = alternate
};

struct GpioOptionGroup {
  const char* nvs_key;  // legacy "GPIOOPTn"; NVS + wire identity, <= 15 chars
  const char* label;
  const GpioOptionChoice* choices;
  uint8_t choice_count;
  uint8_t default_value;
};

struct GpioOptionCatalog {
  const GpioOptionGroup* groups;
  size_t group_count;
};

template <size_t N>
constexpr GpioOptionCatalog make_gpio_option_catalog(const GpioOptionGroup (&groups)[N]) {
  // Extra groups would render and persist but silently never reach the HAL.
  static_assert(N <= kMaxGpioOptionGroups, "board declares more GPIO option groups than the HAL holds selections for");
  return {groups, N};
}

// Board headers static_assert these three invariants (per group, then per catalog).
constexpr bool gpio_choices_valid(const GpioOptionGroup& group) {
  if (group.choice_count == 0 || group.choices[0].value != 0) {
    return false;
  }
  for (uint8_t i = 1; i < group.choice_count; i++) {
    if (group.choices[i - 1].value >= group.choices[i].value) {
      return false;
    }
  }
  // A default naming no choice would make the clamp re-install an unknown value,
  // leaving every role in the group on its caller fallback.
  for (uint8_t i = 0; i < group.choice_count; i++) {
    if (group.choices[i].value == group.default_value) {
      return true;
    }
  }
  return false;
}

// All choices in a group must define the same role set, or a selection could
// half-fall-back to a fixed pin for a role only some choices override.
constexpr bool gpio_choices_cover_same_roles(const GpioOptionGroup& group) {
  for (uint8_t c = 1; c < group.choice_count; c++) {
    if (group.choices[c].pin_count != group.choices[0].pin_count) {
      return false;
    }
    for (uint8_t i = 0; i < group.choices[0].pin_count; i++) {
      bool found = false;
      for (uint8_t j = 0; j < group.choices[c].pin_count; j++) {
        if (group.choices[c].pins[j].role == group.choices[0].pins[i].role) {
          found = true;
          break;
        }
      }
      if (!found) {
        return false;
      }
    }
  }
  return true;
}

// A role in two groups would make resolve_gpio_pin's first-match ambiguous.
constexpr bool gpio_groups_roles_disjoint(GpioOptionCatalog catalog) {
  for (size_t g = 0; g < catalog.group_count; g++) {
    for (size_t h = g + 1; h < catalog.group_count; h++) {
      for (uint8_t ci = 0; ci < catalog.groups[g].choice_count; ci++) {
        for (uint8_t pi = 0; pi < catalog.groups[g].choices[ci].pin_count; pi++) {
          const GpioPinRole role = catalog.groups[g].choices[ci].pins[pi].role;
          for (uint8_t cj = 0; cj < catalog.groups[h].choice_count; cj++) {
            for (uint8_t pj = 0; pj < catalog.groups[h].choices[cj].pin_count; pj++) {
              if (catalog.groups[h].choices[cj].pins[pj].role == role) {
                return false;
              }
            }
          }
        }
      }
    }
  }
  return true;
}

const GpioOptionGroup* find_gpio_option_group(GpioOptionCatalog catalog, const char* nvs_key);

// Takes uint32_t so a stored or posted value wider than a choice ordinal fails the
// lookup instead of being silently narrowed onto an unrelated choice.
const GpioOptionChoice* find_gpio_option_choice(const GpioOptionGroup& group, uint32_t value);

// Earlier groups take precedence when more than one defines the same role.
int16_t resolve_gpio_pin(GpioOptionCatalog catalog, const uint8_t* selections, size_t selection_count,
                         GpioPinRole role, int16_t fallback);

#endif
