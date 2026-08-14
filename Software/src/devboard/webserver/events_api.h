#ifndef EVENTS_API_H
#define EVENTS_API_H

#include <WString.h>

// GET /api/events payload: active events (occurrences > 0), newest first.
String build_events_json();

#endif
