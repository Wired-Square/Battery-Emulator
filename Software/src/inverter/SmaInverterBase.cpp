#include "SmaInverterBase.h"

#include "../communication/can/comm_can.h"
#include "../datalayer/datalayer.h"
#include "../devboard/utils/events.h"

void SmaInverterBase::fill_measurement_frames(CAN_frame& f358, CAN_frame& f3D8, CAN_frame& f4D8, CAN_frame& f518,
                                              CAN_frame& f458, int16_t current_dA) {
  temperature_average =
      ((datalayer.battery.combined.status.temperature_max_dC + datalayer.battery.combined.status.temperature_min_dC) / 2);

  if (datalayer.battery.combined.status.voltage_dV > 10) {  // Only update value when we have voltage available to avoid div0
    ampere_hours_remaining =
        ((datalayer.battery.combined.status.reported_remaining_capacity_Wh / datalayer.battery.combined.status.voltage_dV) *
         100);  //(WH[10000] * V+1[3600])*100 = 270 (27.0Ah)
  }

  //Maxvoltage (eg 400.0V = 4000 , 16bits long)
  f358.data.u8[0] = (datalayer.battery.combined.info.max_design_voltage_dV >> 8);
  f358.data.u8[1] = (datalayer.battery.combined.info.max_design_voltage_dV & 0x00FF);
  //Minvoltage (eg 300.0V = 3000 , 16bits long)
  f358.data.u8[2] = (datalayer.battery.combined.info.min_design_voltage_dV >> 8);
  f358.data.u8[3] = (datalayer.battery.combined.info.min_design_voltage_dV & 0x00FF);
  //Discharge limited current, 500 = 50A, (0.1, A)
  f358.data.u8[4] = (datalayer.battery.combined.status.max_discharge_current_dA >> 8);
  f358.data.u8[5] = (datalayer.battery.combined.status.max_discharge_current_dA & 0x00FF);
  //Charge limited current, 125 =12.5A (0.1, A)
  f358.data.u8[6] = (datalayer.battery.combined.status.max_charge_current_dA >> 8);
  f358.data.u8[7] = (datalayer.battery.combined.status.max_charge_current_dA & 0x00FF);

  //SOC (100.00%)
  f3D8.data.u8[0] = (datalayer.battery.combined.status.reported_soc >> 8);
  f3D8.data.u8[1] = (datalayer.battery.combined.status.reported_soc & 0x00FF);
  //StateOfHealth (100.00%)
  f3D8.data.u8[2] = (datalayer.battery.combined.status.soh_pptt >> 8);
  f3D8.data.u8[3] = (datalayer.battery.combined.status.soh_pptt & 0x00FF);
  //State of charge (AH, 0.1)
  f3D8.data.u8[4] = (ampere_hours_remaining >> 8);
  f3D8.data.u8[5] = (ampere_hours_remaining & 0x00FF);

  //Voltage (370.0)
  f4D8.data.u8[0] = (datalayer.battery.combined.status.voltage_dV >> 8);
  f4D8.data.u8[1] = (datalayer.battery.combined.status.voltage_dV & 0x00FF);
  //Current (TODO: signed OK?)
  f4D8.data.u8[2] = (current_dA >> 8);
  f4D8.data.u8[3] = (current_dA & 0x00FF);
  //Temperature average
  f4D8.data.u8[4] = (temperature_average >> 8);
  f4D8.data.u8[5] = (temperature_average & 0x00FF);
  //Battery ready
  if (datalayer.system.status.system_status == FAULT) {
    f4D8.data.u8[6] = STOP_STATE;
  } else {
    f4D8.data.u8[6] = READY_STATE;
  }

  //Highest battery temperature
  f518.data.u8[0] = (datalayer.battery.combined.status.temperature_max_dC >> 8);
  f518.data.u8[1] = (datalayer.battery.combined.status.temperature_max_dC & 0x00FF);
  //Lowest battery temperature
  f518.data.u8[2] = (datalayer.battery.combined.status.temperature_min_dC >> 8);
  f518.data.u8[3] = (datalayer.battery.combined.status.temperature_min_dC & 0x00FF);
  //Sum of all cellvoltages
  f518.data.u8[4] = (datalayer.battery.combined.status.voltage_dV >> 8);
  f518.data.u8[5] = (datalayer.battery.combined.status.voltage_dV & 0x00FF);
  //Cell min/max voltage (mV / 25)
  f518.data.u8[6] = (datalayer.battery.combined.status.cell_min_voltage_mV / 25);
  f518.data.u8[7] = (datalayer.battery.combined.status.cell_max_voltage_mV / 25);

  //Lifetime charged energy amount
  f458.data.u8[0] = (datalayer.battery.combined.status.total_charged_battery_Wh & 0xFF000000) >> 24;
  f458.data.u8[1] = (datalayer.battery.combined.status.total_charged_battery_Wh & 0x00FF0000) >> 16;
  f458.data.u8[2] = (datalayer.battery.combined.status.total_charged_battery_Wh & 0x0000FF00) >> 8;
  f458.data.u8[3] = (datalayer.battery.combined.status.total_charged_battery_Wh & 0x000000FF);
  //Lifetime discharged energy amount
  f458.data.u8[4] = (datalayer.battery.combined.status.total_discharged_battery_Wh & 0xFF000000) >> 24;
  f458.data.u8[5] = (datalayer.battery.combined.status.total_discharged_battery_Wh & 0x00FF0000) >> 16;
  f458.data.u8[6] = (datalayer.battery.combined.status.total_discharged_battery_Wh & 0x0000FF00) >> 8;
  f458.data.u8[7] = (datalayer.battery.combined.status.total_discharged_battery_Wh & 0x000000FF);
}

void SmaInverterBase::check_enable_line() {
  // Check if Enable line is working. If we go too long without any input, raise an event
  if (!datalayer.system.status.inverter_allows_contactor_closing) {
    timeWithoutInverterAllowsContactorClosing++;

    if (timeWithoutInverterAllowsContactorClosing > THIRTY_MINUTES) {
      set_event(EVENT_NO_ENABLE_DETECTED, 0);
    }
  } else {
    timeWithoutInverterAllowsContactorClosing = 0;
  }
}
