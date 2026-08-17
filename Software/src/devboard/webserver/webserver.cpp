#include "webserver.h"
#include <Preferences.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../../battery/BATTERIES.h"
#include "../../battery/Battery.h"
#include "../../battery/Shunt.h"
#include "../../charger/CHARGERS.h"
#include "../../communication/can/comm_can.h"
#include "../../communication/contactorcontrol/comm_contactorcontrol.h"
#include "../../communication/equipmentstopbutton/comm_equipmentstopbutton.h"
#include "../../communication/nvm/comm_nvm.h"
#include "../../datalayer/datalayer.h"
#include "../../datalayer/datalayer_extended.h"
#include "../../devboard/safety/safety.h"
#include "../../inverter/INVERTERS.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "../../system_settings.h"
#include "../sdcard/sdcard.h"
#include "../utils/events.h"
#include "../utils/led_handler.h"
#include "../utils/millis64.h"
#include "../utils/time_format.h"
#include "../utils/timer.h"
#include "../utils/version.h"
#include "esp_task_wdt.h"
#include "favicon.h"
#include "../wifi/wifi.h"
#include "static_assets.h"
#include "webserver_can_streaming.h"

#include <string>

std::string http_username;
std::string http_password;

bool webserver_auth = false;
static constexpr int HTTP_STATUS_OK = 200;
static constexpr int HTTP_STATUS_BAD_REQUEST = 400;
static constexpr int HTTP_STATUS_INTERNAL_SERVER_ERROR = 500;
// Sanity cap on a reassembled JSON POST body. The full settings save is ~2-3 KB
// (~104 fields); 16 KB leaves generous headroom while bounding the per-request
// heap allocation against a malformed or hostile Content-Length.
static constexpr size_t MAX_JSON_POST_BODY_BYTES = 16384;
static constexpr const char* CONTENT_TYPE_JSON = "application/json";
static constexpr const char* ASSET_PATH_SHELL = "/index.html";
// datalayer integer scalings: pptt is percent x 100, deci-units are unit x 10
static constexpr float PPTT_PER_PERCENT = 100.0f;
static constexpr float DECI_PER_UNIT = 10.0f;
static constexpr float MS_PER_SECOND = 1000.0f;
// BYD auto-calibrate drift threshold accepted range (out-of-range is ignored, matching the GET origin)
static constexpr int BYD_AUTOCAL_DRIFT_MIN = 1;
static constexpr int BYD_AUTOCAL_DRIFT_MAX = 20;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncAuthenticationMiddleware web_auth_middleware;

// Measure OTA progress
unsigned long ota_progress_millis = 0;

#include "advanced_api.h"
#include "canreplay_api.h"
#include "cellmonitor_api.h"
#include "events_api.h"
#include "logging_api.h"
#include "settings_api.h"
#include "web_json.h"

MyTimer ota_timeout_timer = MyTimer(15000);
bool ota_active = false;

String importedLogs = "";      // Store the uploaded logfile contents in RAM
bool isReplayRunning = false;  // Global flag to track replay state
static constexpr uint32_t CAN_REPLAY_TASK_STACK_SIZE = 8192;

// True when user has updated settings that need a reboot to be effective.
bool settingsUpdated = false;

CAN_frame currentFrame = {.FD = true, .ext_ID = false, .DLC = 64, .ID = 0x12F, .data = {0}};

bool webserver_auth_is_ready() {
  return webserver_auth && !http_username.empty() && !http_password.empty();
}

void handleFileUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len,
                      bool final) {
  if (!index) {
    importedLogs = "";  // Clear previous logs
    logging.printf("Receiving file: %s\n", filename.c_str());
  }

  // Append received data to the string (RAM storage)
  importedLogs += String((char*)data).substring(0, len);

  if (final) {
    logging.println("Upload Complete!");
    request->send(200, "text/plain", "File uploaded successfully");
  }
}

void canReplayTask(void* param) {
  std::vector<String> messages;
  messages.reserve(1000);  // Pre-allocate memory to reduce fragmentation

  if (!importedLogs.isEmpty()) {
    int lastIndex = 0;

    while (true) {
      int nextIndex = importedLogs.indexOf("\n", lastIndex);
      if (nextIndex == -1) {
        messages.push_back(importedLogs.substring(lastIndex));
        break;
      }
      messages.push_back(importedLogs.substring(lastIndex, nextIndex));
      lastIndex = nextIndex + 1;
    }

    InterfaceList replay_list = esp32hal->interfaces();
    uint8_t replay_index = datalayer.system.info.can_replay_interface;
    const InterfaceDescriptor* replay_interface =
        replay_index < replay_list.count ? &replay_list.data[replay_index] : nullptr;

    do {
      float firstTimestamp = -1.0f;
      float lastTimestamp = 0.0f;
      bool firstMessageSent = false;  // Track first message

      for (size_t i = 0; i < messages.size(); i++) {
        String line = messages[i];
        line.trim();
        if (line.length() == 0)
          continue;

        int timeStart = line.indexOf("(") + 1;
        int timeEnd = line.indexOf(")");
        if (timeStart == 0 || timeEnd == -1)
          continue;

        float currentTimestamp = line.substring(timeStart, timeEnd).toFloat();

        if (firstTimestamp < 0) {
          firstTimestamp = currentTimestamp;
        }

        // Send first message immediately
        if (!firstMessageSent) {
          firstMessageSent = true;
          firstTimestamp = currentTimestamp;  // Adjust reference time
        } else {
          // Delay only if this isn't the first message
          float deltaT = (currentTimestamp - lastTimestamp) * 1000;
          vTaskDelay((int)deltaT / portTICK_PERIOD_MS);
        }

        lastTimestamp = currentTimestamp;

        int interfaceStart = timeEnd + 2;
        int interfaceEnd = line.indexOf(" ", interfaceStart);
        if (interfaceEnd == -1)
          continue;

        int idStart = interfaceEnd + 1;
        int idEnd = line.indexOf(" [", idStart);
        if (idStart == -1 || idEnd == -1)
          continue;

        String messageID = line.substring(idStart, idEnd);
        int dlcStart = idEnd + 2;
        int dlcEnd = line.indexOf("]", dlcStart);
        if (dlcEnd == -1)
          continue;

        String dlc = line.substring(dlcStart, dlcEnd);
        int dataStart = dlcEnd + 2;
        String dataBytes = line.substring(dataStart);

        currentFrame.ID = strtol(messageID.c_str(), NULL, 16);
        currentFrame.DLC = dlc.toInt();

        int byteIndex = 0;
        char* token = strtok((char*)dataBytes.c_str(), " ");
        while (token != NULL && byteIndex < currentFrame.DLC) {
          currentFrame.data.u8[byteIndex++] = strtol(token, NULL, 16);
          token = strtok(NULL, " ");
        }

        currentFrame.FD = replay_interface != nullptr && replay_interface->type == InterfaceType::CanMcp2517fd;
        currentFrame.ext_ID = (currentFrame.ID > 0x7F0);

        transmit_can_frame_to_interface(&currentFrame, replay_interface);
      }
    } while (datalayer.system.info.loop_playback);

    messages.clear();          // Free vector memory
    messages.shrink_to_fit();  // Release excess memory
  }

  isReplayRunning = false;  // Mark replay as stopped
  vTaskDelete(NULL);
}

