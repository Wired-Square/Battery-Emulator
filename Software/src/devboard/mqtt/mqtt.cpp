#include "mqtt.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <src/communication/nvm/comm_nvm.h>
#include <list>
#include "../../battery/BATTERIES.h"
#include "../../communication/contactorcontrol/comm_contactorcontrol.h"
#include "../../datalayer/datalayer.h"
#include "../../datalayer/datalayer_extended.h"
#include "../../devboard/hal/hal.h"
#include "../../devboard/safety/safety.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "../utils/events.h"
#include "../utils/timer.h"
#include "../webserver/webserver.h"
#include "mqtt.h"
#include "mqtt_client.h"

std::string mqtt_user;
std::string mqtt_password;

bool mqtt_enabled = false;
bool ha_autodiscovery_enabled = false;
std::string ha_autodiscovery_topic = "homeassistant";
bool mqtt_transmit_all_cellvoltages = false;
uint16_t mqtt_timeout_ms = 2000;
uint16_t mqtt_publish_interval_ms = 5000;

const int mqtt_port_default = 0;
const char* mqtt_server_default = "";

int mqtt_port = mqtt_port_default;
std::string mqtt_server = mqtt_server_default;

#define MQTT_QOS 0  // MQTT Quality of Service (0, 1, or 2) //TODO: Should this be configurable?

// Cap on the esp-mqtt outbox, in bytes. QoS>0 traffic queues in the outbox while the broker
// or Wi-Fi is unresponsive; without a limit it grows on the heap until OOM. With the limit,
// enqueueing fails instead — a bounded, recoverable failure.
#define MQTT_OUTBOX_LIMIT_BYTES (16 * 1024)

esp_mqtt_client_config_t mqtt_cfg;
esp_mqtt_client_handle_t client;
char mqtt_msg[MQTT_MSG_BUFFER_SIZE];
MyTimer publish_global_timer(0);  // Will be configured with mqtt_publish_interval_ms on first use
MyTimer check_global_timer(800);  // check timmer - low-priority MQTT checks, where responsiveness is not critical.
bool client_started = false;
static String lwt_topic = "";

static String topic_name = "";
static String default_entity_id_prefix = "";
static String device_name = "";
static String device_id = "";

static bool publish_common_info(void);
static bool publish_cell_voltages(void);
static bool publish_cell_balancing(void);
static bool publish_events(void);

// A dropped broker connection makes every publish fail (QoS 0 returns -1 while the client
// is not connected), and publish_values() runs again every mqtt_publish_interval_ms, so
// logging each failure unconditionally repeats the same line for the whole outage. Pending
// events make it worse: they are deliberately retried until they go out, so publish_events()
// fails on every cycle from the moment EVENT_MQTT_DISCONNECT is raised until reconnect.
// Log the first failure of an outage only; publish_values() re-arms the latch after a
// complete successful cycle.
static bool publish_failure_logged = false;

static void log_publish_failure(const char* what) {
  if (!publish_failure_logged) {
    publish_failure_logged = true;
    logging.printf("%s MQTT msg could not be sent\n", what);
  }
}

/** Publish global values and call callbacks for specific modules */
static void publish_values(void) {

  if (mqtt_publish((topic_name + "/status").c_str(), "online", false) == false) {
    return;
  }

  if (publish_events() == false) {
    return;
  }

  if (publish_common_info() == false) {
    return;
  }

  if (mqtt_transmit_all_cellvoltages) {
    if (publish_cell_voltages() == false) {
      return;
    }
  }

  if (mqtt_transmit_all_cellvoltages) {
    if (publish_cell_balancing() == false) {
      return;
    }
  }

  // Whole cycle went out: arm the failure log again so the next outage is reported.
  publish_failure_logged = false;
}

static bool ha_common_info_published = false;
static bool ha_cell_voltages_published = false;
static bool ha_events_published = false;
static bool ha_buttons_published = false;

// Set from the MQTT_EVENT_CONNECTED handler, acted on by mqtt_client_loop(). The handler
// runs on the esp-mqtt task, so publishing the button configs from it would use the
// publish buffers concurrently with the publish cycle running on the MQTT task.
static volatile bool pending_buttons_discovery = false;

// FNV-1a over the version string. A hash rather than the string itself keeps this to a
// single primitive NVS entry, which is all that is needed to tell "same firmware as when
// discovery was last published" from "updated since". Never returns 0, so a missing NVS
// key (which reads back as 0) can never be mistaken for a matching signature.
uint32_t mqtt_firmware_signature(void) {
  uint32_t hash = 2166136261u;
  for (const char* c = version_number; *c != '\0'; c++) {
    hash = (hash ^ (uint8_t)*c) * 16777619u;
  }
  return (hash == 0u) ? 1u : hash;
}

// True once every discovery config that applies to this configuration has gone out. Cell
// voltage configs only count when they are actually published (MQTTCELLV), and they are
// only marked done once the cell count is known for every present battery.
static bool autodiscovery_complete(void) {
  return ha_common_info_published && ha_events_published && ha_buttons_published &&
         (ha_cell_voltages_published || !mqtt_transmit_all_cellvoltages);
}

// Clears the one-shot setting and records the firmware the configs were published from.
// The configs are retained at the broker, no need for emulator republishing at each boot.
static void store_autodiscovery_done(void) {
  ha_autodiscovery_enabled = false;  // switches the publish paths to state-only for this session
  BatteryEmulatorSettingsStore settings;
  settings.saveBool("HADISC", false);
  settings.saveUInt("HADISCFW", mqtt_firmware_signature());
  LOG_SET_NEXT_SEVERITY(5);  // notice
  logging.println("Home Assistant autodiscovery published");
}

struct SensorConfig {
  const char* default_entity_id;
  const char* name;
  const char* value_template;
  const char* unit;
  const char* device_class;

  // A function that returns true for the battery if it supports this config
  std::function<bool(Battery*)> condition;

  bool diagnostic;
};

bool mqtt_publish_heap_metrics = false;

