#include "events_api.h"

#include <algorithm>
#include <vector>

#include "../utils/events.h"
#include "../utils/millis64.h"

void write_events(ResponseWriter& out) {
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
  out.begin_object();
  out.begin_array("events");
  for (const auto& entry : active) {
    const EVENTS_STRUCT_TYPE* event = entry.event_pointer;
    out.begin_object();
    out.field("type", get_event_enum_string(entry.event_handle));
    out.field("level", get_event_level_string(entry.event_handle));
    out.field("millis_ago", now - event->timestamp);
    out.field("count", event->occurences);
    out.field("data", event->data);
    out.field("message", get_event_message_string(entry.event_handle).c_str());
    out.end_object();
  }
  out.end_array();
  out.end_object();
}