static String canreplay_state_json() {
  return build_canreplay_json(isReplayRunning, !importedLogs.isEmpty());
}

void def_route_with_auth(const char* uri, AsyncWebServer& serv, WebRequestMethodComposite method,
                         std::function<void(AsyncWebServerRequest*)> handler) {
  serv.on(uri, method, [handler](AsyncWebServerRequest* request) {
    if (webserver_auth_is_ready() && !request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication(AsyncAuthType::AUTH_BASIC, WEB_AUTH_REALM);
    }
    handler(request);
  });
}

// The server middleware chain runs only once the body is fully parsed, which is
// after the body callback has already seen the payload, so these handlers repeat
// the auth check themselves rather than rely on web_auth_middleware.
static void def_json_post_with_auth(const char* uri,
                                    std::function<void(AsyncWebServerRequest*, JsonDocument&)> handler) {
  server.on(
      uri, HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr,
      [handler](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        // AsyncTCP caps a chunk at MAX_PAYLOAD_SIZE, so the settings
        // save arrives across several callbacks; reassemble into a per-request
        // heap buffer before parsing. _tempObject is freed by the request
        // destructor, so a mid-transfer abort cannot leak the buffer.
        if (index == 0) {
          if (webserver_auth_is_ready() && !request->authenticate(http_username.c_str(), http_password.c_str())) {
            return request->requestAuthentication(AsyncAuthType::AUTH_BASIC, WEB_AUTH_REALM);
          }
          if (total == 0 || total > MAX_JSON_POST_BODY_BYTES) {
            return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad JSON");
          }
          request->_tempObject = malloc(total);
          if (request->_tempObject == nullptr) {
            return request->send(HTTP_STATUS_INTERNAL_SERVER_ERROR, "text/plain", "Out of memory");
          }
        }
        if (request->_tempObject == nullptr) {
          return;  // First chunk was rejected; ignore trailing chunks of the same body.
        }
        if (index + len > total) {
          free(request->_tempObject);
          request->_tempObject = nullptr;
          return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad JSON");
        }
        memcpy((uint8_t*)request->_tempObject + index, data, len);
        if (index + len != total) {
          return;
        }
        JsonDocument doc;
        if (deserializeJson(doc, (uint8_t*)request->_tempObject, total) != DeserializationError::Ok) {
          free(request->_tempObject);
          request->_tempObject = nullptr;
          return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad JSON");
        }
        handler(request, doc);
        free(request->_tempObject);
        request->_tempObject = nullptr;
      });
}

// A miss here means the build embedded a different asset set than the routes
// expect, so answer rather than leaving the request to time out.
static void serve_web_asset_or_fail(AsyncWebServerRequest* request, const char* path) {
  if (!serve_web_asset(request, path)) {
    request->send(HTTP_STATUS_INTERNAL_SERVER_ERROR, "text/plain", "Missing asset");
  }
}

// Edit-card acks read live device state (state_json carries none of these); one
// builder per card keeps the GET /api/editcards snapshot and the POST acks in lockstep.
static void fill_chargelimits_ack(JsonObject ack) {
  auto& settings = datalayer.battery.settings;
  // The user's configured capacity, NOT a pack's live info value — drivers
  // overwrite the live values with BMS-reported capacity; this field keeps
  // the user's intent.
  ack["battery_wh_max"] = settings.user_set_total_capacity_Wh;
  ack["use_scaled_soc"] = settings.soc_scaling_active;
  ack["soc_max"] = settings.max_percentage / PPTT_PER_PERCENT;
  ack["soc_min"] = settings.min_percentage / PPTT_PER_PERCENT;
  ack["charge_a"] = settings.max_user_set_charge_dA / DECI_PER_UNIT;
  ack["discharge_a"] = settings.max_user_set_discharge_dA / DECI_PER_UNIT;
  ack["use_volt_limits"] = settings.user_set_voltage_limits_active;
  ack["target_ch_v"] = settings.max_user_set_charge_voltage_dV / DECI_PER_UNIT;
  ack["target_disch_v"] = settings.max_user_set_discharge_voltage_dV / DECI_PER_UNIT;
  ack["bms_reset_duration"] = settings.user_set_bms_reset_duration_ms / MS_PER_SECOND;
}

