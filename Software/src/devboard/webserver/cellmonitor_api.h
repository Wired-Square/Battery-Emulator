#ifndef CELLMONITOR_API_H
#define CELLMONITOR_API_H

#include <WString.h>

// GET /api/cellmonitor payload: one entry per present battery, each with its
// read cell voltages (mV), per-cell balancing flags, and overall balancing.
String build_cellmonitor_json();

#endif
