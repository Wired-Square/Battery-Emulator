#include "settings_labels.h"

#ifdef HW_LILYGO2CAN
const std::map<int, String> led_modes = {{0, "Classic"},     {1, "Energy Flow"},     {2, "Heartbeat"},
                                         {3, "GRB Classic"}, {4, "GRB Energy Flow"}, {5, "GRB Heartbeat"}};
#else
const std::map<int, String> led_modes = {{0, "Classic"}, {1, "Energy Flow"}, {2, "Heartbeat"}};
#endif

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