static void fill_bydautocal_ack(JsonObject ack) {
  auto& byd = datalayer_extended.bydAtto3;
  auto& byd2 = datalayer_extended.bydAtto3_2;
  ack["enabled"] = byd.auto_calibrate_soc_enabled;
  ack["drift"] = byd.auto_calibrate_soc_drift_percent;
  ack["enabled2"] = byd2.auto_calibrate_soc_enabled;
  ack["drift2"] = byd2.auto_calibrate_soc_drift_percent;
  ack["cal_target_soc"] = byd.calibrationTargetSOC;
  ack["cal_target_ah"] = byd.calibrationTargetAH;
  ack["cal_target_soc2"] = byd2.calibrationTargetSOC;
  ack["cal_target_ah2"] = byd2.calibrationTargetAH;
  ack["keep_iso_off"] = byd.keep_iso_disabled;
}

static void fill_recoverymode_ack(JsonObject ack) {
  ack["recovery_mode"] = datalayer.battery.settings.user_requests_forced_charging_recovery_mode;
}

static void fill_canidcutoff_ack(JsonObject ack) {
  ack["cutoff"] = user_selected_CAN_ID_cutoff_filter;
}

static void fill_charger_ack(JsonObject ack) {
  ack["hv_enabled"] = datalayer.charger.charger_HV_enabled;
  ack["aux12v_enabled"] = datalayer.charger.charger_aux12V_enabled;
  ack["setpoint_v"] = datalayer.charger.charger_setpoint_HV_VDC;
  ack["setpoint_a"] = datalayer.charger.charger_setpoint_HV_IDC;
  ack["end_a"] = datalayer.charger.charger_setpoint_HV_IDC_END;
}

void init_webserver() {
  if (webserver_auth_is_ready()) {
    web_auth_middleware.setUsername(http_username.c_str());
    web_auth_middleware.setPassword(http_password.c_str());
    web_auth_middleware.setRealm(WEB_AUTH_REALM);
    web_auth_middleware.setAuthType(AsyncAuthType::AUTH_BASIC);
    server.addMiddleware(&web_auth_middleware);
  }

  server
      .on("/logout", HTTP_GET,
          [](AsyncWebServerRequest* request) {
            AsyncWebServerResponse* response = request->beginResponse(
                401, "text/plain", "Logout requested. Cancel the browser login prompt to finish logging out.");
            response->addHeader("WWW-Authenticate", String("Basic realm=\"") + WEB_AUTH_REALM + "\"");
            response->addHeader("Cache-Control", "no-store");
            response->addHeader("Connection", "close");
            request->send(response);
          })
      .skipServerMiddlewares();

  // Route for machine-readable board capabilities
  def_route_with_auth("/capabilities", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, capabilities_json());
  });

  // Contract with the bundled OTA page: it reads `hardware` and `firmware` from here.
  def_route_with_auth("/GetFirmwareInfo", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, capabilities_json());
  });

  // Route for live state polled by the SPA
  def_route_with_auth("/api/state", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, state_json());
  });

  // Route for root / web page
  def_route_with_auth("/", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    // Clear OTA active flag as a safeguard in case onOTAEnd() wasn't called
    ota_active = false;
    serve_web_asset_or_fail(request, ASSET_PATH_SHELL);
  });

  // "/" is registered separately above for its OTA-flag clear.
  for_each_web_asset_path([](const char* path) {
    def_route_with_auth(path, server, HTTP_GET,
                        [path](AsyncWebServerRequest* request) { serve_web_asset_or_fail(request, path); });
  });

  // Route for going to advanced battery info web page
  def_route_with_auth("/api/advanced", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, build_advanced_json());
  });

  def_route_with_auth("/api/canreplay", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, canreplay_state_json());
  });

  def_json_post_with_auth("/api/canreplay/interface", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    if (!doc["interface"].is<int>()) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Error: updating interface failed");
    }
    int interfaceValue = doc["interface"].as<int>();
    InterfaceList list = esp32hal->interfaces();
    if (interfaceValue < 0 || (size_t)interfaceValue >= list.count || list.data[interfaceValue].can_bus == nullptr) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Error: updating interface failed");
    }
    datalayer.system.info.can_replay_interface = (uint8_t)interfaceValue;
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, canreplay_state_json());
  });

  def_json_post_with_auth("/api/canreplay/start", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    if (isReplayRunning) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Replay already running!");
    }
    datalayer.system.info.loop_playback = doc["loop"].as<bool>();
    isReplayRunning = true;  // Set before task creation so a rapid second POST is rejected.
    xTaskCreatePinnedToCore(canReplayTask, "CAN_Replay", CAN_REPLAY_TASK_STACK_SIZE, NULL, 1, NULL,
                            esp32hal->CORE_FUNCTION_CORE());
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, canreplay_state_json());
  });

  def_json_post_with_auth("/api/canreplay/stop", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    datalayer.system.info.loop_playback = false;  // Ends looping; the in-progress pass finishes.
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, canreplay_state_json());
  });

  def_json_post_with_auth("/api/pause", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    if (!doc["on"].is<bool>()) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad Request");
    }
    setBatteryPause(doc["on"].as<bool>(), false);
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, state_json());
  });

  // The SPA's equipment stop. Mirrors /equipmentStop: on == contactors open, CAN
  // left running, and the state stored to flash so a reboot stays stopped.
  def_json_post_with_auth("/api/equipmentstop", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    if (!doc["on"].is<bool>()) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad Request");
    }
    bool stop = doc["on"].as<bool>();
    setBatteryPause(stop, false, stop ? EquipmentStop::STOP : EquipmentStop::RESUME);
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, state_json());
  });

  // Local store is safe because build_settings_json is synchronous — it does not
  // outlive the request.
  def_route_with_auth("/api/settings", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    BatteryEmulatorSettingsStore settings(true);
    String json = build_settings_json(settings);
    if (json.isEmpty()) {
      return request->send(HTTP_STATUS_INTERNAL_SERVER_ERROR, "text/plain", "Settings payload overflow");
    }
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, json);
  });

  // Current values for the live Edit-card panel; shapes mirror the POST acks.
  def_route_with_auth("/api/editcards", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    JsonDocument doc;
    fill_chargelimits_ack(doc["chargelimits"].to<JsonObject>());
    fill_bydautocal_ack(doc["bydautocal"].to<JsonObject>());
    fill_recoverymode_ack(doc["recoverymode"].to<JsonObject>());
    fill_canidcutoff_ack(doc["canidcutoff"].to<JsonObject>());
    if (charger) {
      fill_charger_ack(doc["charger"].to<JsonObject>());
    }
    fill_balancing_ack(datalayer.battery.pack[0].settings, doc["balancing"].to<JsonObject>());
    // The SPA renders one card per entry: never emit an entry for a slot the
    // POST path would reject.
    JsonArray balancing_slots = doc["balancing_tesla"].to<JsonArray>();
    for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
      if (!battery_slot_addressable(slot)) {
        continue;
      }
      JsonObject entry = balancing_slots.add<JsonObject>();
      entry["slot"] = slot;
      fill_balancing_ack(datalayer.battery_slot(slot).settings, entry);
    }
    String out;
    serializeJson(doc, out);
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, out);
  });

  def_json_post_with_auth("/api/settings", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    BatteryEmulatorSettingsStore settings;
    SettingsApplyResult result = apply_settings_json(settings, doc.as<JsonObjectConst>());
    if (!result.ok) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", result.error);
    }
    settingsUpdated |= result.changed;
    String json = build_settings_json(settings, result.reboot_required);
    if (json.isEmpty()) {
      return request->send(HTTP_STATUS_INTERNAL_SERVER_ERROR, "text/plain", "Settings payload overflow");
    }
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, json);
  });

