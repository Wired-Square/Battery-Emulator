/*  Portions of this file are an adaptation of the SimpleISA library, originally authored by Jack Rickard.
 *
 *  At present, this code supports the Scale IVT Modular current/voltage sensor device.  
 *  These devices measure current, up to three voltages, and provide temperature compensation.
 *  Additional sensors are planned to provide flexibility/lower BOM costs.
 *
 *  Original license/copyright header of SimpleISA is shown below:
 *   This library was written by Jack Rickard of EVtv - http://www.evtv.me
 *   copyright 2014
 *   You are licensed to use this library for any purpose, commercial or private, 
 *   without restriction.
 *
 *  2024 - Modified to make use of ESP32-Arduino-CAN by miwagner
 *
 *  2024.11 - Modified byte sequence to Big Endian (this is the default for IVT) and the same as CHAdeMO
 *          - Fixed and Added send functions
 *          - Added some GET functions
 *            by NJbubo
 *
 */
#include "CHADEMO-SHUNTS.h"
#include "../datalayer/datalayer.h"
#include "../devboard/utils/events.h"
#include "CHADEMO-BATTERY.h"

/* Initial frames received from ISA shunts provide invalid during initialization */
static int framecount = 0;

/* original variables/names/types from SimpleISA. These warrant refinement */
static float Amperes;  // Floating point with current in Amperes
static float AH;       //Floating point with accumulated ampere-hours
static float KW;
static float KWH;

static float Voltage;
static float Voltage1;
static float Voltage2;
static float Voltage3;
static float VoltageHI;
static float Voltage1HI;
static float Voltage2HI;
static float Voltage3HI;
static float VoltageLO;
static float Voltage1LO;
static float Voltage2LO;
static float Voltage3LO;

static float Temperature;

static float milliamps;
static long watt;
static long As;
static long lastAs;
static long wh;
static long lastWh;

float get_measured_voltage() {
  return Voltage;
}

float get_measured_current() {
  return Amperes;
}

//This is our CAN interrupt service routine to catch inbound frames
void ISA_handleFrame(CAN_frame* frame) {

  if (frame->ID < 0x510 || frame->ID > 0x528) {
    return;
  }

  framecount++;

  switch (frame->ID) {

    case 0x510:
    case 0x511:
      break;

    case 0x521:
      ISA_handle521(frame);
      break;

    case 0x522:
      ISA_handle522(frame);
      break;

    case 0x523:
      ISA_handle523(frame);
      break;

    case 0x524:
      ISA_handle524(frame);
      break;

    case 0x525:
      ISA_handle525(frame);
      break;

    case 0x526:
      ISA_handle526(frame);
      break;

    case 0x527:
      ISA_handle527(frame);
      break;

    case 0x528:
      ISA_handle528(frame);
      break;
  }
  return;
}

//handle frame for Amperes
inline void ISA_handle521(CAN_frame* frame) {
  long current = 0;
  current =
      (long)((frame->data.u8[2] << 24) | (frame->data.u8[3] << 16) | (frame->data.u8[4] << 8) | (frame->data.u8[5]));

  milliamps = current;
  Amperes = current / 1000.0f;
}

//handle frame for Voltage
inline void ISA_handle522(CAN_frame* frame) {
  long volt =
      (long)((frame->data.u8[2] << 24) | (frame->data.u8[3] << 16) | (frame->data.u8[4] << 8) | (frame->data.u8[5]));

  Voltage = volt / 1000.0f;
  Voltage1 = Voltage - (Voltage2 + Voltage3);

  if (framecount < 150) {
    VoltageLO = Voltage;
    Voltage1LO = Voltage1;
  } else {
    if (Voltage < VoltageLO)
      VoltageLO = Voltage;
    if (Voltage > VoltageHI)
      VoltageHI = Voltage;
    if (Voltage1 < Voltage1LO)
      Voltage1LO = Voltage1;
    if (Voltage1 > Voltage1HI)
      Voltage1HI = Voltage1;
  }
}

//handle frame for Voltage 2
inline void ISA_handle523(CAN_frame* frame) {
  long volt =
      (long)((frame->data.u8[2] << 24) | (frame->data.u8[3] << 16) | (frame->data.u8[4] << 8) | (frame->data.u8[5]));

  Voltage2 = volt / 1000.0f;
  if (Voltage2 > 3)
    Voltage2 -= Voltage3;

  if (framecount < 150) {
    Voltage2LO = Voltage2;
  } else {
    if (Voltage2 < Voltage2LO)
      Voltage2LO = Voltage2;
    if (Voltage2 > Voltage2HI)
      Voltage2HI = Voltage2;
  }
}

//handle frame for Voltage3
inline void ISA_handle524(CAN_frame* frame) {
  long volt =
      (long)((frame->data.u8[2] << 24) | (frame->data.u8[3] << 16) | (frame->data.u8[4] << 8) | (frame->data.u8[5]));

  Voltage3 = volt / 1000.0f;

  if (framecount < 150) {
    Voltage3LO = Voltage3;
  } else {
    if (Voltage3 < Voltage3LO && Voltage3 > 10)
      Voltage3LO = Voltage3;
    if (Voltage3 > Voltage3HI)
      Voltage3HI = Voltage3;
  }
}

//handle frame for Temperature
inline void ISA_handle525(CAN_frame* frame) {
  long temp = 0;
  temp = (long)((frame->data.u8[2] << 24) | (frame->data.u8[3] << 16) | (frame->data.u8[4] << 8) | (frame->data.u8[5]));

  Temperature = temp / 10;
}

//handle frame for Kilowatts
inline void ISA_handle526(CAN_frame* frame) {
  watt = 0;
  watt = (long)((frame->data.u8[2] << 24) | (frame->data.u8[3] << 16) | (frame->data.u8[4] << 8) | (frame->data.u8[5]));

  KW = watt / 1000.0f;
}

//handle frame for Ampere-Hours
inline void ISA_handle527(CAN_frame* frame) {
  As = 0;
  As = (long)(frame->data.u8[2] << 24) | (frame->data.u8[3] << 16) | (frame->data.u8[4] << 8) | (frame->data.u8[5]);

  AH += (As - lastAs) / 3600.0f;
  lastAs = As;
}

//handle frame for kiloWatt-hours
inline void ISA_handle528(CAN_frame* frame) {
  wh = (long)((frame->data.u8[2] << 24) | (frame->data.u8[3] << 16) | (frame->data.u8[4] << 8) | (frame->data.u8[5]));
  KWH += (wh - lastWh) / 1000.0f;
  lastWh = wh;
}
