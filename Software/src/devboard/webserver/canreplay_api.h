#ifndef CANREPLAY_API_H
#define CANREPLAY_API_H

#include <WString.h>

// GET /api/canreplay payload: CAN-capable playback interfaces, the selected
// one, and run state. running/has_log are webserver-owned globals, passed in
// so this builder stays free of that translation unit.
String build_canreplay_json(bool running, bool has_log);

#endif