#ifdef BOARD_HAS_LOAD_SWITCH
  def_json_post_with_auth("/api/loadswitch", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    LoadSwitch* load_switch = esp32hal->load_switch();
    if (load_switch == nullptr || !doc["channel"].is<int>() || !doc["on"].is<bool>()) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad Request");
    }
    int channel = doc["channel"].as<int>();
    // channel_count, not kLoadSwitchMaxChannels: request_manual() would accept a
    // channel the tick never drains, stranding pending set forever.
    if (channel < 0 || channel >= load_switch->status().channel_count ||
        load_switch->status().channels[channel].role != LoadSwitchRole::Manual) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Invalid channel");
    }
    load_switch->request_manual((uint8_t)channel, doc["on"].as<bool>());
    // Answers with pending set: the tick has not run, so the client is told the
    // request is in flight rather than shown the value it asked for.
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, state_json());
  });
#endif

  // Absent fields preserve; the ack echoes resulting device state (state_json
  // carries none of these) so the client repaints truthfully.
  def_json_post_with_auth("/api/chargelimits", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    auto& settings = datalayer.battery.settings;
    bool changed = false;
    if (!doc["battery_wh_max"].isNull()) {
      // The setting keeps the user's intent; every slot's live capacity is
      // re-seeded from it, matching boot. BMS-capable drivers overwrite
      // their own pack's live value afterwards.
      uint32_t capacity_Wh = doc["battery_wh_max"].as<uint32_t>();
      settings.user_set_total_capacity_Wh = capacity_Wh;
      for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
        datalayer.battery_slot(slot).info.total_capacity_Wh = capacity_Wh;
      }
      changed = true;
    }
    if (!doc["use_scaled_soc"].isNull()) {
      settings.soc_scaling_active = doc["use_scaled_soc"].as<bool>();
      changed = true;
    }
    if (!doc["soc_max"].isNull()) {
      settings.max_percentage = static_cast<uint16_t>(doc["soc_max"].as<float>() * PPTT_PER_PERCENT);
      changed = true;
    }
    if (!doc["soc_min"].isNull()) {
      settings.min_percentage = static_cast<uint16_t>(doc["soc_min"].as<float>() * PPTT_PER_PERCENT);
      changed = true;
    }
    if (!doc["charge_a"].isNull()) {
      settings.max_user_set_charge_dA = static_cast<uint16_t>(doc["charge_a"].as<float>() * DECI_PER_UNIT);
      changed = true;
    }
    if (!doc["discharge_a"].isNull()) {
      settings.max_user_set_discharge_dA = static_cast<uint16_t>(doc["discharge_a"].as<float>() * DECI_PER_UNIT);
      changed = true;
    }
    if (!doc["use_volt_limits"].isNull()) {
      settings.user_set_voltage_limits_active = doc["use_volt_limits"].as<bool>();
      changed = true;
    }
    if (!doc["target_ch_v"].isNull()) {
      settings.max_user_set_charge_voltage_dV = static_cast<uint16_t>(doc["target_ch_v"].as<float>() * DECI_PER_UNIT);
      changed = true;
    }
    if (!doc["target_disch_v"].isNull()) {
      settings.max_user_set_discharge_voltage_dV =
          static_cast<uint16_t>(doc["target_disch_v"].as<float>() * DECI_PER_UNIT);
      changed = true;
    }
    if (!doc["bms_reset_duration"].isNull()) {
      settings.user_set_bms_reset_duration_ms = static_cast<uint16_t>(doc["bms_reset_duration"].as<float>() * MS_PER_SECOND);
      changed = true;
    }
    if (changed) {
      store_settings();
    }
    JsonDocument ack;
    fill_chargelimits_ack(ack.to<JsonObject>());
    String out;
    serializeJson(ack, out);
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, out);
  });

  // BYD auto-cal card: enabled/drift persist per-key; cal-targets are RAM writes,
  // so store_settings() runs only when one is present.
  def_json_post_with_auth("/api/bydautocal", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    auto& byd = datalayer_extended.bydAtto3;
    auto& byd2 = datalayer_extended.bydAtto3_2;
    {
      BatteryEmulatorSettingsStore settings;
      if (!doc["enabled"].isNull()) {
        byd.auto_calibrate_soc_enabled = doc["enabled"].as<bool>();
        settings.saveBool("BYDAUTOCALEN", byd.auto_calibrate_soc_enabled);
      }
      if (!doc["drift"].isNull()) {
        int value = doc["drift"].as<int>();
        if (value >= BYD_AUTOCAL_DRIFT_MIN && value <= BYD_AUTOCAL_DRIFT_MAX) {
          byd.auto_calibrate_soc_drift_percent = (uint8_t)value;
          settings.saveUInt("BYDAUTOCALDRIFT", (uint8_t)value);
        }
      }
      if (!doc["enabled2"].isNull()) {
        byd2.auto_calibrate_soc_enabled = doc["enabled2"].as<bool>();
        settings.saveBool("BYDAUTOCALEN2", byd2.auto_calibrate_soc_enabled);
      }
      if (!doc["drift2"].isNull()) {
        int value = doc["drift2"].as<int>();
        if (value >= BYD_AUTOCAL_DRIFT_MIN && value <= BYD_AUTOCAL_DRIFT_MAX) {
          byd2.auto_calibrate_soc_drift_percent = (uint8_t)value;
          settings.saveUInt("BYDAUTOCALDRFT2", (uint8_t)value);
        }
      }
      if (!doc["keep_iso_off"].isNull()) {
        byd.keep_iso_disabled = doc["keep_iso_off"].as<bool>();
        byd2.keep_iso_disabled = byd.keep_iso_disabled;
        settings.saveBool("BYDKEEPISOOFF", byd.keep_iso_disabled);
      }
    }
    bool cal_changed = false;
    if (!doc["cal_target_soc"].isNull()) {
      byd.calibrationTargetSOC = static_cast<uint16_t>(doc["cal_target_soc"].as<float>());
      cal_changed = true;
    }
    if (!doc["cal_target_ah"].isNull()) {
      byd.calibrationTargetAH = static_cast<uint16_t>(doc["cal_target_ah"].as<float>());
      cal_changed = true;
    }
    if (!doc["cal_target_soc2"].isNull()) {
      byd2.calibrationTargetSOC = static_cast<uint16_t>(doc["cal_target_soc2"].as<float>());
      cal_changed = true;
    }
    if (!doc["cal_target_ah2"].isNull()) {
      byd2.calibrationTargetAH = static_cast<uint16_t>(doc["cal_target_ah2"].as<float>());
      cal_changed = true;
    }
    if (cal_changed) {
      store_settings();
    }
    JsonDocument ack;
    fill_bydautocal_ack(ack.to<JsonObject>());
    String out;
    serializeJson(ack, out);
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, out);
  });

  // Undercharged recovery mode. RAM flag that is not persisted;
  // store_settings() is still called to persist its fixed subset.
  def_json_post_with_auth("/api/recoverymode", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    if (!doc["on"].is<bool>()) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad Request");
    }
    datalayer.battery.settings.user_requests_forced_charging_recovery_mode = doc["on"].as<bool>();
    store_settings();
    JsonDocument ack;
    fill_recoverymode_ack(ack.to<JsonObject>());
    String out;
    serializeJson(ack, out);
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, out);
  });

  // CAN ID cutoff filter. RAM value that is not persisted;
  // store_settings() is still called to persist its fixed subset.
  def_json_post_with_auth("/api/canidcutoff", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    if (!doc["cutoff"].is<int>()) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad Request");
    }
    user_selected_CAN_ID_cutoff_filter = (uint16_t)doc["cutoff"].as<int>();
    store_settings();
    JsonDocument ack;
    fill_canidcutoff_ack(ack.to<JsonObject>());
    String out;
    serializeJson(ack, out);
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, out);
  });

  // RAM values that are not persisted; store_settings() is still called to
  // persist its fixed subset. Bounds follow the slot's own chemistry — packs
  // autodetect independently.
  def_json_post_with_auth("/api/balancing", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    uint8_t slot = 0;
    if (const char* error = validate_battery_slot(doc, slot)) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", error);
    }
    auto& slot_data = datalayer.battery_slot(slot);
    if (const char* error = validate_balancing_update(slot_data.info.chemistry, doc)) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", error);
    }
    apply_balancing_update(slot_data.settings, doc);
    store_settings();
    JsonDocument ack;
    fill_balancing_ack(slot_data.settings, ack.to<JsonObject>());
    String out;
    serializeJson(ack, out);
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, out);
  });

  // Viewing the CAN log implies logging should run; enabling here matches the
  // legacy page's view side effect. Entering fresh (logging was off) clears the
  // buffer, which is shared with the debug log, so stale debug text is not shown
  // as CAN frames. /api/canlog/stop (called on route teardown) turns it back off.
  def_route_with_auth("/api/canlog", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!datalayer.system.info.can_logging_active) {
      datalayer.system.info.logged_can_messages_offset = 0;
      datalayer.system.info.logged_can_messages[0] = '\0';
    }
    datalayer.system.info.can_logging_active = true;
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, build_canlog_json());
  });
  def_route_with_auth("/api/canlog/stop", server, HTTP_POST, [](AsyncWebServerRequest* request) {
    datalayer.system.info.can_logging_active = false;
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, "{\"ok\":true}");
  });
  def_route_with_auth("/api/debug", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, build_debug_json());
  });

  // Factory reset carries no body, so it uses the bodyless auth helper: a
  // Content-Length-0 POST never reaches the JSON body callback.
  def_route_with_auth("/api/factoryreset", server, HTTP_POST, [](AsyncWebServerRequest* request) {
    BatteryEmulatorSettingsStore settings;
    settings.clearAll();
    // Re-stamp the schema so a pre-reboot settings save is not re-migrated.
    settings.saveUInt("IFSCHEMA", INTERFACE_SCHEMA_VERSION);
    erase_phy_cal_data();
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, "{\"ok\":true}");
  });

  // Define the handler to import can log
  server.on(
      "/import_can_log", HTTP_POST,
      [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "Ready to receive file.");  // Response when request is made
      },
      handleFileUpload);

