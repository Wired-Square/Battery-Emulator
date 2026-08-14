#include "settings_labels.h"
#include <utility>
#include <vector>
#include "../hal/hal.h"

String options_for_comm_interface(uint32_t selected_packed) {
  InterfaceList list = esp32hal->interfaces();
  std::vector<std::pair<String, uint32_t>> pairs;
  for (size_t i = 0; i < list.count; i++) {
    if (!descriptor_selectable(list.data[i])) {
      continue;
    }
    pairs.push_back(std::pair(String(descriptor_name(list.data[i])), pack_interface_config(list.data[i].type, i)));
  }
  // Alphabetical, matching the pre-descriptor ordering.
  std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

  String options;
  for (const auto& [name, packed] : pairs) {
    options += ("<option value=\"" + String(packed) + "\"" + (selected_packed == packed ? " selected" : "") + ">");
    options += name;
    options += "</option>";
  }
  return options;
}
