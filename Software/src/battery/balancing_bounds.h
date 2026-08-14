#ifndef BALANCING_BOUNDS_H
#define BALANCING_BOUNDS_H

#include <cstdint>

// Bounds on the balancing settings a web client may write. The ceilings are
// the Tesla 3/Y chemistry safety constants, static_asserted against the
// driver's values in TESLA-BATTERY.cpp; the driver also clamps at apply time,
// so these are the single source for both layers.
constexpr uint16_t BALANCING_CELL_MAX_NCM_MV = 4250;
constexpr uint16_t BALANCING_CELL_MAX_LFP_MV = 3650;
constexpr uint16_t BALANCING_CELL_MIN_MV = 3400;
constexpr uint16_t BALANCING_DEVIATION_MAX_NCM_MV = 500;
constexpr uint16_t BALANCING_DEVIATION_MAX_LFP_MV = 400;
constexpr uint16_t BALANCING_DEVIATION_MIN_MV = 300;
constexpr uint16_t BALANCING_PACK_MAX_NCM_DV = 4030;
constexpr uint16_t BALANCING_PACK_MAX_LFP_DV = 3880;
constexpr uint16_t BALANCING_PACK_MIN_DV = 3800;
// Balancing deliberately charges past the chemistry pack ceiling.
constexpr uint16_t BALANCING_PACK_HEADROOM_DV = 60;
constexpr uint16_t BALANCING_FLOAT_POWER_MIN_W = 100;
constexpr uint16_t BALANCING_FLOAT_POWER_MAX_W = 2000;

#endif