#ifdef SDCARD
  if (datalayer.system.info.CAN_SD_logging_active) {
    // Define the handler to export can log
    server.on("/export_can_log", HTTP_GET, [](AsyncWebServerRequest* request) {
      pause_can_writing();
      request->send(SD, CAN_LOG_FILE, String(), true);
      resume_can_writing();
    });

    // Define the handler to delete can log
    server.on("/delete_can_log", HTTP_GET, [](AsyncWebServerRequest* request) {
      delete_can_log();
      request->send(200, "text/plain", "Log file deleted");
    });
  } else
#endif  // SDCARD
  {
    // Define the handler to export can log
    server.on("/export_can_log", HTTP_GET, [](AsyncWebServerRequest* request) {
      String logs = String(datalayer.system.info.logged_can_messages);
      if (logs.length() == 0) {
        logs = "No logs available.";
      }

      String filename = "canlog_" + format_ms_stamp(millis64()) + ".txt";

      // Use request->send with dynamic headers
      AsyncWebServerResponse* response = request->beginResponse(200, "text/plain", logs);
      response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
      request->send(response);
    });
  }

#ifdef SDCARD
  if (datalayer.system.info.SD_logging_active) {
    // Define the handler to delete log file
    server.on("/delete_log", HTTP_GET, [](AsyncWebServerRequest* request) {
      delete_log();
      request->send(200, "text/plain", "Log file deleted");
    });

    // Define the handler to export debug log
    server.on("/export_log", HTTP_GET, [](AsyncWebServerRequest* request) {
      pause_log_writing();
      request->send(SD, LOG_FILE, String(), true);
      resume_log_writing();
    });
  } else
