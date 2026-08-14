#include "SMA-SBS-BYD-CAN.h"
#include "../communication/can/comm_can.h"
#include "../datalayer/datalayer.h"
#include "../devboard/utils/events.h"

/* TODO: Map error bits in 0x158 */

/* Do not change code below unless you are sure what you are doing */

void SmaSBSBydHvsInverter::update_values() {
  fill_measurement_frames(SMA_358, SMA_3D8, SMA_4D8, SMA_518, SMA_458, datalayer.battery.combined.status.reported_current_dA);

  //Error bits (0x158 bit meanings documented in SMA-BYD-H-CAN.cpp)
  if (datalayer.system.status.battery_allows_contactor_closing) {
    SMA_158.data.u8[2] = 0xAA;
  } else {
    SMA_158.data.u8[2] = 0x6A;
  }

  control_contactor_led();
  check_enable_line();
}

void SmaSBSBydHvsInverter::map_can_frame_to_variable(CAN_frame rx_frame) {
  switch (rx_frame.ID) {
    case 0x360:  //Message originating from SMA inverter - Voltage and current
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      inverter_voltage = (rx_frame.data.u8[0] << 8) | rx_frame.data.u8[1];
      inverter_current = (rx_frame.data.u8[2] << 8) | rx_frame.data.u8[3];
      break;
    case 0x3E0:  //Message originating from SMA inverter - ?
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x420:  //Message originating from SMA inverter - Timestamp
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      inverter_time =
          (rx_frame.data.u8[0] << 24) | (rx_frame.data.u8[1] << 16) | (rx_frame.data.u8[2] << 8) | rx_frame.data.u8[3];
      break;
    case 0x560:  //Message originating from SMA inverter - Init
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x561:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x562:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x563:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x564:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x565:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x566:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x567:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x5E0:  //Message originating from SMA inverter - String
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      //Inverter brand (frame1-3 = 0x53 0x4D 0x41) = SMA
      break;
    case 0x5E1:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x5E2:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x5E3:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x5E4:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x5E5:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x5E6:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    case 0x5E7:  //Message originating from SMA inverter - Pairing request
    case 0x660:  //Message originating from SMA inverter - Pairing request
      pairing_events++;
      set_event(EVENT_SMA_PAIRING, pairing_events);  // also printing a log entry
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      transmit_can_init = true;
      break;
    case 0x62C:
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      break;
    default:
      break;
  }
}

void SmaSBSBydHvsInverter::transmit_can(unsigned long currentMillis) {

  if (transmit_can_init) {

    transmit_can_frame(&SMA_558);
    transmit_can_frame(&SMA_598);
    transmit_can_frame(&SMA_5D8);
    transmit_can_frame(&SMA_618_0);
    transmit_can_frame(&SMA_618_1);
    transmit_can_frame(&SMA_618_2);
    transmit_can_frame(&SMA_618_3);
    transmit_can_frame(&SMA_158);
    transmit_can_frame(&SMA_358);
    transmit_can_frame(&SMA_3D8);
    transmit_can_frame(&SMA_458);
    transmit_can_frame(&SMA_518);
    transmit_can_frame(&SMA_4D8);
    transmit_can_init = false;
  }

  // Send CAN Message every 100ms if contactors are closed
  if (datalayer.system.status.contactors_engaged == 1) {
    if (currentMillis - previousMillis100ms >= INTERVAL_100_MS) {
      previousMillis100ms = currentMillis;

      transmit_can_frame(&SMA_558);
      transmit_can_frame(&SMA_598);
      transmit_can_frame(&SMA_5D8);
      transmit_can_frame(&SMA_618_0);
      transmit_can_frame(&SMA_618_1);
      transmit_can_frame(&SMA_618_2);
      transmit_can_frame(&SMA_618_3);
      transmit_can_frame(&SMA_158);
      transmit_can_frame(&SMA_358);
      transmit_can_frame(&SMA_3D8);
      transmit_can_frame(&SMA_458);
      transmit_can_frame(&SMA_518);
      transmit_can_frame(&SMA_4D8);
    }
  }
}
