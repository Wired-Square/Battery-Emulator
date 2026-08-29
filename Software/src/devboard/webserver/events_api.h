#ifndef EVENTS_API_H
#define EVENTS_API_H

#include "response_writer.h"

// GET /api/events payload: active events (occurrences > 0), newest first.
void write_events(ResponseWriter& out);

#endif