#endif  // SDCARD
  {
    // Define the handler to export debug log
    server.on("/export_log", HTTP_GET, [](AsyncWebServerRequest* request) {
      String logs = String(datalayer.system.info.logged_can_messages);
      if (logs.length() == 0) {
        logs = "No logs available.";
      }

      String filename = "log_" + format_ms_stamp(millis64()) + ".txt";

      // Use request->send with dynamic headers
      AsyncWebServerResponse* response = request->beginResponse(200, "text/plain", logs);
      response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
      request->send(response);
    });
  }

  def_route_with_auth("/api/cellmonitor", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, build_cellmonitor_json());
  });

  def_route_with_auth("/api/events", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, build_events_json());
  });

  def_route_with_auth("/api/events/clear", server, HTTP_POST, [](AsyncWebServerRequest* request) {
    reset_all_events();
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, build_events_json());
  });

  def_json_post_with_auth("/api/advanced/command", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    if (!doc["id"].is<const char*>()) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad Request");
    }
    uint8_t slot = 0;
    if (const char* error = validate_battery_slot(doc, slot)) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", error);
    }
    // A "value" that is present but unreadable as an int32 must not be mistaken for
    // one that was never sent, or it would slip past the arity check below.
    auto raw_value = doc["value"];
    const bool has_value = !raw_value.isNull();
    if (has_value && !raw_value.is<int32_t>()) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad Request");
    }
    const int32_t value = has_value ? raw_value.as<int32_t>() : 0;
    bool ok = run_advanced_command(doc["id"].as<const char*>(), slot,
                                   has_value ? &value : nullptr);
    if (!ok) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Command rejected");
    }
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, build_advanced_json());
  });

  register_dump_can_route(server);

  // Registered unconditionally: init_webserver() runs in the connectivity task
  // before setup_charger() creates the charger, so presence must gate at
  // request time. RAM values that are not persisted; store_settings() is still
  // called to persist its fixed subset. Absent fields preserve. setpoint_v
  // applies before setpoint_a is validated, so one POST can raise both without
  // tripping the power ceiling against the outgoing voltage.
  def_json_post_with_auth("/api/charger", [](AsyncWebServerRequest* request, JsonDocument& doc) {
    if (!charger) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "No charger configured");
    }
    bool changed = false;
    if (!doc["setpoint_v"].isNull()) {
      float volts = doc["setpoint_v"].as<float>();
      if (volts < CHARGER_MIN_HV || volts > CHARGER_MAX_HV) {
        return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Invalid value");
      }
      datalayer.charger.charger_setpoint_HV_VDC = volts;
      changed = true;
    }
    if (!doc["setpoint_a"].isNull()) {
      float amps = doc["setpoint_a"].as<float>();
      if (amps < 0 || amps > CHARGER_MAX_A ||
          amps * DECI_PER_UNIT > datalayer.battery.settings.max_user_set_charge_dA ||
          amps * datalayer.charger.charger_setpoint_HV_VDC > CHARGER_MAX_POWER) {
        return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Invalid value");
      }
      datalayer.charger.charger_setpoint_HV_IDC = amps;
      changed = true;
    }
    if (!doc["end_a"].isNull()) {
      datalayer.charger.charger_setpoint_HV_IDC_END = doc["end_a"].as<float>();
      changed = true;
    }
    if (!doc["hv_enabled"].isNull()) {
      datalayer.charger.charger_HV_enabled = doc["hv_enabled"].as<bool>();
      changed = true;
    }
    if (!doc["aux12v_enabled"].isNull()) {
      datalayer.charger.charger_aux12V_enabled = doc["aux12v_enabled"].as<bool>();
      changed = true;
    }
    if (changed) {
      store_settings();
    }
    JsonDocument ack;
    fill_charger_ack(ack.to<JsonObject>());
    String out;
    serializeJson(ack, out);
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, out);
  });

  // Route to handle reboot command
  def_route_with_auth("/reboot", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", "Rebooting server...");
    hold_pins_across_reset();
    graceful_restart();
  });

  // Initialize ElegantOTA
  init_ElegantOTA();

  // Start server
  server.begin();
}

