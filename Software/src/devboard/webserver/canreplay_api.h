#ifndef CANREPLAY_API_H
#define CANREPLAY_API_H

#include "response_writer.h"

// GET /api/canreplay payload: CAN-capable playback interfaces, the selected
// one, and run state. running/has_log are webserver-owned globals, passed in
// so this builder stays free of that translation unit.
void write_canreplay(ResponseWriter& out, bool running, bool has_log);

#endif
