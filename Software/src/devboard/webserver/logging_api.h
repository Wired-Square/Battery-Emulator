#ifndef LOGGING_API_H
#define LOGGING_API_H

#include "response_writer.h"

// {"active","sd","cutoff","lines":[...]} — the in-memory CAN log buffer.
void write_canlog(ResponseWriter& out);

// {"web_active","sd_active","lines":[...]} — the debug ring buffer, wrap-ordered.
void write_debug(ResponseWriter& out);

#endif
