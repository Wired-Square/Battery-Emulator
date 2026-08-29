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
#include "../wifi/wifi.h"
#include "esp_task_wdt.h"
#include "favicon.h"
#include "static_assets.h"
#include "web_ui_selection.h"
#include "webserver_can_streaming.h"

#include <string>

std::string http_username;
std::string http_password;

bool webserver_auth = false;
static constexpr int HTTP_STATUS_OK = 200;
static constexpr int HTTP_STATUS_BAD_REQUEST = 400;
static constexpr int HTTP_STATUS_INTERNAL_SERVER_ERROR = 500;
static constexpr int HTTP_STATUS_SERVICE_UNAVAILABLE = 503;
// The full settings save is ~2-3 KB (~104 fields); do not shrink below that.
static constexpr size_t MAX_JSON_POST_BODY_BYTES = 16384;
static constexpr const char* CONTENT_TYPE_JSON = "application/json";
static constexpr int kWebApiVersion = 1;
// datalayer integer scalings: pptt is percent x 100, deci-units are unit x 10
static constexpr float PPTT_PER_PERCENT = 100.0f;
static constexpr float DECI_PER_UNIT = 10.0f;
static constexpr float MS_PER_SECOND = 1000.0f;
static char web_ui_shell_name[kMaxAssetNameLen + 1] = "";

static void refresh_web_ui_shell(BatteryEmulatorSettingsStore& store) {
  const String stored = store.getString("WEBUI", kDefaultUiShell);
  const size_t len = stored.length() < kMaxAssetNameLen ? stored.length() : kMaxAssetNameLen;
  memcpy(web_ui_shell_name, stored.c_str(), len);
  web_ui_shell_name[len] = '\0';
}

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
#include "json_document_reader.h"
#include "json_response_writer.h"
#include "battery_slot_api.h"

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
  return render_json([](ResponseWriter& out) { write_canreplay(out, isReplayRunning, !importedLogs.isEmpty()); });
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

static uint8_t json_post_body[MAX_JSON_POST_BODY_BYTES];
static AsyncWebServerRequest* json_post_owner = nullptr;

// The server middleware chain runs only once the body is fully parsed, which is
// after the body callback has already seen the payload, so these handlers repeat
// the auth check themselves rather than rely on web_auth_middleware.
static void def_json_post_with_auth(const char* uri,
                                    std::function<void(AsyncWebServerRequest*, const DocumentReader&)> handler,
                                    const char* scalar_map = nullptr) {
  server.on(
      uri, HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr,
      [handler, scalar_map](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0) {
          if (webserver_auth_is_ready() && !request->authenticate(http_username.c_str(), http_password.c_str())) {
            return request->requestAuthentication(AsyncAuthType::AUTH_BASIC, WEB_AUTH_REALM);
          }
          if (total == 0 || total > MAX_JSON_POST_BODY_BYTES) {
            return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad JSON");
          }
          if (json_post_owner != nullptr) {
            return request->send(HTTP_STATUS_SERVICE_UNAVAILABLE, "text/plain", "Busy");
          }
          json_post_owner = request;
          // Releases a body that never completes. Compares first: this fires on
          // every teardown, including after a later POST has taken ownership.
          request->onDisconnect([request]() {
            if (json_post_owner == request) {
              json_post_owner = nullptr;
            }
          });
        }
        if (json_post_owner != request) {
          return;  // First chunk was rejected; ignore trailing chunks of the same body.
        }
        if (index + len > total) {
          json_post_owner = nullptr;
          return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad JSON");
        }
        memcpy(json_post_body + index, data, len);
        if (index + len != total) {
          return;
        }
        JsonDocument doc;
        if (deserializeJson(doc, json_post_body, total) != DeserializationError::Ok) {
          json_post_owner = nullptr;
          return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad JSON");
        }
        JsonDocumentReader reader(doc.as<JsonVariantConst>(), scalar_map);
        handler(request, reader);
        json_post_owner = nullptr;
      });
}

// A miss here means the build embedded a different asset set than the routes
// expect, so answer rather than leaving the request to time out.
static void serve_web_asset_or_fail(AsyncWebServerRequest* request, const char* path) {
  if (!serve_web_asset(request, path)) {
    request->send(HTTP_STATUS_INTERNAL_SERVER_ERROR, "text/plain", "Missing asset");
  }
}