void webserver_tick() {
  can_dump_drain_tick();

  if (ota_active && ota_timeout_timer.elapsed()) {
    // OTA timeout, try to restore can and clear the update event
    set_event(EVENT_OTA_UPDATE_TIMEOUT, 0);
    onOTAEnd(false);
  }
}

// Function to initialize ElegantOTA
void init_ElegantOTA() {
  ElegantOTA.begin(&server);  // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);
}

static void add_capabilities_device(JsonObject devices, const char* role, const char* type_name,
                                    const InterfaceDescriptor* interface, InterfaceList list) {
  if (type_name == nullptr || type_name[0] == '\0') {
    return;
  }
  JsonObject device = devices[role].to<JsonObject>();
  device["type"] = type_name;
  if (interface != nullptr) {
    device["interface"] = static_cast<size_t>(interface - list.data);
  }
}

String capabilities_json() {
  JsonDocument doc;
  doc["hardware"] = esp32hal->name();
  doc["firmware"] = String(version_number);
#if defined(GITHUB_ORG) && defined(GITHUB_REPO)
#if defined(GIT_TAG)
  doc["firmware_url"] = "https://github.com/" GITHUB_ORG "/" GITHUB_REPO "/releases/tag/" GIT_TAG;
#elif defined(GITHUB_PR)
  doc["firmware_url"] = "https://github.com/" GITHUB_ORG "/" GITHUB_REPO "/pull/" GITHUB_PR;
#endif
#endif

  BatteryEmulatorSettingsStore settings(true);
  InterfaceList list = esp32hal->interfaces();
  JsonArray interfaces = doc["interfaces"].to<JsonArray>();
  for (size_t i = 0; i < list.count; i++) {
    JsonObject iface = interfaces.add<JsonObject>();
    iface["index"] = i;
    iface["type"] = static_cast<uint8_t>(list.data[i].type);
    iface["name"] = descriptor_name(list.data[i]);
    bool termination_capable = esp32hal->supports_interface_termination(i);
    iface["termination_capable"] = termination_capable;
    if (termination_capable) {
      iface["termination"] = settings.getBool(interface_termination_key(i).c_str(), false);
    }
  }

  JsonObject devices = doc["devices"].to<JsonObject>();
  add_capabilities_device(devices, "battery", name_for_battery_type(battery_type_for_slot(0)), can_config.battery,
                          list);
  if (battery_slot_occupied(1)) {
    add_capabilities_device(devices, "battery2", name_for_battery_type(battery_type_for_slot(1)),
                            can_config.battery_double, list);
  }
  if (battery_slot_occupied(2)) {
    add_capabilities_device(devices, "battery3", name_for_battery_type(battery_type_for_slot(2)),
                            can_config.battery_triple, list);
  }
  add_capabilities_device(devices, "inverter", name_for_inverter_type(user_selected_inverter_protocol),
                          can_config.inverter, list);
  add_capabilities_device(devices, "charger", name_for_charger_type(user_selected_charger_type), can_config.charger,
                          list);
  add_capabilities_device(devices, "shunt", name_for_shunt_type(user_selected_shunt_type), can_config.shunt, list);

  String out;
  serializeJson(doc, out);
  return out;
}

// The dashboard shows a severity signal, not the event list, which has its own
// endpoint; "latest" is absent when nothing is active, so the client shows a dash.
static void add_state_events(JsonObject events) {
  uint16_t active = 0;
  uint64_t newest_timestamp = 0;
  EVENTS_ENUM_TYPE newest = EVENT_NOF_EVENTS;

  for (uint16_t i = 0; i < EVENT_NOF_EVENTS; i++) {
    EVENTS_ENUM_TYPE event = static_cast<EVENTS_ENUM_TYPE>(i);
    const EVENTS_STRUCT_TYPE* entry = get_event_pointer(event);
    if (entry->state != EVENT_STATE_ACTIVE && entry->state != EVENT_STATE_ACTIVE_LATCHED) {
      continue;
    }
    active++;
    if (newest == EVENT_NOF_EVENTS || entry->timestamp >= newest_timestamp) {
      newest_timestamp = entry->timestamp;
      newest = event;
    }
  }

  events["active"] = active;
  if (newest != EVENT_NOF_EVENTS) {
    events["latest"] = get_event_message_string(newest);
  }
}

