#include "../inverter/INVERTERS.h"
#include "BMW-SBOX.h"
#include "Shunt.h"

CanShunt* shunt = nullptr;
ShuntType user_selected_shunt_type = ShuntType::None;

void setup_shunt() {
  if (shunt) {
    return;
  }
  switch (user_selected_shunt_type) {
    case ShuntType::BmwSbox:
      shunt = new BmwSbox();
      shunt->setup();
      break;
    case ShuntType::Inverter:
      // The selected inverter supplies shunt values directly.
      if (inverter && inverter->provides_shunt()) {
        inverter->enable_shunt();
      }
      break;
    case ShuntType::CustomClamp:
      // CT/ADC clamp is set up and read inside the CHAdeMO battery driver.
      break;
    case ShuntType::None:
    case ShuntType::Highest:
      break;
  }
}

extern const char* name_for_shunt_type(ShuntType type) {
  switch (type) {
    case ShuntType::None:
      return "None";
    case ShuntType::BmwSbox:
      return BmwSbox::Name;
    case ShuntType::Inverter:
      return "Using inverter values";
    case ShuntType::CustomClamp:
      return "Custom Clamp";
    default:
      return nullptr;
  }
}