static std::function<bool(Battery*)> always = [](Battery* b) {
  return true;
};
static std::function<bool(Battery*)> heap_metrics_enabled = [](Battery* b) {
  return mqtt_publish_heap_metrics;
};
static std::function<bool(Battery*)> supports_charged = [](Battery* b) {
  return b->supports_charged_energy();
};
static std::function<bool(Battery*)> supports_tesla_dcdc_metrics = [](Battery* b) {
  return b != nullptr && (user_selected_battery_type == BatteryType::TeslaModel3Y ||
                          user_selected_battery_type == BatteryType::TeslaModelSX);
};
static std::function<bool(Battery*)> supports_byd_autocal_metrics = [](Battery* b) {
  return b != nullptr && user_selected_battery_type == BatteryType::BydAtto3;
};
static std::function<bool(Battery*)> supports_byd_metrics = [](Battery* b) {
  return b != nullptr && user_selected_battery_type == BatteryType::BydAtto3;
};
static std::function<bool(Battery*)> supports_insulation = [](Battery* b) {
  return b != nullptr && b->supports_insulation_resistance();
};
static std::function<bool(Battery*)> supports_leaf_metrics = [](Battery* b) {
  return b != nullptr && user_selected_battery_type == BatteryType::NissanLeaf;
};

SensorConfig batterySensorConfigTemplate[] = {
    {"SOC", "SoC (scaled)", "", "%", "battery", always},
    {"SOC_real", "SoC (real)", "", "%", "battery", always},
    {"state_of_health", "State of Health", "", "%", "battery", always},
    {"temperature_min", "Temperature Min", "", "°C", "temperature", always},
    {"temperature_max", "Temperature Max", "", "°C", "temperature", always},
    {"stat_batt_power", "Battery Power", "", "W", "power", always},
    {"battery_current", "Battery Current", "", "A", "current", always},
    {"cell_max_voltage", "Cell Max Voltage", "", "V", "voltage", always},
    {"cell_min_voltage", "Cell Min Voltage", "", "V", "voltage", always},
    {"cell_voltage_delta", "Cell Voltage Delta", "", "mV", "voltage", always},
    {"battery_voltage", "Battery Voltage", "", "V", "voltage", always},
    {"total_capacity", "Total Capacity", "", "Wh", "energy", always},
    {"remaining_capacity", "Remaining Capacity (scaled)", "", "Wh", "energy", always},
    {"remaining_capacity_real", "Remaining Capacity (real)", "", "Wh", "energy", always},
    {"max_discharge_power", "Max Discharge Power", "", "W", "power", always},
    {"max_charge_power", "Max Charge Power", "", "W", "power", always},
    {"charged_energy", "Battery Charged Energy", "", "Wh", "energy", supports_charged},
    {"discharged_energy", "Battery Discharged Energy", "", "Wh", "energy", supports_charged},
    {"balancing_active_cells", "Balancing Cells", "", "", "", always},
    {"insulation_resistance", "Insulation Resistance", "", "kΩ", "", supports_insulation},
    {"balancing_status", "Balancing Status", "", "", "", always},
    {"charging_status", "Charging Status", "", "", "", always},
    {"charging_state", "Charging State", "", "", "", always},
    {"limiting_factor", "Limiting Factor", "", "", "", always},
    {"dc_dc_current", "DC-DC Current", "", "A", "current", supports_tesla_dcdc_metrics},
    {"dc_dc_voltage", "DC-DC Voltage", "", "V", "voltage", supports_tesla_dcdc_metrics},
    {"autocal_taper", "BYD Auto-cal: In Taper", "", "", "", supports_byd_autocal_metrics},
    {"autocal_dwell_s", "BYD Auto-cal: Dwell Time", "", "s", "duration", supports_byd_autocal_metrics},
    {"autocal_cooldown_ready", "BYD Auto-cal: Cooldown Ready", "", "", "", supports_byd_autocal_metrics},
    {"autocal_soc_drift", "BYD Auto-cal: SOC Drift", "", "%", "battery", supports_byd_autocal_metrics},
    {"min_cell_number", "Min Cell Number", "", "", "", supports_byd_metrics},
    {"max_cell_number", "Max Cell Number", "", "", "", supports_byd_metrics},
    {"leaf_hx", "Hx", "", "%", "", supports_leaf_metrics}};

SensorConfig globalSensorConfigTemplate[] = {{"bms_status", "BMS Status", "", "", "", always, true},
                                             {"pause_status", "Pause Status", "", "", "", always, true},
                                             {"max_charge_power_combined", "Max Charge Power (combined)", "", "W",
                                              "power", always, true},
                                             {"max_discharge_power_combined", "Max Discharge Power (combined)", "", "W",
                                              "power", always, true},
                                             {"event_level", "Event Level", "", "", "", always, true},
                                             {"emulator_status", "Emulator Status", "", "", "", always, true},
                                             {"emulator_uptime", "Emulator Uptime", "", "s", "duration", always, true},
                                             {"cpu_temp", "CPU Temperature", "", "°C", "temperature", always, true},
                                             {"software_version", "Emulator Version", "", "", "", always, true},
                                             {"heap_free", "Heap Free", "", "B", "data_size", heap_metrics_enabled,
                                              true},
                                             {"heap_max_block", "Heap Max Block", "", "B", "data_size",
                                              heap_metrics_enabled, true},
                                             {"heap_min_free", "Heap Min Free", "", "B", "data_size",
                                              heap_metrics_enabled, true},
                                             {"heap_fragmentation", "Heap Fragmentation", "", "%", "",
                                              heap_metrics_enabled, true}};

static std::list<SensorConfig> sensorConfigs;

void create_battery_sensor_configs() {
  for (auto& config : batterySensorConfigTemplate) {
    // Capture the pristine (battery #1) values before any mutation, so that generating
    // suffixed copies for battery #2 and #3 doesn't compound suffixes onto each other or
    // capture the wrong (already-suffixed) condition.
    const std::string base_default_entity_id = config.default_entity_id;
    const std::string base_name = config.name;
    auto original_condition = config.condition;

    config.value_template = strdup(("{{ value_json." + base_default_entity_id + " }}").c_str());
    sensorConfigs.push_back(config);

    if (batteries[1]) {
      SensorConfig config2 = config;
      config2.value_template = strdup(("{{ value_json." + base_default_entity_id + "_2 }}").c_str());
      config2.name = strdup((base_name + " 2").c_str());
      config2.default_entity_id = strdup((base_default_entity_id + "_2").c_str());
      config2.condition = [original_condition](Battery*) {
        return batteries[1] && original_condition(batteries[1]);
      };
      sensorConfigs.push_back(config2);
    }

    if (batteries[2]) {
      SensorConfig config3 = config;
      config3.value_template = strdup(("{{ value_json." + base_default_entity_id + "_3 }}").c_str());
      config3.name = strdup((base_name + " 3").c_str());
      config3.default_entity_id = strdup((base_default_entity_id + "_3").c_str());
      config3.condition = [original_condition](Battery*) {
        return batteries[2] && original_condition(batteries[2]);
      };
      sensorConfigs.push_back(config3);
    }
  }
}

