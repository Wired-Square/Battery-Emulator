#ifndef CHADEMO_SHUNTS_H
#define CHADEMO_SHUNTS_H

#include <stdint.h>
#include "../devboard/utils/types.h"

float get_measured_voltage();
float get_measured_current();

void ISA_handleFrame(CAN_frame* frame);
inline void ISA_handle521(CAN_frame* frame);
inline void ISA_handle522(CAN_frame* frame);
inline void ISA_handle523(CAN_frame* frame);
inline void ISA_handle524(CAN_frame* frame);
inline void ISA_handle525(CAN_frame* frame);
inline void ISA_handle526(CAN_frame* frame);
inline void ISA_handle527(CAN_frame* frame);
inline void ISA_handle528(CAN_frame* frame);
void transmit_can_frame(CAN_frame* tx_frame, int interface);

#endif
