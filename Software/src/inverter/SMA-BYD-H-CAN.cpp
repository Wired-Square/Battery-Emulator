#include "SMA-BYD-H-CAN.h"
#include "../communication/can/comm_can.h"
#include "../datalayer/datalayer.h"
#include "../devboard/utils/events.h"

/* TODO: Map error bits in 0x158 */

/* Do not change code below unless you are sure what you are doing */

void SmaBydHInverter::update_values() {
  fill_measurement_frames(SMA_358, SMA_3D8, SMA_4D8, SMA_518, SMA_458, datalayer.battery.combined.status.reported_current_dA);

  //Error bits
  if (datalayer.system.status.battery_allows_contactor_closing) {
    SMA_158.data.u8[2] = 0xAA;
  } else {
    SMA_158.data.u8[2] = 0x6A;
  }

  control_contactor_led();
  check_enable_line();

  /*
  //SMA_158.data.u8[0] = //bit12 Fault high temperature, bit34Battery cellundervoltage, bit56 Battery cell overvoltage, bit78 batterysystemdefect
  //TODO: add all error bits. Sending message with all 0xAA until that.

  0x158 can be used to send error messages or warnings.

  Each message is defined of two bits:  
  01=message triggered  
  10=no message triggered  
  0xA9=10101001, triggers first message  
  0xA6=10100110, triggers second message  
  0x9A=10011010, triggers third message  
  0x6A=01101010, triggers forth message  
  bX defines the byte

  b0 A9   Battery system defect
  b0 A6   Battery cell overvoltage fault
  b0 9A   Battery cell undervoltage fault
  b0 6A   Battery high temperature fault
  b1 A9   Battery low temperature fault
  b1 A6   Battery high temperature fault
  b1 9A   Battery low temperature fault
  b1 6A   Overload (reboot required)
  b2 A9   Overload (reboot required)
  b2 A6   Incorrect switch position for the battery disconnection point
  b2 9A   Battery system short circuit
  b2 6A   Internal battery hardware fault
  b3 A9   Battery imbalancing fault
  b3 A6   Battery service life expiry
  b3 9A   Battery system thermal management defective
  b3 6A   Internal battery hardware fault
  b4 A9   Battery system defect (warning)
  b4 A6   Battery cell overvoltage fault (warning)
  b4 9A   Battery cell undervoltage fault (warning)
  b4 6A   Battery high temperature fault (warning)
  b5 A9   Battery low temperature fault (warning)
  b5 A6   Battery high temperature fault (warning)
  b5 9A   Battery low temperature fault (warning)
  b5 6A   Self-diagnosis (warning)
  b6 A9   Self-diagnosis (warning)
  b6 A6   Incorrect switch position for the battery disconnection point (warning)
  b6 9A   Battery system short circuit (warning)
  b6 6A   Internal battery hardware fault (warning)
  b7 A9   Battery imbalancing fault (warning)
  b7 A6   Battery service life expiry (warning)
  b7 9A   Battery system thermal management defective (warning)
  b7 6A   Internal battery hardware fault (warning)

*/
}

void SmaBydHInverter::map_can_frame_to_variable(CAN_frame rx_frame) {
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
      /* FALLTHROUGH */
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

void SmaBydHInverter::transmit_can(unsigned long currentMillis) {

  if (transmit_can_init) {

    // Check if enough time has passed since the last batch
    if (currentMillis - previousMillisBatch >= delay_between_batches_ms) {
      previousMillisBatch = currentMillis;  // Update the time of the last message batch

      // Send a subset of messages per iteration to avoid overloading the CAN bus / transmit buffer
      switch (batch_send_index) {
        case 0:
          transmit_can_frame(&SMA_558);
          transmit_can_frame(&SMA_598);
          transmit_can_frame(&SMA_5D8);
          break;
        case 1:
          transmit_can_frame(&SMA_618_1);
          transmit_can_frame(&SMA_618_2);
          transmit_can_frame(&SMA_618_3);
          break;
        case 2:
          transmit_can_frame(&SMA_158);
          transmit_can_frame(&SMA_358);
          transmit_can_frame(&SMA_3D8);
          break;
        case 3:
          transmit_can_frame(&SMA_458);
          transmit_can_frame(&SMA_518);
          transmit_can_frame(&SMA_4D8);
          transmit_can_init = false;
          break;
        default:
          break;
      }

      // Increment message index and wrap around if needed
      batch_send_index++;

      if (transmit_can_init == false) {  //We completed sending the batches
        batch_send_index = 0;
      }
    }
  }

  // Send CAN Message every 100ms if inverter allows contactor closing
  if (datalayer.system.status.inverter_allows_contactor_closing) {
    if (currentMillis - previousMillis100ms >= INTERVAL_100_MS) {
      previousMillis100ms = currentMillis;
      transmit_can_frame(&SMA_158);
      transmit_can_frame(&SMA_358);
      transmit_can_frame(&SMA_3D8);
      transmit_can_frame(&SMA_458);
      transmit_can_frame(&SMA_518);
      transmit_can_frame(&SMA_4D8);
    }
    // Send CAN Message every 60s (potentially SMA_458 is not required for stable operation)
    if (currentMillis - previousMillis60s >= INTERVAL_60_S) {
      previousMillis60s = currentMillis;
      transmit_can_frame(&SMA_458);
    }
  }
}