void create_global_sensor_configs() {
  for (auto& config : globalSensorConfigTemplate) {
    config.value_template = strdup(("{{ value_json." + std::string(config.default_entity_id) + " }}").c_str());
    sensorConfigs.push_back(config);
  }
}

SensorConfig buttonConfigs[] = {{"BMSRESET", "Reset BMS", nullptr, nullptr, nullptr, nullptr},
                                {"PAUSE", "Pause charge/discharge", nullptr, nullptr, nullptr, nullptr},
                                {"RESUME", "Resume charge/discharge", nullptr, nullptr, nullptr, nullptr},
                                {"RESTART", "Reboot Emulator", nullptr, nullptr, nullptr, nullptr},
                                {"STOP", "Open Contactors", nullptr, nullptr, nullptr, nullptr}};

static String generateCommonInfoAutoConfigTopic(const char* default_entity_id) {
  return String(ha_autodiscovery_topic.c_str()) + "/sensor/" + topic_name + "/" + String(default_entity_id) + "/config";
}

static String generateCellVoltageAutoConfigTopic(int cell_number, String battery_suffix) {
  return String(ha_autodiscovery_topic.c_str()) + "/sensor/" + topic_name + "/cell_voltage" + battery_suffix +
         String(cell_number) + "/config";
}

static String generateEventsAutoConfigTopic(const char* default_entity_id) {
  return String(ha_autodiscovery_topic.c_str()) + "/sensor/" + topic_name + "/" + String(default_entity_id) + "/config";
}

static String generateButtonAutoConfigTopic(const char* subtype) {
  return String(ha_autodiscovery_topic.c_str()) + "/button/" + topic_name + "/" + String(subtype) + "/config";
}

static String generateSensorDefaultEntityId(const String& object_id) {
  return "sensor." + object_id;
}

void set_common_discovery_attributes(JsonDocument& doc) {
  doc["device"]["identifiers"][0] = device_id;
  doc["device"]["manufacturer"] = "DalaTech";
  doc["device"]["model"] = "Battery Emulator";
  doc["device"]["name"] = device_name;
  // Both are string literals with static storage duration, so ArduinoJson keeps them
  // zero-copy (stored by pointer) instead of allocating them in the document pool.
  doc["device"]["hw_version"] = esp32hal->name();
  doc["device"]["sw_version"] = version_number;
  doc["availability"][0]["topic"] = lwt_topic;
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";
  doc["enabled_by_default"] = true;
}

void set_battery_voltage_attributes(JsonDocument& doc, int i, int cellNumber, const String& state_topic,
                                    const String& default_entity_id_prefix, const String& battery_name_suffix) {
  const String default_entity_object_id = default_entity_id_prefix + "battery_voltage_cell" + String(cellNumber);
  doc["name"] = "Battery" + battery_name_suffix + " Cell Voltage " + String(cellNumber);
  doc["default_entity_id"] = generateSensorDefaultEntityId(default_entity_object_id);
  doc["unique_id"] = topic_name + default_entity_id_prefix + "_battery_voltage_cell" + String(cellNumber);
  doc["device_class"] = "voltage";
  doc["state_class"] = "measurement";
  doc["state_topic"] = state_topic;
  doc["unit_of_measurement"] = "V";
  doc["suggested_display_precision"] = 3;
  doc["icon"] = "mdi:current-dc";
  doc["value_template"] = "{{ value_json.cell_voltages[" + String(i) + "] }}";
}

static String generateButtonTopic(const char* subtype) {
  return topic_name + "/command/" + String(subtype);
}

static const char* get_balancing_status_text(balancing_status_enum status) {
  switch (status) {
    case BALANCING_STATUS_UNKNOWN:
      return "Unknown";
    case BALANCING_STATUS_ERROR:
      return "Error";
    case BALANCING_STATUS_READY:
      return "Ready";
    case BALANCING_STATUS_ACTIVE:
      return "Active";
    case BALANCING_STATUS_BLOCKED:
      return "Pending";  //Cells are flagged for balancing but the BMS is not bleeding them yet
    default:
      return "Unknown";
  }
}