void init_webserver() {
  {
    BatteryEmulatorSettingsStore store(true);
    refresh_web_ui_shell(store);
  }

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
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_capabilities));
  });

  // Contract with the bundled OTA page: it reads `hardware` and `firmware` from here.
  def_route_with_auth("/GetFirmwareInfo", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_capabilities));
  });

  // Route for live state polled by the SPA
  def_route_with_auth("/api/state", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_state));
  });

  // Route for root / web page
  def_route_with_auth("/", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    // Clear OTA active flag as a safeguard in case onOTAEnd() wasn't called
    ota_active = false;
    const AsyncWebParameter* requested = request->getParam("ui");
    const char* asset =
        resolve_named_asset(default_web_asset_table(), kUiShellSpec,
                            requested != nullptr ? requested->value().c_str() : nullptr, web_ui_shell_name,
                            kDefaultUiShell);
    if (asset == nullptr) {
      return request->send(HTTP_STATUS_INTERNAL_SERVER_ERROR, "text/plain", "Missing asset");
    }
    serve_web_asset_or_fail(request, asset);
  });

  // "/" is registered separately above for its OTA-flag clear.
  for_each_web_asset_path([](const char* path) {
    def_route_with_auth(path, server, HTTP_GET,
                        [path](AsyncWebServerRequest* request) { serve_web_asset_or_fail(request, path); });
  });

  // Route for going to advanced battery info web page
  def_route_with_auth("/api/advanced", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_advanced));
  });

  def_route_with_auth("/api/canreplay", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, canreplay_state_json());
  });

  def_json_post_with_auth("/api/canreplay/interface", [](AsyncWebServerRequest* request, const DocumentReader& body) {
    const DocumentValue requested = body.value("interface");
    if (!requested.is_integer_in(INT32_MIN, INT32_MAX)) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Error: updating interface failed");
    }
    int interfaceValue = static_cast<int>(requested.integer);
    InterfaceList list = esp32hal->interfaces();
    if (interfaceValue < 0 || (size_t)interfaceValue >= list.count || list.data[interfaceValue].can_bus == nullptr) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Error: updating interface failed");
    }
    datalayer.system.info.can_replay_interface = (uint8_t)interfaceValue;
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, canreplay_state_json());
  });

  def_json_post_with_auth("/api/canreplay/start", [](AsyncWebServerRequest* request, const DocumentReader& body) {
    if (isReplayRunning) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Replay already running!");
    }
    datalayer.system.info.loop_playback = body.value("loop").as_bool();
    isReplayRunning = true;  // Set before task creation so a rapid second POST is rejected.
    xTaskCreatePinnedToCore(canReplayTask, "CAN_Replay", CAN_REPLAY_TASK_STACK_SIZE, NULL, 1, NULL,
                            esp32hal->CORE_FUNCTION_CORE());
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, canreplay_state_json());
  });

  def_json_post_with_auth("/api/canreplay/stop", [](AsyncWebServerRequest* request, const DocumentReader& body) {
    datalayer.system.info.loop_playback = false;  // Ends looping; the in-progress pass finishes.
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, canreplay_state_json());
  });

  def_json_post_with_auth("/api/pause", [](AsyncWebServerRequest* request, const DocumentReader& body) {
    const DocumentValue on = body.value("on");
    if (!on.is_bool()) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad Request");
    }
    setBatteryPause(on.as_bool(), false);
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_state));
  });

  // The SPA's equipment stop. Mirrors /equipmentStop: on == contactors open, CAN
  // left running, and the state stored to flash so a reboot stays stopped.
  def_json_post_with_auth("/api/equipmentstop", [](AsyncWebServerRequest* request, const DocumentReader& body) {
    const DocumentValue on = body.value("on");
    if (!on.is_bool()) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad Request");
    }
    bool stop = on.as_bool();
    setBatteryPause(stop, false, stop ? EquipmentStop::STOP : EquipmentStop::RESUME);
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_state));
  });

  // Local store is safe because write_settings is synchronous — it does not
  // outlive the request.
  def_route_with_auth("/api/settings", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    BatteryEmulatorSettingsStore settings(true);
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON,
                  render_json([&settings](ResponseWriter& out) { write_settings(out, settings); }));
  });

  def_json_post_with_auth(
      "/api/settings",
      [](AsyncWebServerRequest* request, const DocumentReader& body) {
        BatteryEmulatorSettingsStore settings;
        SettingsApplyResult result = apply_settings(settings, body);
        if (!result.ok) {
          return request->send(HTTP_STATUS_BAD_REQUEST, CONTENT_TYPE_JSON,
                               render_json([&result](ResponseWriter& out) {
                                 out.begin_object();
                                 out.field("error", result.error.c_str());
                                 if (result.error_key != nullptr) {
                                   out.field("error_key", result.error_key);
                                   if (result.error_arg.length() > 0) {
                                     out.field("error_arg", result.error_arg.c_str());
                                   }
                                 }
                                 out.end_object();
                               }));
        }
        settingsUpdated |= result.changed;
        refresh_web_ui_shell(settings);
        request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON,
                      render_json([&settings, &result](ResponseWriter& out) {
                        write_settings(out, settings, result.reboot_required);
                      }));
      },
      "values");

