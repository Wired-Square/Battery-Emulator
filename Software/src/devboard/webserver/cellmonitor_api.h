#ifndef CELLMONITOR_API_H
#define CELLMONITOR_API_H

#include "response_writer.h"

// GET /api/cellmonitor payload: one entry per present battery, each with its
// read cell voltages (mV), per-cell balancing flags, overall balancing, and any
// extra per-cell series the driver publishes.
void write_cellmonitor(ResponseWriter& out);

#endif