void set_battery_attributes(JsonDocument& doc, const DATALAYER_BATTERY_TYPE& battery, const String& suffix,
                            bool supports_charged) {
  doc["SOC" + suffix] = ((float)battery.status.reported_soc) / 100.0f;
  doc["SOC_real" + suffix] = ((float)battery.status.real_soc) / 100.0f;
  doc["state_of_health" + suffix] = ((float)battery.status.soh_pptt) / 100.0f;
  doc["temperature_min" + suffix] = ((float)((int16_t)battery.status.temperature_min_dC)) / 10.0f;
  doc["temperature_max" + suffix] = ((float)((int16_t)battery.status.temperature_max_dC)) / 10.0f;
  doc["stat_batt_power" + suffix] = ((float)((int32_t)battery.status.active_power_W));
  doc["battery_current" + suffix] = ((float)((int16_t)battery.status.current_dA)) / 10.0f;
  doc["battery_voltage" + suffix] = ((float)battery.status.voltage_dV) / 10.0f;
  if (battery.info.number_of_cells != 0u && battery.status.cell_voltages_mV[battery.info.number_of_cells - 1] != 0u) {
    doc["cell_max_voltage" + suffix] = ((float)battery.status.cell_max_voltage_mV) / 1000.0f;
    doc["cell_min_voltage" + suffix] = ((float)battery.status.cell_min_voltage_mV) / 1000.0f;
    doc["cell_voltage_delta" + suffix] =
        ((float)battery.status.cell_max_voltage_mV) - ((float)battery.status.cell_min_voltage_mV);
  }
  doc["total_capacity" + suffix] = ((float)battery.info.total_capacity_Wh);
  doc["remaining_capacity_real" + suffix] = ((float)battery.status.remaining_capacity_Wh);
  doc["remaining_capacity" + suffix] = ((float)battery.status.reported_remaining_capacity_Wh);
  doc["max_discharge_power" + suffix] = ((float)battery.status.max_discharge_power_W);
  doc["max_charge_power" + suffix] = ((float)battery.status.max_charge_power_W);
  // Omit until the integration has decoded a valid sample so HA shows "unknown"
  // instead of a false 0 kOhm at boot.
  if (battery.status.insulation_resistance_available) {
    doc["insulation_resistance" + suffix] = battery.status.insulation_resistance_kOhm;
  }

  if (supports_charged) {
    if (battery.status.total_charged_battery_Wh != 0 && battery.status.total_discharged_battery_Wh != 0) {
      doc["charged_energy" + suffix] = ((float)battery.status.total_charged_battery_Wh);
      doc["discharged_energy" + suffix] = ((float)battery.status.total_discharged_battery_Wh);
    }
  }

  // Add balancing data
  uint16_t active_cells = 0;
  if (battery.info.number_of_cells != 0u) {
    for (size_t i = 0; i < battery.info.number_of_cells; ++i) {
      if (battery.status.cell_balancing_status[i]) {
        active_cells++;
      }
    }
  }
  doc["balancing_active_cells" + suffix] = active_cells;
  doc["balancing_status" + suffix] = get_balancing_status_text(battery.status.balancing_status);
  // Limit flags are system-scoped, computed by update_reported_values().
  const auto& limits = datalayer.battery.settings;
  doc["charging_status" + suffix] =
      get_charging_status_text(battery.status.current_dA, limits.inverter_limits_charge,
                               limits.inverter_limits_discharge, limits.user_settings_limit_charge,
                               limits.user_settings_limit_discharge);
  ChargingState charging_state = get_charging_state(battery.status.current_dA);
  doc["charging_state" + suffix] = charging_state_to_text(charging_state);
  doc["limiting_factor" + suffix] = limiting_factor_to_text(
      get_limiting_factor(charging_state, limits.inverter_limits_charge, limits.inverter_limits_discharge,
                          limits.user_settings_limit_charge, limits.user_settings_limit_discharge));
  if (suffix.length() == 0u && supports_leaf_metrics(batteries[0])) {
    // Omit until a group 1 reply with a known layout has been decoded, so HA shows "unknown".
    if (datalayer_extended.nissanleaf.battery_HX_pptt != 0u) {
      doc["leaf_hx"] = ((float)datalayer_extended.nissanleaf.battery_HX_pptt) / 100.0f;
    }
  }
  if (suffix.length() == 0u && supports_tesla_dcdc_metrics(batteries[0])) {
    doc["dc_dc_current" + suffix] = static_cast<float>(datalayer_extended.tesla.battery_dcdcLvOutputCurrent) * 0.1f;
    doc["dc_dc_voltage" + suffix] = static_cast<float>(datalayer_extended.tesla.battery_dcdcLvBusVolt) * 0.0390625f;
  }
  if (supports_byd_autocal_metrics(batteries[0])) {
    const DATALAYER_INFO_BYDATTO3& byd = (suffix == "_2") ? datalayer_extended.bydAtto3_2 : datalayer_extended.bydAtto3;
    doc["autocal_taper" + suffix] = byd.autocal_crit_taper;
    doc["autocal_dwell_s" + suffix] = byd.autocal_dwell_accumulated_ms / 1000u;
    doc["autocal_cooldown_ready" + suffix] = byd.autocal_crit_cooldown_ready;
    doc["autocal_soc_drift" + suffix] = byd.autocal_drift_percent;
  }
  if (supports_byd_metrics(batteries[0])) {
    const DATALAYER_INFO_BYDATTO3& byd = (suffix == "_2") ? datalayer_extended.bydAtto3_2 : datalayer_extended.bydAtto3;
    doc["min_cell_number" + suffix] = byd.BMS_min_cell_voltage_number;
    doc["max_cell_number" + suffix] = byd.BMS_max_cell_voltage_number;
  }
}

static std::vector<EventData> order_events;

// Returns the MDI icon for an info/global discovery sensor, or nullptr to leave HA's
// device-class default. Per-entity matches win over device-class matches. strncmp
// prefixes also cover the "_2" double-battery entity variants.
static const char* sensor_discovery_icon(const char* entity_id, const char* device_class) {
  if (entity_id != nullptr) {
    if (strncmp(entity_id, "balancing_active_cells", strlen("balancing_active_cells")) == 0 ||
        strncmp(entity_id, "balancing_status", strlen("balancing_status")) == 0) {
      return "mdi:fuel-cell";
    }
    if (strncmp(entity_id, "bms_status", strlen("bms_status")) == 0) {
      return "mdi:information-box-outline";
    }
    if (strncmp(entity_id, "insulation_resistance", strlen("insulation_resistance")) == 0) {
      return "mdi:resistor";
    }
    if (strncmp(entity_id, "heap_", strlen("heap_")) == 0) {
      return "mdi:memory";
    }
    if (strcmp(entity_id, "leaf_hx") == 0) {
      return "mdi:battery-heart-variant";
    }
    if (strncmp(entity_id, "charging_state", strlen("charging_state")) == 0) {
      return "mdi:home-battery";
    }
    if (strncmp(entity_id, "limiting_factor", strlen("limiting_factor")) == 0) {
      return "mdi:home-battery-outline";
    }
    if (strncmp(entity_id, "emulator_status", strlen("emulator_status")) == 0 ||
        strncmp(entity_id, "event_level", strlen("event_level")) == 0) {
      return "mdi:information-outline";
    }
    if (strncmp(entity_id, "pause_status", strlen("pause_status")) == 0) {
      return "mdi:battery-outline";
    }
    if (strcmp(entity_id, "software_version") == 0) {
      return "mdi:tag-outline";
    }
  }
  if (device_class != nullptr) {
    if (strcmp(device_class, "voltage") == 0)
      return "mdi:current-dc";
    if (strcmp(device_class, "current") == 0)
      return "mdi:equal";
  }
  return nullptr;
}

// Returns the MDI icon for a command button, or nullptr.
static const char* button_discovery_icon(const char* command) {
  if (strcmp(command, "RESTART") == 0)
    return "mdi:restart";
  if (strcmp(command, "BMSRESET") == 0)
    return "mdi:star-four-points-box-outline";
  if (strcmp(command, "PAUSE") == 0)
    return "mdi:battery-minus-outline";
  if (strcmp(command, "RESUME") == 0)
    return "mdi:battery-sync-outline";
  if (strcmp(command, "STOP") == 0)
    return "mdi:battery-remove-outline";
  return nullptr;
}