#ifdef BOARD_HAS_LOAD_SWITCH
  def_json_post_with_auth("/api/loadswitch", [](AsyncWebServerRequest* request, const DocumentReader& body) {
    LoadSwitch* load_switch = esp32hal->load_switch();
    const DocumentValue channel_value = body.value("channel");
    const DocumentValue on = body.value("on");
    if (load_switch == nullptr || !channel_value.is_integer_in(INT32_MIN, INT32_MAX) || !on.is_bool()) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad Request");
    }
    int channel = static_cast<int>(channel_value.integer);
    // channel_count, not kLoadSwitchMaxChannels: request_manual() would accept a
    // channel the tick never drains, stranding pending set forever.
    if (channel < 0 || channel >= load_switch->status().channel_count ||
        load_switch->status().channels[channel].role != LoadSwitchRole::Manual) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Invalid channel");
    }
    load_switch->request_manual((uint8_t)channel, on.as_bool());
    // Answers with pending set: the tick has not run, so the client is told the
    // request is in flight rather than shown the value it asked for.
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_state));
  });
#endif

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
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_canlog));
  });
  def_route_with_auth("/api/canlog/stop", server, HTTP_POST, [](AsyncWebServerRequest* request) {
    datalayer.system.info.can_logging_active = false;
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, "{\"ok\":true}");
  });
  def_route_with_auth("/api/debug", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_debug));
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
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_cellmonitor));
  });

  def_route_with_auth("/api/events", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_events));
  });

  def_route_with_auth("/api/events/clear", server, HTTP_POST, [](AsyncWebServerRequest* request) {
    reset_all_events();
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_events));
  });

  def_json_post_with_auth("/api/advanced/command", [](AsyncWebServerRequest* request, const DocumentReader& body) {
    const DocumentValue id = body.value("id");
    if (!id.is_string()) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad Request");
    }
    uint8_t slot = 0;
    if (const char* error = validate_battery_slot(body, slot)) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", error);
    }
    // A "value" that is present but unreadable as an int32 must not be mistaken for
    // one that was never sent, or it would slip past the arity check below.
    const DocumentValue raw_value = body.value("value");
    const bool has_value = !raw_value.missing();
    if (has_value && !raw_value.is_integer_in(INT32_MIN, INT32_MAX)) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad Request");
    }
    const int32_t value = has_value ? static_cast<int32_t>(raw_value.integer) : 0;
    bool ok = run_advanced_command(id.as_text(), slot, has_value ? &value : nullptr);
    if (!ok) {
      return request->send(HTTP_STATUS_BAD_REQUEST, "text/plain", "Command rejected");
    }
    request->send(HTTP_STATUS_OK, CONTENT_TYPE_JSON, render_json(write_advanced));
  });

  register_dump_can_route(server);

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

static void add_capabilities_device(ResponseWriter& out, const char* role, const char* type_name,
                                    const InterfaceDescriptor* interface, InterfaceList list) {
  if (type_name == nullptr || type_name[0] == '\0') {
    return;
  }
  out.begin_object(role);
  out.field("type", type_name);
  if (interface != nullptr) {
    out.field("interface", static_cast<size_t>(interface - list.data));
  }
  out.end_object();
}