String state_json() {
  JsonDocument doc;

  JsonObject sys = doc["system"].to<JsonObject>();
  sys["status"] = String(get_emulator_pause_status().c_str());
  sys["uptime"] = format_ms_string(millis64());
  // Numeric seconds for the SPA to detect a reboot (resets to 0) and reload.
  sys["uptime_s"] = static_cast<uint32_t>(millis64() / static_cast<uint64_t>(MS_PER_SECOND));
  sys["free_heap"] = ESP.getFreeHeap();
  sys["paused"] = emulator_pause_request_ON;
  sys["equipment_stop"] = datalayer.system.info.equipment_stop_active;
  sys["auth"] = webserver_auth;
  sys["log_available"] = datalayer.system.info.web_logging_active || datalayer.system.info.SD_logging_active;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ap_active"] = ap_active;
  if (ap_active) {
    wifi["ap_ssid"] = ssidAP.c_str();
    wifi["ap_ip"] = WiFi.softAPIP().toString();
  }
  const bool sta_connected = WiFi.status() == WL_CONNECTED;
  wifi["connected"] = sta_connected;
  wifi["ssid"] = ssid.c_str();
  String mac = WiFi.macAddress();
  mac.toLowerCase();
  wifi["mac"] = mac;
  if (sta_connected) {
    wifi["ip"] = WiFi.localIP().toString();
    wifi["hostname"] = WiFi.getHostname();
    wifi["rssi"] = WiFi.RSSI();
    wifi["channel"] = WiFi.channel();
  }

  const DATALAYER_BATTERY_STATUS_TYPE& battery_status = datalayer.battery.combined.status;
  JsonObject battery = doc["battery"].to<JsonObject>();
  if (datalayer.battery.pack[0].info.battery_name[0] != '\0') {
    battery["name"] = datalayer.battery.pack[0].info.battery_name;
  }
  battery["soc"] = battery_status.reported_soc / PPTT_PER_PERCENT;
  battery["soc_real"] = battery_status.real_soc / PPTT_PER_PERCENT;
  battery["soh"] = battery_status.soh_pptt / PPTT_PER_PERCENT;
  battery["voltage"] = battery_status.voltage_dV / DECI_PER_UNIT;
  battery["current"] = battery_status.reported_current_dA / DECI_PER_UNIT;
  battery["power"] = battery_status.active_power_W;
  battery["cell_min_mV"] = battery_status.cell_min_voltage_mV;
  battery["cell_max_mV"] = battery_status.cell_max_voltage_mV;

  JsonObject inverter = doc["inverter"].to<JsonObject>();
  inverter["name"] = name_for_inverter_type(user_selected_inverter_protocol);

  if (charger) {
    const DATALAYER_CHARGER_TYPE& charger_data = datalayer.charger;
    JsonObject chg = doc["charger"].to<JsonObject>();
    chg["name"] = name_for_charger_type(user_selected_charger_type);
    chg["alive"] = charger_data.CAN_charger_still_alive > 0;
    chg["hv_enabled"] = charger_data.charger_HV_enabled;
    chg["aux12v_enabled"] = charger_data.charger_aux12V_enabled;
    chg["hv_v"] = charger_data.charger_stat_HVvol;
    chg["hv_a"] = charger_data.charger_stat_HVcur;
    chg["ac_v"] = charger_data.charger_stat_ACvol;
    chg["ac_a"] = charger_data.charger_stat_ACcur;
    chg["lv_v"] = charger_data.charger_stat_LVvol;
    chg["lv_a"] = charger_data.charger_stat_LVcur;
  }

  add_state_events(doc["events"].to<JsonObject>());

#ifdef BOARD_HAS_LOAD_SWITCH
  if (LoadSwitch* load_switch = esp32hal->load_switch()) {
    const LoadSwitchStatus& ls_status = load_switch->status();
    JsonObject ls = doc["load_switch"].to<JsonObject>();
    ls["device_ok"] = ls_status.device_ok;
    JsonArray channels = ls["channels"].to<JsonArray>();
    // Every channel is emitted, disabled ones included: the client addresses a
    // channel by its array index when toggling.
    for (uint8_t ch = 0; ch < ls_status.channel_count; ch++) {
      const LoadSwitchChannelStatus& channel = ls_status.channels[ch];
      JsonObject entry = channels.add<JsonObject>();
      entry["role"] = name_for_load_switch_role(channel.role);
      entry["manual"] = channel.role == LoadSwitchRole::Manual;
      entry["on"] = channel.on;
      entry["pending"] = channel.pending;
      entry["pending_on"] = channel.pending_on;
      entry["current_mA"] = channel.current_mA;
      entry["fault"] = channel.fault || channel.latched_off;
    }
  }
#endif

  String out;
  serializeJson(doc, out);
  return out;
}


void onOTAStart() {
  // Pause charge/discharge but keep CAN alive so the inverter link survives the update.
  setBatteryPause(true, false, EquipmentStop::UNCHANGED, false);

  // Log when OTA has started
  set_event(EVENT_OTA_UPDATE, 0);

  // If already set, make a new attempt
  clear_event(EVENT_OTA_UPDATE_TIMEOUT);
  ota_active = true;

  ota_timeout_timer.reset();
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    logging.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
    // Reset the "watchdog"
    ota_timeout_timer.reset();
  }
}

void onOTAEnd(bool success) {

  ota_active = false;
  clear_event(EVENT_OTA_UPDATE);

  // Log when OTA has finished
  if (success) {
    LOG_SET_NEXT_SEVERITY(5);  // notice
    logging.println("OTA update finished successfully!");
    hold_pins_across_reset();
    graceful_restart();
  } else {
    LOG_SET_NEXT_SEVERITY(3);  // err
    logging.println("There was an error during OTA update!");
    setBatteryPause(false, false, EquipmentStop::UNCHANGED, false);
  }
}