static bool publish_common_info(void) {
  static JsonDocument doc;
  static String state_topic = topic_name + "/info";

  //  if(ha_autodiscovery_enabled) {

  if (ha_autodiscovery_enabled && !ha_common_info_published) {
    for (auto& config : sensorConfigs) {
      if (!config.condition(batteries[0])) {
        continue;
      }

      doc["name"] = config.name;
      doc["state_topic"] = state_topic;
      doc["unique_id"] = topic_name + "_" + String(config.default_entity_id);
      const String default_entity_object_id = default_entity_id_prefix + String(config.default_entity_id);
      doc["default_entity_id"] = generateSensorDefaultEntityId(default_entity_object_id);
      doc["value_template"] = config.value_template;
      if (config.unit != nullptr && strlen(config.unit) > 0) {
        doc["unit_of_measurement"] = config.unit;
      }
      if (config.device_class != nullptr && strlen(config.device_class) > 0) {
        doc["device_class"] = config.device_class;
        doc["state_class"] = "measurement";
      }
      // "balancing_active_cells" is a numeric count with no device_class, so it misses the
      // state_class assignment above. Mark it as a measurement explicitly so Home Assistant
      // records long-term statistics for it. (strncmp also covers the "_2" double-battery variant.)
      if (strncmp(config.default_entity_id, "balancing_active_cells", strlen("balancing_active_cells")) == 0) {
        doc["state_class"] = "measurement";
      }
      // "energy" device_class is only valid with state_class total / total_increasing, never
      // "measurement" — HA rejects the combination. The capacity sensors represent a current
      // stored amount, so use "energy_storage" (compatible with "measurement") instead. The
      // charged/discharged sensors are genuine lifetime totals, so keep "energy" but use
      // "total_increasing". (strncmp also covers the "_2" double-battery variants.)
      if (strncmp(config.default_entity_id, "total_capacity", strlen("total_capacity")) == 0 ||
          strncmp(config.default_entity_id, "remaining_capacity", strlen("remaining_capacity")) == 0) {
        doc["device_class"] = "energy_storage";
      } else if (strncmp(config.default_entity_id, "charged_energy", strlen("charged_energy")) == 0 ||
                 strncmp(config.default_entity_id, "discharged_energy", strlen("discharged_energy")) == 0) {
        doc["state_class"] = "total_increasing";
      }
      // Cell min/max voltages: show 3 decimals in HA so they don't round to the same integer
      // on display. Precision is intentionally not applied to battery_voltage. (Icons are
      // handled centrally below.)
      if (strncmp(config.default_entity_id, "cell_max_voltage", strlen("cell_max_voltage")) == 0 ||
          strncmp(config.default_entity_id, "cell_min_voltage", strlen("cell_min_voltage")) == 0) {
        doc["suggested_display_precision"] = 3;
      }
      // "leaf_hx" is a percentage with no matching device_class, so it misses the state_class
      // assignment above too. Mark it as a measurement and show two decimals, like LeafSpy does.
      if (strcmp(config.default_entity_id, "leaf_hx") == 0) {
        doc["state_class"] = "measurement";
        doc["suggested_display_precision"] = 2;
      }
      // "heap_fragmentation" is a percentage with no matching device_class either. Mark it as a
      // measurement and show one decimal, like the ESPHome debug sensor does.
      if (strcmp(config.default_entity_id, "heap_fragmentation") == 0) {
        doc["state_class"] = "measurement";
        doc["suggested_display_precision"] = 1;
      }
      // Battery current and both SOC sensors: show 1 decimal in HA. (strncmp also covers the
      // "_2" double-battery variants.)
      if (strncmp(config.default_entity_id, "battery_current", strlen("battery_current")) == 0 ||
          strncmp(config.default_entity_id, "SOC", strlen("SOC")) == 0) {
        doc["suggested_display_precision"] = 1;
      }
      // Entity icons (centralized): status sensors by entity id, all voltage/current sensors
      // by device_class. This also covers the balancing and cell min/max entities above.
      {
        const char* icon = sensor_discovery_icon(config.default_entity_id, config.device_class);
        if (icon != nullptr) {
          doc["icon"] = icon;
        }
      }
      if (config.diagnostic) {
        doc["entity_category"] = "diagnostic";
      }
      set_common_discovery_attributes(doc);
      serializeJson(doc, mqtt_msg);
      if (mqtt_publish(generateCommonInfoAutoConfigTopic(config.default_entity_id).c_str(), mqtt_msg, true)) {
        ha_common_info_published = true;
      } else {
        return false;
      }
      doc.clear();
    }

  } else {
    doc["bms_status"] = getBMSStatus(datalayer.system.status.system_status);
    doc["pause_status"] = get_emulator_pause_status();
    // Enforced system limits from the combined view; the per-battery
    // max_*_power keys carry each pack's own BMS headroom.
    doc["max_charge_power_combined"] = ((float)datalayer.battery.combined.status.max_charge_power_W);
    doc["max_discharge_power_combined"] = ((float)datalayer.battery.combined.status.max_discharge_power_W);

    //only publish these values if BMS is active and we are comunication with the battery (can send CAN messages to the battery)
    if (datalayer.battery.pack[0].status.CAN_battery_still_alive && allowed_to_send_CAN && esp32hal->system_booted_up()) {
      set_battery_attributes(doc, datalayer.battery.pack[0], "", batteries[0]->supports_charged_energy());
    }

    if (batteries[1]) {
      //only publish these values if BMS is active and we are comunication with the battery (can send CAN messages to the battery)
      if (datalayer.battery.pack[1].status.CAN_battery_still_alive && allowed_to_send_CAN && esp32hal->system_booted_up()) {
        set_battery_attributes(doc, datalayer.battery.pack[1], "_2", batteries[1]->supports_charged_energy());
      }
    }

    if (batteries[2]) {
      //only publish these values if BMS is active and we are comunication with the battery (can send CAN messages to the battery)
      if (datalayer.battery.pack[2].status.CAN_battery_still_alive && allowed_to_send_CAN && esp32hal->system_booted_up()) {
        set_battery_attributes(doc, datalayer.battery.pack[2], "_3", batteries[2]->supports_charged_energy());
      }
    }

    doc["event_level"] = get_event_level_string(get_event_level());
    doc["emulator_status"] = get_emulator_status_string(get_emulator_status());
    // Static identity of the running binary. Published on every cycle (the topic is not
    // retained, so a single publish would be lost on a Home Assistant restart).
    doc["hardware"] = esp32hal->name();
    doc["software_version"] = version_number;
    if (datalayer.system.info.CPU_measurement_enabled) {
      doc["cpu_temp"] = datalayer.system.info.CPU_temperature;
    }
    doc["emulator_uptime"] = millis64() / 1000;

    // Internal-RAM heap diagnostics. Same sources and fragmentation formula as the ESPHome
    // debug component, so the values are directly comparable with an ESPHome node's.
    if (mqtt_publish_heap_metrics) {
      const uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      const uint32_t heap_max_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
      doc["heap_free"] = heap_free;
      doc["heap_max_block"] = heap_max_block;
      doc["heap_min_free"] = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
      // Share of the free heap that is not reachable as one contiguous block. Guarded
      // against a zero free heap so no NaN is ever published.
      if (heap_free > 0u) {
        doc["heap_fragmentation"] = 100.0f - (100.0f * (float)heap_max_block / (float)heap_free);
      }
    }

    serializeJson(doc, mqtt_msg);
    if (mqtt_publish(state_topic.c_str(), mqtt_msg, false) == false) {
      log_publish_failure("Common info");
      return false;
    }
    doc.clear();
  }
  return true;
}