void write_capabilities(ResponseWriter& out) {
  out.begin_object();
  out.field("hardware", esp32hal->name());
  out.field("firmware", version_number);
  out.field("api", kWebApiVersion);
#if defined(GITHUB_ORG) && defined(GITHUB_REPO)
#if defined(GIT_TAG)
  out.field("firmware_url", "https://github.com/" GITHUB_ORG "/" GITHUB_REPO "/releases/tag/" GIT_TAG);
#elif defined(GITHUB_PR)
  out.field("firmware_url", "https://github.com/" GITHUB_ORG "/" GITHUB_REPO "/pull/" GITHUB_PR);
#endif
#endif

  BatteryEmulatorSettingsStore settings(true);
  InterfaceList list = esp32hal->interfaces();
  out.begin_array("interfaces");
  for (size_t i = 0; i < list.count; i++) {
    out.begin_object();
    out.field("index", i);
    out.field("type", static_cast<uint8_t>(list.data[i].type));
    out.field("name", descriptor_name(list.data[i]));
    bool termination_capable = esp32hal->supports_interface_termination(i);
    out.field("termination_capable", termination_capable);
    if (termination_capable) {
      out.field("termination", settings.getBool(interface_termination_key(i).c_str(), false));
    }
    out.end_object();
  }
  out.end_array();

  out.begin_array("batteries");
  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    if (!battery_slot_occupied(slot)) {
      continue;
    }
    out.begin_object();
    out.field("slot", slot);
    out.field("type", name_for_battery_type(battery_type_for_slot(slot)));
    const InterfaceDescriptor* interface = can_config.battery[slot];
    if (interface != nullptr) {
      out.field("interface", static_cast<size_t>(interface - list.data));
    }
    out.end_object();
  }
  out.end_array();

  out.begin_object("devices");
  add_capabilities_device(out, "inverter", name_for_inverter_type(user_selected_inverter_protocol),
                          can_config.inverter, list);
  add_capabilities_device(out, "charger", name_for_charger_type(user_selected_charger_type), can_config.charger, list);
  add_capabilities_device(out, "shunt", name_for_shunt_type(user_selected_shunt_type), can_config.shunt, list);
  out.end_object();
  out.end_object();
}

static void add_battery_status(ResponseWriter& out, const DATALAYER_BATTERY_TYPE& pack) {
  const DATALAYER_BATTERY_STATUS_TYPE& status = pack.status;
  out.field("soc", status.reported_soc / PPTT_PER_PERCENT);
  out.field("soc_real", status.real_soc / PPTT_PER_PERCENT);
  out.field("soh", status.soh_pptt / PPTT_PER_PERCENT);
  out.field("voltage", status.voltage_dV / DECI_PER_UNIT);
  out.field("current", status.reported_current_dA / DECI_PER_UNIT);
  out.field("power", status.active_power_W);
  out.field("cell_min_mV", status.cell_min_voltage_mV);
  out.field("cell_max_mV", status.cell_max_voltage_mV);
  out.field("remaining_wh", status.remaining_capacity_Wh);
  out.field("max_charge_w", status.max_charge_power_W);
  out.field("max_discharge_w", status.max_discharge_power_W);
  out.field("max_charge_a", status.max_charge_current_dA / DECI_PER_UNIT);
  out.field("max_discharge_a", status.max_discharge_current_dA / DECI_PER_UNIT);
  out.field("temp_min_c", status.temperature_min_dC / DECI_PER_UNIT);
  out.field("temp_max_c", status.temperature_max_dC / DECI_PER_UNIT);
  out.field("total_wh", pack.info.total_capacity_Wh);
}

// The dashboard shows a severity signal, not the event list, which has its own
// endpoint; "latest" is absent when nothing is active, so the client shows a dash.
static void add_state_events(ResponseWriter& out) {
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

  out.field("active", active);
  if (newest != EVENT_NOF_EVENTS) {
    out.field("latest", get_event_message_string(newest).c_str());
    out.field("latest_type", get_event_enum_string(newest));
  }
}

