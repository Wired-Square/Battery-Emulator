#ifndef LOGGING_API_H
#define LOGGING_API_H

#include <WString.h>

// {"active","sd","cutoff","lines":[...]} — the in-memory CAN log buffer.
String build_canlog_json();

// {"web_active","sd_active","lines":[...]} — the debug ring buffer, wrap-ordered.
String build_debug_json();

#endif