static bool publish_cell_voltages(void) {
  static JsonDocument doc;
  static String state_topic = topic_name + "/spec_data";
  static String state_topic_2 = topic_name + "/spec_data_2";
  static String state_topic_3 = topic_name + "/spec_data_3";

  if (ha_autodiscovery_enabled) {
    bool successfully_published = false;
    if (ha_cell_voltages_published == false) {

      // If the cell voltage number isn't initialized...
      if (datalayer.battery.pack[0].info.number_of_cells != 0u) {

        for (int i = 0; i < datalayer.battery.pack[0].info.number_of_cells; i++) {
          int cellNumber = i + 1;
          set_battery_voltage_attributes(doc, i, cellNumber, state_topic, default_entity_id_prefix, "");
          set_common_discovery_attributes(doc);

          serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));
          if (mqtt_publish(generateCellVoltageAutoConfigTopic(cellNumber, "").c_str(), mqtt_msg, true) == false) {
            return false;
          }
        }
        successfully_published = true;
        doc.clear();  // clear after sending autoconfig
      }

      if (batteries[1]) {
        successfully_published = false;
        // TODO: Combine this identical block with the previous one.
        // If the cell voltage number isn't initialized...
        if (datalayer.battery.pack[1].info.number_of_cells != 0u) {

          for (int i = 0; i < datalayer.battery.pack[1].info.number_of_cells; i++) {
            int cellNumber = i + 1;
            set_battery_voltage_attributes(doc, i, cellNumber, state_topic_2, default_entity_id_prefix + "2_", " 2");
            set_common_discovery_attributes(doc);

            serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));
            if (mqtt_publish(generateCellVoltageAutoConfigTopic(cellNumber, "_2_").c_str(), mqtt_msg, true) == false) {
              return false;
            }
          }
          successfully_published = true;
          doc.clear();  // clear after sending autoconfig
        }
      }

      if (batteries[2]) {
        successfully_published = false;
        // If the cell voltage number isn't initialized...
        if (datalayer.battery.pack[2].info.number_of_cells != 0u) {

          for (int i = 0; i < datalayer.battery.pack[2].info.number_of_cells; i++) {
            int cellNumber = i + 1;
            set_battery_voltage_attributes(doc, i, cellNumber, state_topic_3, default_entity_id_prefix + "3_", " 3");
            set_common_discovery_attributes(doc);

            serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));
            if (mqtt_publish(generateCellVoltageAutoConfigTopic(cellNumber, "_3_").c_str(), mqtt_msg, true) == false) {
              return false;
            }
          }
          successfully_published = true;
          doc.clear();  // clear after sending autoconfig
        }
      }
    }
    if (successfully_published) {
      ha_cell_voltages_published = true;
    }
  }

  // If cell voltages have been populated...
  if (datalayer.battery.pack[0].info.number_of_cells != 0u &&
      datalayer.battery.pack[0].status.cell_voltages_mV[datalayer.battery.pack[0].info.number_of_cells - 1] != 0u) {

    JsonArray cell_voltages = doc["cell_voltages"].to<JsonArray>();
    for (size_t i = 0; i < datalayer.battery.pack[0].info.number_of_cells; ++i) {
      cell_voltages.add(((float)datalayer.battery.pack[0].status.cell_voltages_mV[i]) / 1000.0f);
    }

    serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));

    if (!mqtt_publish(state_topic.c_str(), mqtt_msg, false)) {
      log_publish_failure("Cell voltage");
      return false;
    }
    doc.clear();
  }

  if (batteries[1]) {
    // If cell voltages have been populated...
    if (datalayer.battery.pack[1].info.number_of_cells != 0u &&
        datalayer.battery.pack[1].status.cell_voltages_mV[datalayer.battery.pack[1].info.number_of_cells - 1] != 0u) {

      JsonArray cell_voltages = doc["cell_voltages"].to<JsonArray>();
      for (size_t i = 0; i < datalayer.battery.pack[1].info.number_of_cells; ++i) {
        cell_voltages.add(((float)datalayer.battery.pack[1].status.cell_voltages_mV[i]) / 1000.0f);
      }

      serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));

      if (!mqtt_publish(state_topic_2.c_str(), mqtt_msg, false)) {
        log_publish_failure("Cell voltage");
        return false;
      }
      doc.clear();
    }
  }

  if (batteries[2]) {
    // If cell voltages have been populated...
    if (datalayer.battery.pack[2].info.number_of_cells != 0u &&
        datalayer.battery.pack[2].status.cell_voltages_mV[datalayer.battery.pack[2].info.number_of_cells - 1] != 0u) {

      JsonArray cell_voltages = doc["cell_voltages"].to<JsonArray>();
      for (size_t i = 0; i < datalayer.battery.pack[2].info.number_of_cells; ++i) {
        cell_voltages.add(((float)datalayer.battery.pack[2].status.cell_voltages_mV[i]) / 1000.0f);
      }

      serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));

      if (!mqtt_publish(state_topic_3.c_str(), mqtt_msg, false)) {
        log_publish_failure("Cell voltage");
        return false;
      }
      doc.clear();
    }
  }
  return true;
}