void write_state(ResponseWriter& out) {
  out.begin_object();

  out.begin_object("system");
  out.field("status", get_emulator_pause_status().c_str());
  out.field("status_id", static_cast<uint32_t>(emulator_pause_status));
  out.field("emulator_status", get_emulator_status_string(get_emulator_status()));
  out.field("uptime", format_ms_string(millis64()).c_str());
  // Numeric seconds for the SPA to detect a reboot (resets to 0) and reload.
  out.field("uptime_s", static_cast<uint32_t>(millis64() / static_cast<uint64_t>(MS_PER_SECOND)));
  out.field("free_heap", ESP.getFreeHeap());
  if (datalayer.system.info.CPU_measurement_enabled) {
    out.field("cpu_temp", datalayer.system.info.CPU_temperature);
  }
  out.field("paused", emulator_pause_request_ON);
  out.field("equipment_stop", datalayer.system.info.equipment_stop_active);
  out.field("auth", webserver_auth);
  out.field("log_available", datalayer.system.info.web_logging_active || datalayer.system.info.SD_logging_active);
  out.end_object();

  out.begin_object("wifi");
  out.field("ap_active", ap_active);
  if (ap_active) {
    out.field("ap_ssid", ssidAP.c_str());
    out.field("ap_ip", WiFi.softAPIP().toString().c_str());
  }
  const bool sta_connected = WiFi.status() == WL_CONNECTED;
  out.field("connected", sta_connected);
  out.field("ssid", ssid.c_str());
  String mac = WiFi.macAddress();
  mac.toLowerCase();
  out.field("mac", mac.c_str());
  if (sta_connected) {
    out.field("ip", WiFi.localIP().toString().c_str());
    out.field("hostname", WiFi.getHostname());
    out.field("rssi", WiFi.RSSI());
    out.field("channel", WiFi.channel());
  }
  out.end_object();

  out.begin_object("battery");
  add_battery_status(out, datalayer.battery.combined);
  out.end_object();

  out.begin_array("batteries");
  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    if (!battery_slot_occupied(slot)) {
      continue;
    }
    const DATALAYER_BATTERY_TYPE& pack = datalayer.battery_slot(slot);
    out.begin_object();
    out.field("slot", slot);
    if (pack.info.battery_name[0] != '\0') {
      out.field("name", pack.info.battery_name);
    }
    add_battery_status(out, pack);
    out.end_object();
  }
  out.end_array();

  out.begin_object("inverter");
  out.field("name", name_for_inverter_type(user_selected_inverter_protocol));
  out.end_object();

  if (charger) {
    const DATALAYER_CHARGER_TYPE& charger_data = datalayer.charger;
    out.begin_object("charger");
    out.field("name", name_for_charger_type(user_selected_charger_type));
    out.field("alive", charger_data.CAN_charger_still_alive > 0);
    out.field("hv_enabled", charger_data.charger_HV_enabled);
    out.field("aux12v_enabled", charger_data.charger_aux12V_enabled);
    out.field("hv_v", charger_data.charger_stat_HVvol);
    out.field("hv_a", charger_data.charger_stat_HVcur);
    out.field("ac_v", charger_data.charger_stat_ACvol);
    out.field("ac_a", charger_data.charger_stat_ACcur);
    out.field("lv_v", charger_data.charger_stat_LVvol);
    out.field("lv_a", charger_data.charger_stat_LVcur);
    out.end_object();
  }

  out.begin_object("events");
  add_state_events(out);
  out.end_object();

#ifdef BOARD_HAS_LOAD_SWITCH
  if (LoadSwitch* load_switch = esp32hal->load_switch()) {
    const LoadSwitchStatus& ls_status = load_switch->status();
    out.begin_object("load_switch");
    out.field("device_ok", ls_status.device_ok);
    out.begin_array("channels");
    // Every channel is emitted, disabled ones included: the client addresses a
    // channel by its array index when toggling.
    for (uint8_t ch = 0; ch < ls_status.channel_count; ch++) {
      const LoadSwitchChannelStatus& channel = ls_status.channels[ch];
      out.begin_object();
      out.field("role_id", static_cast<uint32_t>(channel.role));
      out.field("role", name_for_load_switch_role(channel.role));
      out.field("manual", channel.role == LoadSwitchRole::Manual);
      out.field("on", channel.on);
      out.field("pending", channel.pending);
      out.field("pending_on", channel.pending_on);
      out.field("current_mA", channel.current_mA);
      out.field("fault", channel.fault || channel.latched_off);
      out.end_object();
    }
    out.end_array();
    out.end_object();
  }
#endif

  out.end_object();
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

