#include "canreplay_api.h"

#include "../../datalayer/datalayer.h"
#include "../hal/hal.h"

void write_canreplay(ResponseWriter& out, bool running, bool has_log) {
  out.begin_object();
  out.begin_array("interfaces");
  InterfaceList list = esp32hal->interfaces();
  for (size_t i = 0; i < list.count; i++) {
    // Playback transmits CAN frames, so only CAN-capable interfaces are offered.
    if (list.data[i].can_bus == nullptr) {
      continue;
    }
    out.begin_object();
    out.field("index", i);
    out.field("name", descriptor_name(list.data[i]));
    out.end_object();
  }
  out.end_array();
  out.field("selected", datalayer.system.info.can_replay_interface);
  out.field("running", running);
  out.field("loop", datalayer.system.info.loop_playback);
  out.field("has_log", has_log);
  out.end_object();
}