static bool publish_cell_balancing(void) {
  static JsonDocument doc;
  static String state_topic = topic_name + "/balancing_data";
  static String state_topic_2 = topic_name + "/balancing_data_2";
  static String state_topic_3 = topic_name + "/balancing_data_3";

  // If cell balancing data is available...
  if (datalayer.battery.pack[0].info.number_of_cells != 0u) {

    JsonArray cell_balancing = doc["cell_balancing"].to<JsonArray>();
    for (size_t i = 0; i < datalayer.battery.pack[0].info.number_of_cells; ++i) {
      cell_balancing.add(datalayer.battery.pack[0].status.cell_balancing_status[i]);
    }

    serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));

    if (!mqtt_publish(state_topic.c_str(), mqtt_msg, false)) {
      log_publish_failure("Cell balancing");
      return false;
    }
    doc.clear();
  }

  // Handle second battery if available
  if (batteries[1]) {
    if (datalayer.battery.pack[1].info.number_of_cells != 0u) {

      JsonArray cell_balancing = doc["cell_balancing"].to<JsonArray>();
      for (size_t i = 0; i < datalayer.battery.pack[1].info.number_of_cells; ++i) {
        cell_balancing.add(datalayer.battery.pack[1].status.cell_balancing_status[i]);
      }

      serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));

      if (!mqtt_publish(state_topic_2.c_str(), mqtt_msg, false)) {
        log_publish_failure("Cell balancing");
        return false;
      }
      doc.clear();
    }
  }

  // Handle third battery if available
  if (batteries[2]) {
    if (datalayer.battery.pack[2].info.number_of_cells != 0u) {

      JsonArray cell_balancing = doc["cell_balancing"].to<JsonArray>();
      for (size_t i = 0; i < datalayer.battery.pack[2].info.number_of_cells; ++i) {
        cell_balancing.add(datalayer.battery.pack[2].status.cell_balancing_status[i]);
      }

      serializeJson(doc, mqtt_msg, sizeof(mqtt_msg));

      if (!mqtt_publish(state_topic_3.c_str(), mqtt_msg, false)) {
        log_publish_failure("Cell balancing");
        return false;
      }
      doc.clear();
    }
  }
  return true;
}

bool publish_events() {
  static JsonDocument doc;
  static String state_topic = topic_name + "/events";
  if (ha_autodiscovery_enabled && !ha_events_published) {

    doc["name"] = "Event";
    doc["state_topic"] = state_topic;
    doc["unique_id"] = topic_name + "_event";
    doc["default_entity_id"] = generateSensorDefaultEntityId(default_entity_id_prefix + "event");
    doc["value_template"] =
        "{{ value_json.event_type ~ ' (c:' ~ value_json.count ~ ',m:' ~  value_json.millis ~ ') ' ~ value_json.message "
        "}}";
    doc["json_attributes_topic"] = state_topic;
    doc["json_attributes_template"] = "{{ value_json | tojson }}";
    doc["icon"] = "mdi:information-outline";
    doc["entity_category"] = "diagnostic";
    set_common_discovery_attributes(doc);
    serializeJson(doc, mqtt_msg);
    if (mqtt_publish(generateEventsAutoConfigTopic("event").c_str(), mqtt_msg, true)) {
      ha_events_published = true;
    } else {
      return false;
    }

    doc.clear();
  } else {
    const EVENTS_STRUCT_TYPE* event_pointer;

    //clear the vector
    order_events.clear();
    // Collect all events
    for (int i = 0; i < EVENT_NOF_EVENTS; i++) {
      event_pointer = get_event_pointer((EVENTS_ENUM_TYPE)i);
      if (event_pointer->occurences > 0 && !event_pointer->MQTTpublished) {
        order_events.push_back({static_cast<EVENTS_ENUM_TYPE>(i), event_pointer});
      }
    }
    // Sort events by timestamp
    std::sort(order_events.begin(), order_events.end(), compareEventsByTimestampAsc);

    for (const auto& event : order_events) {

      EVENTS_ENUM_TYPE event_handle = event.event_handle;
      event_pointer = event.event_pointer;

      doc["event_type"] = String(get_event_enum_string(event_handle));
      doc["severity"] = String(get_event_level_string(event_handle));
      doc["count"] = String(event_pointer->occurences);
      doc["data"] = String(event_pointer->data);
      doc["message"] = get_event_message_string(event_handle);
      doc["millis"] = String(event_pointer->timestamp);

      serializeJson(doc, mqtt_msg);
      if (!mqtt_publish(state_topic.c_str(), mqtt_msg, false)) {
        log_publish_failure("Event");
        return false;
      } else {
        set_event_MQTTpublished(event_handle);
      }
      doc.clear();
      //clear the vector
      order_events.clear();
    }
  }
  return true;
}

static bool publish_buttons_discovery(void) {
  if (ha_autodiscovery_enabled) {
    if (ha_buttons_published == false) {
      logging.println("Publishing buttons discovery");

      static JsonDocument doc;
      for (int i = 0; i < sizeof(buttonConfigs) / sizeof(buttonConfigs[0]); i++) {
        SensorConfig& config = buttonConfigs[i];
        doc["name"] = config.name;
        doc["unique_id"] = default_entity_id_prefix + config.default_entity_id;
        doc["command_topic"] = generateButtonTopic(config.default_entity_id);
        {
          const char* icon = button_discovery_icon(config.default_entity_id);
          if (icon != nullptr) {
            doc["icon"] = icon;
          }
        }
        // Rebooting the emulator is a maintenance action on the emulator itself, not a
        // battery control like the pause/resume/stop buttons.
        if (strcmp(config.default_entity_id, "RESTART") == 0) {
          doc["entity_category"] = "diagnostic";
        }
        set_common_discovery_attributes(doc);
        serializeJson(doc, mqtt_msg);
        if (mqtt_publish(generateButtonAutoConfigTopic(config.default_entity_id).c_str(), mqtt_msg, true)) {
          ha_buttons_published = true;
        } else {
          return false;
        }
        doc.clear();
      }
    }
  }
  return true;
}

static void subscribe() {
  esp_mqtt_client_subscribe(client, (topic_name + "/command/+").c_str(), 1);
}

