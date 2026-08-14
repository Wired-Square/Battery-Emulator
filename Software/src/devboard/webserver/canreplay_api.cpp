#include "canreplay_api.h"
#include "web_json.h"

#include <string>

#include "../../datalayer/datalayer.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "../hal/hal.h"


String build_canreplay_json(bool running, bool has_log) {
  JsonDocument doc;
  JsonArray interfaces = doc["interfaces"].to<JsonArray>();
  InterfaceList list = esp32hal->interfaces();
  for (size_t i = 0; i < list.count; i++) {
    // Playback transmits CAN frames, so only CAN-capable interfaces are offered.
    if (list.data[i].can_bus == nullptr) {
      continue;
    }
    JsonObject obj = interfaces.add<JsonObject>();
    obj["index"] = static_cast<int>(i);
    obj["name"] = descriptor_name(list.data[i]);
  }
  doc["selected"] = datalayer.system.info.can_replay_interface;
  doc["running"] = running;
  doc["loop"] = datalayer.system.info.loop_playback;
  doc["has_log"] = has_log;
  return serialise_doc(doc);
}
