#include "SMA-BYD-HVS-CAN.h"
#include "../communication/can/comm_can.h"
#include "../datalayer/datalayer.h"
#include "../devboard/utils/events.h"

/* TODO:
- Figure out the manufacturer info needed in transmit_can_init() CAN messages
  - CAN logs from real system might be needed
- Figure out how cellvoltages need to be displayed
- Figure out if sending transmit_can_init() like we do now is OK
- Figure out how to send the non-cyclic messages when needed
*/

void SmaBydHvsInverter::update_values() {
  fill_measurement_frames(SMA_358, SMA_3D8, SMA_4D8, SMA_518, SMA_458, datalayer.battery.combined.status.reported_current_dA);

  control_contactor_led();
  check_enable_line();
}

void SmaBydHvsInverter::map_can_frame_to_variable(CAN_frame rx_frame) {
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

    case 0x5E0:  //Message originating from SMA inverter - String
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      //Inverter brand (frame1-3 = 0x53 0x4D 0x41) = SMA
      break;

    case 0x5E7:  //Message originating from SMA inverter - Pairing request
      /* FALLTHROUGH */
    case 0x660:  //Message originating from SMA inverter - Pairing request
      pairing_events++;
      set_event(EVENT_SMA_PAIRING, pairing_events);  // also printing a log entry
      datalayer.system.status.CAN_inverter_still_alive = CAN_STILL_ALIVE * 3;
      transmit_can_init();
      break;

    default:
      break;
  }
}

void SmaBydHvsInverter::pushFrame(CAN_frame* frame, std::function<void(void)> callback) {
  if (listLength >= 20) {
    return;  //TODO: scream.
  }
  framesToSend[listLength] = {
      .frame = frame,
      .callback = callback,
  };
  listLength++;
}

void SmaBydHvsInverter::transmit_can(unsigned long currentMillis) {

  // Send CAN Message only if we're enabled by inverter
  if (!datalayer.system.status.inverter_allows_contactor_closing) {
    return;
  }

  if (listLength > 0 && currentMillis - previousMillis250ms >= INTERVAL_250_MS) {
    previousMillis250ms = currentMillis;
    // Send next frame.
    Frame frame = framesToSend[0];
    transmit_can_frame(frame.frame);
    frame.callback();
    for (int i = 0; i < listLength - 1; i++) {
      framesToSend[i] = framesToSend[i + 1];
    }
    listLength--;
  }

  if (!pairing_completed) {
    return;
  }

  // Send CAN Message every 2s
  if (currentMillis - previousMillis2s >= INTERVAL_2_S) {
    previousMillis2s = currentMillis;
    pushFrame(&SMA_358);
  }
  // Send CAN Message every 10s
  if (currentMillis - previousMillis10s >= INTERVAL_10_S) {
    previousMillis10s = currentMillis;
    pushFrame(&SMA_518);
    pushFrame(&SMA_4D8);
    pushFrame(&SMA_3D8);
  }
  // Send CAN Message every 60s (potentially SMA_458 is not required for stable operation)
  if (currentMillis - previousMillis60s >= INTERVAL_60_S) {
    previousMillis60s = currentMillis;
    pushFrame(&SMA_458);
  }
}

void SmaBydHvsInverter::completePairing() {
  pairing_completed = true;
}

void SmaBydHvsInverter::transmit_can_init() {
  listLength = 0;  // clear all frames

  pushFrame(&SMA_558);    //Pairing start - Vendor
  pushFrame(&SMA_598);    //Serial
  pushFrame(&SMA_5D8);    //BYD
  pushFrame(&SMA_618_0);  //BATTERY
  pushFrame(&SMA_618_1);  //-Box Pr
  pushFrame(&SMA_618_2);  //emium H
  pushFrame(&SMA_618_3);  //VS
  pushFrame(&SMA_358);
  pushFrame(&SMA_3D8);
  pushFrame(&SMA_458);
  pushFrame(&SMA_4D8);
  pushFrame(&SMA_518, [this]() { this->completePairing(); });
}