void mqtt_message_received(char* topic_raw, int topic_len, char* data, int data_len) {

  char* topic = strndup(topic_raw, topic_len);

  logging.printf("MQTT message arrived: [%.*s]\n", topic_len, topic);

  if (remote_bms_reset) {
    if (strcmp(topic, generateButtonTopic("BMSRESET").c_str()) == 0) {
      logging.println("Triggering BMS reset");
      start_bms_reset();
    }
  }

  if (strcmp(topic, generateButtonTopic("PAUSE").c_str()) == 0) {
    setBatteryPause(true, false);
  }

  if (strcmp(topic, generateButtonTopic("RESUME").c_str()) == 0) {
    setBatteryPause(false, false, EquipmentStop::RESUME);
  }

  if (strcmp(topic, generateButtonTopic("RESTART").c_str()) == 0) {
    hold_pins_across_reset();
    graceful_restart();
  }

  if (strcmp(topic, generateButtonTopic("STOP").c_str()) == 0) {
    setBatteryPause(true, false, EquipmentStop::STOP);
  }

  if (strcmp(topic, generateButtonTopic("SET_LIMITS").c_str()) == 0) {
    JsonDocument doc;
    char* data_str = strndup(data, data_len);
    deserializeJson(doc, data_str);

    if (doc["max_charge"].is<int>()) {
      datalayer.battery.settings.max_remote_set_charge_dA = doc["max_charge"];
      datalayer.battery.settings.remote_settings_limit_charge = true;
    } else {
      datalayer.battery.settings.max_remote_set_charge_dA = 0;
      datalayer.battery.settings.remote_settings_limit_charge = false;
    }

    if (doc["max_discharge"].is<int>()) {
      datalayer.battery.settings.max_remote_set_discharge_dA = doc["max_discharge"];
      datalayer.battery.settings.remote_settings_limit_discharge = true;
    } else {
      datalayer.battery.settings.max_remote_set_discharge_dA = 0;
      datalayer.battery.settings.remote_settings_limit_discharge = false;
    }

    if (doc["timeout"].is<int>()) {
      datalayer.battery.settings.remote_set_timeout = doc["timeout"].as<int>() * 1000;
    } else {
      datalayer.battery.settings.remote_set_timeout = 30000;
    }

    datalayer.battery.settings.remote_set_timestamp = millis();

    free(data_str);
  }

  free(topic);
}

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
      clear_event(EVENT_MQTT_DISCONNECT);
      set_event(EVENT_MQTT_CONNECT, 0);

      // Handed to the MQTT task instead of published here, see pending_buttons_discovery.
      pending_buttons_discovery = true;
      subscribe();
      logging.println("MQTT connected");
      break;
    case MQTT_EVENT_DISCONNECTED:
      set_event(EVENT_MQTT_DISCONNECT, 0);  // also printing a log entry
      break;
    case MQTT_EVENT_DATA:
      mqtt_message_received(event->topic, event->topic_len, event->data, event->data_len);
      break;
    case MQTT_EVENT_ERROR:
      // logging.println("MQTT_ERROR");
      // logging.print("reported from esp-tls");
      // logging.println(event->error_handle->esp_tls_last_esp_err);
      // logging.print("reported from tls stack");
      // logging.println(event->error_handle->esp_tls_stack_err);
      // logging.print("captured as transport's socket errno");
      // logging.println(strerror(event->error_handle->esp_transport_sock_errno));
      break;
    case MQTT_EVENT_SUBSCRIBED:
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      break;
    case MQTT_EVENT_PUBLISHED:
      break;
    case MQTT_EVENT_BEFORE_CONNECT:
      break;
    case MQTT_EVENT_DELETED:
      break;
    case MQTT_USER_EVENT:
      break;
    case MQTT_EVENT_ANY:
      break;
  }
}

bool init_mqtt(void) {

  if (batteries[0] == nullptr) {
    logging.println("ERROR: No battery selected. Aborting MQTT initialization");
    return false;
  }

  if (ha_autodiscovery_enabled) {
    create_battery_sensor_configs();
    create_global_sensor_configs();
  }

  String hostname = String(WiFi.getHostname());
  topic_name = hostname;
  default_entity_id_prefix = hostname + "_";
  device_name = hostname;
  device_id = hostname;

  String clientId = String("BatteryEmulatorClient-") + WiFi.getHostname();

  mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  mqtt_cfg.broker.address.hostname = mqtt_server.c_str();
  mqtt_cfg.broker.address.port = mqtt_port;
  mqtt_cfg.credentials.client_id = clientId.c_str();
  mqtt_cfg.credentials.username = mqtt_user.c_str();
  mqtt_cfg.credentials.authentication.password = mqtt_password.c_str();
  lwt_topic = topic_name + "/status";
  mqtt_cfg.session.last_will.topic = lwt_topic.c_str();
  mqtt_cfg.session.last_will.qos = 1;
  mqtt_cfg.session.last_will.retain = true;
  mqtt_cfg.session.last_will.msg = "offline";
  mqtt_cfg.session.last_will.msg_len = strlen(mqtt_cfg.session.last_will.msg);
  mqtt_cfg.network.timeout_ms = mqtt_timeout_ms;
  // Bound heap growth of the esp-mqtt outbox when the broker is unreachable.
  mqtt_cfg.outbox.limit = MQTT_OUTBOX_LIMIT_BYTES;
  client = esp_mqtt_client_init(&mqtt_cfg);

  if (client == nullptr) {
    return false;
  }

  if (esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, client) != ESP_OK) {
    return false;
  }

  return true;
}

void mqtt_client_loop(void) {
  // Only attempt to publish/reconnect MQTT if Wi-Fi is connected and checkTimmer is elapsed
  if (check_global_timer.elapsed() && WiFi.status() == WL_CONNECTED) {

    if (client_started == false) {
      // Configure timer with the loaded interval on first use
      publish_global_timer = MyTimer(mqtt_publish_interval_ms);
      esp_mqtt_client_start(client);
      client_started = true;
      logging.println("MQTT initialized");
      return;
    }

    // Requested by the MQTT_EVENT_CONNECTED handler, published here so that the publish
    // buffers stay single-threaded. Retried on the next pass if the publish fails.
    if (pending_buttons_discovery && !ota_active && publish_buttons_discovery()) {
      pending_buttons_discovery = false;
    }

    // Skip publishing if OTA update is in progress to avoid interference
    if (publish_global_timer.elapsed() && !ota_active) {
      publish_values();

      // One-shot autodiscovery: as soon as every applicable config is out and retained at
      // the broker, clear the setting so it is not republished on every boot.
      if (ha_autodiscovery_enabled && autodiscovery_complete()) {
        store_autodiscovery_done();
      }
    }
  }
}

bool mqtt_publish(const char* topic, const char* mqtt_msg, bool retain) {
  int msg_id = esp_mqtt_client_publish(client, topic, mqtt_msg, strlen(mqtt_msg), MQTT_QOS, retain);
  return msg_id > -1;
}
