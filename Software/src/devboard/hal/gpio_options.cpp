#include "gpio_options.h"

#include <string.h>

const GpioOptionGroup* find_gpio_option_group(GpioOptionCatalog catalog, const char* nvs_key) {
  for (size_t i = 0; i < catalog.group_count; i++) {
    if (strcmp(catalog.groups[i].nvs_key, nvs_key) == 0) {
      return &catalog.groups[i];
    }
  }
  return nullptr;
}

const GpioOptionChoice* find_gpio_option_choice(const GpioOptionGroup& group, uint32_t value) {
  for (uint8_t i = 0; i < group.choice_count; i++) {
    if (group.choices[i].value == value) {
      return &group.choices[i];
    }
  }
  return nullptr;
}

int16_t resolve_gpio_pin(GpioOptionCatalog catalog, const uint8_t* selections, size_t selection_count,
                         GpioPinRole role, int16_t fallback) {
  for (size_t g = 0; g < catalog.group_count && g < selection_count; g++) {
    const GpioOptionChoice* choice = find_gpio_option_choice(catalog.groups[g], selections[g]);
    if (choice == nullptr) {
      continue;
    }
    for (uint8_t p = 0; p < choice->pin_count; p++) {
      if (choice->pins[p].role == role) {
        return choice->pins[p].pin;
      }
    }
  }
  return fallback;
}
