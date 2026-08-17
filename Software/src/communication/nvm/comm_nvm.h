#ifndef _COMM_NVM_H_
#define _COMM_NVM_H_

#include <Preferences.h>
#include <WString.h>
#include <limits>
#include "../../battery/Battery.h"
#include "../../datalayer/datalayer.h"
#include "../../devboard/utils/events.h"
#include "../../devboard/utils/logging.h"
#include "../../devboard/wifi/wifi.h"

/**
 * @brief Initialization of setting storage
 *
 * @param[in] void
 *
 * @return void
 */
void init_stored_settings();

/**
 * @brief Store settings of equipment stop button
 *
 * @param[in] void
 *
 * @return void
 */
void store_settings_equipment_stop();

void erase_phy_cal_data();

/**
 * @brief Store settings
 *
 * @param[in] void
 *
 * @return void
 */
void store_settings();

void clear_wifi_sta_settings();

// NVM key for one interface's termination toggle, keyed by descriptor index
// (stable: per-board descriptor lists are append-only).
inline String interface_termination_key(size_t interface_index) {
  return String("TERMIF") + String(static_cast<unsigned>(interface_index));
}

#ifdef BOARD_HAS_LOAD_SWITCH
inline String load_switch_role_key(uint8_t channel) {
  return String("LSROLE") + String(channel);
}
inline String load_switch_duty_key(uint8_t channel) {
  return String("LSDUTY") + String(channel);
}
inline String load_switch_divisor_key(uint8_t channel) {
  return String("LSDIV") + String(channel);
}
#endif

#ifdef BOARD_HAS_INTERFACE_TERMINATION
// Requires board_init() to have run first (the expander must be up on
// boards that switch termination through one).
void apply_stored_interface_termination();
#endif

// Wraps the Preferences object begin/end calls, so that the scope of this object
// runs them automatically (via constructor/destructor).
class BatteryEmulatorSettingsStore {
 public:
  BatteryEmulatorSettingsStore(bool readOnly = false) {
    if (!settings.begin("batterySettings", readOnly)) {
      set_event(EVENT_PERSISTENT_SAVE_INFO, 0);
    }
  }

  ~BatteryEmulatorSettingsStore() { settings.end(); }

  void clearAll() {
    settings.clear();
    settingsUpdated = true;
  }

  int32_t getInt(const char* name, int32_t defaultValue) {
    return settings.isKey(name) ? settings.getInt(name, defaultValue) : defaultValue;
  }

  void saveInt(const char* name, int32_t value) {
    // isKey() check instead of a sentinel default: saving a value equal to the
    // sentinel into a missing key must not be skipped.
    if (!settings.isKey(name) || getInt(name, 0) != value) {
      settings.putInt(name, value);
      settingsUpdated = true;
    }
  }

  uint32_t getUInt(const char* name, uint32_t defaultValue) {
    return settings.isKey(name) ? settings.getUInt(name, defaultValue) : defaultValue;
  }

  void saveUInt(const char* name, uint32_t value) {
    // isKey() check instead of a sentinel default: saving a value equal to the
    // sentinel into a missing key must not be skipped.
    if (!settings.isKey(name) || getUInt(name, 0) != value) {
      settings.putUInt(name, value);
      settingsUpdated = true;
    }
  }

  bool settingExists(const char* name) { return settings.isKey(name); }

  void removeKey(const char* name) {
    if (settings.isKey(name)) {
      settings.remove(name);
      settingsUpdated = true;
    }
  }

  bool getBool(const char* name, bool defaultValue = false) {
    return settings.isKey(name) ? settings.getBool(name, defaultValue) : defaultValue;
  }

  void saveBool(const char* name, bool value) {
    // isKey() check: a stored 'false' must not be mistaken for a missing key,
    // or the first save of a false value would be skipped and never persisted.
    if (!settings.isKey(name) || getBool(name, false) != value) {
      settings.putBool(name, value);
      settingsUpdated = true;
    }
  }

  String getString(const char* name) { return getString(name, ""); }

  String getString(const char* name, const char* defaultValue) {
    return settings.isKey(name) ? settings.getString(name, defaultValue) : String(defaultValue);
  }

  void saveString(const char* name, const char* value) {
    // isKey() check: a stored empty string must not be mistaken for a missing
    // key, or the first save of an empty value would be skipped.
    if (!settings.isKey(name) || getString(name, "") != String(value)) {
      settings.putString(name, value);
      settingsUpdated = true;
    }
  }

  bool were_settings_updated() const { return settingsUpdated; }

 private:
  Preferences settings;

  // To track if settings were updated
  bool settingsUpdated = false;
};

inline void migrate_battery_slot_types(BatteryEmulatorSettingsStore& settings) {
  if (settings.getBool("DBLBTR", false) && !settings.settingExists("BATT2TYPE")) {
    settings.saveUInt("BATT2TYPE", settings.getUInt("BATTTYPE", (uint32_t)BatteryType::None));
  }
  if (settings.getBool("TRIBTR", false) && !settings.settingExists("BATT3TYPE")) {
    settings.saveUInt("BATT3TYPE", settings.getUInt("BATTTYPE", (uint32_t)BatteryType::None));
  }
}

#endif
