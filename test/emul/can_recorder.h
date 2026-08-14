#pragma once

#include <vector>

#include "../../Software/src/devboard/utils/types.h"

// Host-test capture of frames handed to transmit_can_frame_to_interface.
// Append-only; inert unless a test reads it.
std::vector<CAN_frame>& recorded_frames();
void reset_recorded_frames();
