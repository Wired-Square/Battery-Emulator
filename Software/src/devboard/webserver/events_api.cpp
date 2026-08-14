#include "events_api.h"
#include "web_json.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "../utils/events.h"
#include "../utils/millis64.h"


String build_events_json() {
  std::vector<EventData> active;
  for (int i = 0; i < EVENT_NOF_EVENTS; i++) {
    const auto handle = static_cast<EVENTS_ENUM_TYPE>(i);
    const EVENTS_STRUCT_TYPE* event = get_event_pointer(handle);
    if (event->occurences > 0) {
      active.push_back({handle, event});
    }
  }
  std::sort(active.begin(), active.end(), compareEventsByTimestampDesc);

  const uint64_t now = millis64();
  JsonDocument doc;
  JsonArray events = doc["events"].to<JsonArray>();
  for (const auto& entry : active) {
    const EVENTS_STRUCT_TYPE* event = entry.event_pointer;
    JsonObject obj = events.add<JsonObject>();
    // enum/level strings are static literals; ArduinoJson may link them
    // without copying. The message is a temporary String, so copy it.
    obj["type"] = get_event_enum_string(entry.event_handle);
    obj["level"] = get_event_level_string(entry.event_handle);
    obj["millis_ago"] = now - event->timestamp;
    obj["count"] = event->occurences;
    obj["data"] = event->data;
    obj["message"] = std::string(get_event_message_string(entry.event_handle).c_str());
  }
  return serialise_doc(doc);
}
