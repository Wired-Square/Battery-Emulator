#include "CELLPOWER-BMS.h"
#include "../battery/BATTERIES.h"
#include "../communication/can/comm_can.h"
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"  //For "More battery info" webpage
#include "../devboard/utils/events.h"

void CellPowerBms::update_values() {

  /* Update values from CAN */

  datalayer_battery->status.real_soc = battery_SOC_percentage * 100;

  datalayer_battery->status.remaining_capacity_Wh = static_cast<uint32_t>(
      (static_cast<double>(datalayer_battery->status.real_soc) / 10000) * datalayer_battery->info.total_capacity_Wh);

  datalayer_battery->status.soh_pptt = battery_SOH_percentage * 100;

  datalayer_battery->status.voltage_dV = battery_pack_voltage_dV;

  datalayer_battery->status.current_dA = battery_pack_current_dA;

  datalayer_battery->status.max_charge_power_W =
      datalayer_battery->status.override_charge_power_W;  //TODO, is this available via CAN?

  datalayer_battery->status.max_discharge_power_W =
      datalayer_battery->status.override_discharge_power_W;  //TODO, is this available via CAN?

  datalayer_battery->status.temperature_min_dC = (int16_t)(pack_temperature_low_C * 10);

  datalayer_battery->status.temperature_max_dC = (int16_t)(pack_temperature_high_C * 10);

  datalayer_battery->status.cell_max_voltage_mV = cell_voltage_max_mV;

  datalayer_battery->status.cell_min_voltage_mV = cell_voltage_min_mV;

  /* Update webserver datalayer */
  datalayer_extended.cellpower.system_state_discharge = system_state_discharge;
  datalayer_extended.cellpower.system_state_charge = system_state_charge;
  datalayer_extended.cellpower.system_state_cellbalancing = system_state_cellbalancing;
  datalayer_extended.cellpower.system_state_tricklecharge = system_state_tricklecharge;
  datalayer_extended.cellpower.system_state_idle = system_state_idle;
  datalayer_extended.cellpower.system_state_chargecompleted = system_state_chargecompleted;
  datalayer_extended.cellpower.system_state_maintenancecharge = system_state_maintenancecharge;
  datalayer_extended.cellpower.IO_state_main_positive_relay = IO_state_main_positive_relay;
  datalayer_extended.cellpower.IO_state_main_negative_relay = IO_state_main_negative_relay;
  datalayer_extended.cellpower.IO_state_charge_enable = IO_state_charge_enable;
  datalayer_extended.cellpower.IO_state_precharge_relay = IO_state_precharge_relay;
  datalayer_extended.cellpower.IO_state_discharge_enable = IO_state_discharge_enable;
  datalayer_extended.cellpower.IO_state_IO_6 = IO_state_IO_6;
  datalayer_extended.cellpower.IO_state_IO_7 = IO_state_IO_7;
  datalayer_extended.cellpower.IO_state_IO_8 = IO_state_IO_8;
  datalayer_extended.cellpower.error_Cell_overvoltage = error_Cell_overvoltage;
  datalayer_extended.cellpower.error_Cell_undervoltage = error_Cell_undervoltage;
  datalayer_extended.cellpower.error_Cell_end_of_life_voltage = error_Cell_end_of_life_voltage;
  datalayer_extended.cellpower.error_Cell_voltage_misread = error_Cell_voltage_misread;
  datalayer_extended.cellpower.error_Cell_over_temperature = error_Cell_over_temperature;
  datalayer_extended.cellpower.error_Cell_under_temperature = error_Cell_under_temperature;
  datalayer_extended.cellpower.error_Cell_unmanaged = error_Cell_unmanaged;
  datalayer_extended.cellpower.error_LMU_over_temperature = error_LMU_over_temperature;
  datalayer_extended.cellpower.error_LMU_under_temperature = error_LMU_under_temperature;
  datalayer_extended.cellpower.error_Temp_sensor_open_circuit = error_Temp_sensor_open_circuit;
  datalayer_extended.cellpower.error_Temp_sensor_short_circuit = error_Temp_sensor_short_circuit;
  datalayer_extended.cellpower.error_SUB_communication = error_SUB_communication;
  datalayer_extended.cellpower.error_LMU_communication = error_LMU_communication;
  datalayer_extended.cellpower.error_Over_current_IN = error_Over_current_IN;
  datalayer_extended.cellpower.error_Over_current_OUT = error_Over_current_OUT;
  datalayer_extended.cellpower.error_Short_circuit = error_Short_circuit;
  datalayer_extended.cellpower.error_Leak_detected = error_Leak_detected;
  datalayer_extended.cellpower.error_Leak_detection_failed = error_Leak_detection_failed;
  datalayer_extended.cellpower.error_Voltage_difference = error_Voltage_difference;
  datalayer_extended.cellpower.error_BMCU_supply_over_voltage = error_BMCU_supply_over_voltage;
  datalayer_extended.cellpower.error_BMCU_supply_under_voltage = error_BMCU_supply_under_voltage;
  datalayer_extended.cellpower.error_Main_positive_contactor = error_Main_positive_contactor;
  datalayer_extended.cellpower.error_Main_negative_contactor = error_Main_negative_contactor;
  datalayer_extended.cellpower.error_Precharge_contactor = error_Precharge_contactor;
  datalayer_extended.cellpower.error_Midpack_contactor = error_Midpack_contactor;
  datalayer_extended.cellpower.error_Precharge_timeout = error_Precharge_timeout;
  datalayer_extended.cellpower.error_Emergency_connector_override = error_Emergency_connector_override;
  datalayer_extended.cellpower.warning_High_cell_voltage = warning_High_cell_voltage;
  datalayer_extended.cellpower.warning_Low_cell_voltage = warning_Low_cell_voltage;
  datalayer_extended.cellpower.warning_High_cell_temperature = warning_High_cell_temperature;
  datalayer_extended.cellpower.warning_Low_cell_temperature = warning_Low_cell_temperature;
  datalayer_extended.cellpower.warning_High_LMU_temperature = warning_High_LMU_temperature;
  datalayer_extended.cellpower.warning_Low_LMU_temperature = warning_Low_LMU_temperature;
  datalayer_extended.cellpower.warning_SUB_communication_interfered = warning_SUB_communication_interfered;
  datalayer_extended.cellpower.warning_LMU_communication_interfered = warning_LMU_communication_interfered;
  datalayer_extended.cellpower.warning_High_current_IN = warning_High_current_IN;
  datalayer_extended.cellpower.warning_High_current_OUT = warning_High_current_OUT;
  datalayer_extended.cellpower.warning_Pack_resistance_difference = warning_Pack_resistance_difference;
  datalayer_extended.cellpower.warning_High_pack_resistance = warning_High_pack_resistance;
  datalayer_extended.cellpower.warning_Cell_resistance_difference = warning_Cell_resistance_difference;
  datalayer_extended.cellpower.warning_High_cell_resistance = warning_High_cell_resistance;
  datalayer_extended.cellpower.warning_High_BMCU_supply_voltage = warning_High_BMCU_supply_voltage;
  datalayer_extended.cellpower.warning_Low_BMCU_supply_voltage = warning_Low_BMCU_supply_voltage;
  datalayer_extended.cellpower.warning_Low_SOC = warning_Low_SOC;
  datalayer_extended.cellpower.warning_Balancing_required_OCV_model = warning_Balancing_required_OCV_model;
  datalayer_extended.cellpower.warning_Charger_not_responding = warning_Charger_not_responding;

  /* Peform safety checks */
  if (system_state_chargecompleted) {
    //TODO, shall we set max_charge_power_W to 0 incase this is true?
  }
  if (IO_state_charge_enable) {
    //TODO, shall we react on this?
  }
  if (IO_state_discharge_enable) {
    //TODO, shall we react on this?
  }
  if (error_state) {
    //TODO, shall we react on this?
  }
}

void CellPowerBms::handle_incoming_can_frame(CAN_frame rx_frame) {

  switch (rx_frame.ID) {
    case 0x1A4:  //PDO1_TX - 200ms
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      cell_voltage_max_mV = (uint16_t)((rx_frame.data.u8[1] << 8) | rx_frame.data.u8[0]);
      cell_voltage_min_mV = (uint16_t)((rx_frame.data.u8[3] << 8) | rx_frame.data.u8[2]);
      pack_temperature_high_C = (int8_t)rx_frame.data.u8[4];
      pack_temperature_low_C = (int8_t)rx_frame.data.u8[5];
      system_state_discharge = (rx_frame.data.u8[6] & 0x01);
      system_state_charge = ((rx_frame.data.u8[6] & 0x02) >> 1);
      system_state_cellbalancing = ((rx_frame.data.u8[6] & 0x04) >> 2);
      system_state_tricklecharge = ((rx_frame.data.u8[6] & 0x08) >> 3);
      system_state_idle = ((rx_frame.data.u8[6] & 0x10) >> 4);
      system_state_chargecompleted = ((rx_frame.data.u8[6] & 0x20) >> 5);
      system_state_maintenancecharge = ((rx_frame.data.u8[6] & 0x40) >> 6);
      IO_state_main_positive_relay = (rx_frame.data.u8[7] & 0x01);
      IO_state_main_negative_relay = ((rx_frame.data.u8[7] & 0x02) >> 1);
      IO_state_charge_enable = ((rx_frame.data.u8[7] & 0x04) >> 2);
      IO_state_precharge_relay = ((rx_frame.data.u8[7] & 0x08) >> 3);
      IO_state_discharge_enable = ((rx_frame.data.u8[7] & 0x10) >> 4);
      IO_state_IO_6 = ((rx_frame.data.u8[7] & 0x20) >> 5);
      IO_state_IO_7 = ((rx_frame.data.u8[7] & 0x40) >> 6);
      IO_state_IO_8 = ((rx_frame.data.u8[7] & 0x80) >> 7);
      break;
    case 0x2A4:  //PDO2_TX - 200ms
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      battery_pack_voltage_dV = (uint16_t)((rx_frame.data.u8[1] << 8) | rx_frame.data.u8[0]);
      battery_pack_current_dA = (int16_t)((rx_frame.data.u8[3] << 8) | rx_frame.data.u8[2]);
      battery_SOH_percentage = (uint8_t)rx_frame.data.u8[4];
      battery_SOC_percentage = (uint8_t)rx_frame.data.u8[5];
      battery_remaining_dAh = (uint16_t)((rx_frame.data.u8[7] << 8) | rx_frame.data.u8[6]);
      break;
    case 0x3A4:  //PDO3_TX - 200ms
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      cell_with_highest_voltage = (uint8_t)rx_frame.data.u8[0];
      cell_with_lowest_voltage = (uint8_t)rx_frame.data.u8[1];
      break;
    case 0x4A4:  //PDO4_TX - 200ms
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      error_Cell_overvoltage = (rx_frame.data.u8[0] & 0x01);
      error_Cell_undervoltage = ((rx_frame.data.u8[0] & 0x02) >> 1);
      error_Cell_end_of_life_voltage = ((rx_frame.data.u8[0] & 0x04) >> 2);
      error_Cell_voltage_misread = ((rx_frame.data.u8[0] & 0x08) >> 3);
      error_Cell_over_temperature = ((rx_frame.data.u8[0] & 0x10) >> 4);
      error_Cell_under_temperature = ((rx_frame.data.u8[0] & 0x20) >> 5);
      error_Cell_unmanaged = ((rx_frame.data.u8[0] & 0x40) >> 6);
      error_LMU_over_temperature = ((rx_frame.data.u8[0] & 0x80) >> 7);
      error_LMU_under_temperature = (rx_frame.data.u8[1] & 0x01);
      error_Temp_sensor_open_circuit = ((rx_frame.data.u8[1] & 0x02) >> 1);
      error_Temp_sensor_short_circuit = ((rx_frame.data.u8[1] & 0x04) >> 2);
      error_SUB_communication = ((rx_frame.data.u8[1] & 0x08) >> 3);
      error_LMU_communication = ((rx_frame.data.u8[1] & 0x10) >> 4);
      error_Over_current_IN = ((rx_frame.data.u8[1] & 0x20) >> 5);
      error_Over_current_OUT = ((rx_frame.data.u8[1] & 0x40) >> 6);
      error_Short_circuit = ((rx_frame.data.u8[1] & 0x80) >> 7);
      error_Leak_detected = (rx_frame.data.u8[2] & 0x01);
      error_Leak_detection_failed = ((rx_frame.data.u8[2] & 0x02) >> 1);
      error_Voltage_difference = ((rx_frame.data.u8[2] & 0x04) >> 2);
      error_BMCU_supply_over_voltage = ((rx_frame.data.u8[2] & 0x08) >> 3);
      error_BMCU_supply_under_voltage = ((rx_frame.data.u8[2] & 0x10) >> 4);
      error_Main_positive_contactor = ((rx_frame.data.u8[2] & 0x20) >> 5);
      error_Main_negative_contactor = ((rx_frame.data.u8[2] & 0x40) >> 6);
      error_Precharge_contactor = ((rx_frame.data.u8[2] & 0x80) >> 7);
      error_Midpack_contactor = (rx_frame.data.u8[3] & 0x01);
      error_Precharge_timeout = ((rx_frame.data.u8[3] & 0x02) >> 1);
      error_Emergency_connector_override = ((rx_frame.data.u8[3] & 0x04) >> 2);
      warning_High_cell_voltage = (rx_frame.data.u8[4] & 0x01);
      warning_Low_cell_voltage = ((rx_frame.data.u8[4] & 0x02) >> 1);
      warning_High_cell_temperature = ((rx_frame.data.u8[4] & 0x04) >> 2);
      warning_Low_cell_temperature = ((rx_frame.data.u8[4] & 0x08) >> 3);
      warning_High_LMU_temperature = ((rx_frame.data.u8[4] & 0x10) >> 4);
      warning_Low_LMU_temperature = ((rx_frame.data.u8[4] & 0x20) >> 5);
      warning_SUB_communication_interfered = ((rx_frame.data.u8[4] & 0x40) >> 6);
      warning_LMU_communication_interfered = ((rx_frame.data.u8[4] & 0x80) >> 7);
      warning_High_current_IN = (rx_frame.data.u8[5] & 0x01);
      warning_High_current_OUT = ((rx_frame.data.u8[5] & 0x02) >> 1);
      warning_Pack_resistance_difference = ((rx_frame.data.u8[5] & 0x04) >> 2);
      warning_High_pack_resistance = ((rx_frame.data.u8[5] & 0x08) >> 3);
      warning_Cell_resistance_difference = ((rx_frame.data.u8[5] & 0x10) >> 4);
      warning_High_cell_resistance = ((rx_frame.data.u8[5] & 0x20) >> 5);
      warning_High_BMCU_supply_voltage = ((rx_frame.data.u8[5] & 0x40) >> 6);
      warning_Low_BMCU_supply_voltage = ((rx_frame.data.u8[5] & 0x80) >> 7);
      warning_Low_SOC = (rx_frame.data.u8[6] & 0x01);
      warning_Balancing_required_OCV_model = ((rx_frame.data.u8[6] & 0x02) >> 1);
      warning_Charger_not_responding = ((rx_frame.data.u8[6] & 0x04) >> 2);
      break;
    case 0x7A4:  //PDO7_TX - 200ms
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      requested_charge_current_dA = (uint16_t)((rx_frame.data.u8[1] << 8) | rx_frame.data.u8[0]);
      average_charge_current_dA = (uint16_t)((rx_frame.data.u8[3] << 8) | rx_frame.data.u8[2]);
      actual_charge_current_dA = (uint16_t)((rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4]);
      requested_exceeding_average_current = (rx_frame.data.u8[6] & 0x01);
      break;
    case 0x7A5:  //PDO7.1_TX - 200ms
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      error_state = (rx_frame.data.u8[0] & 0x01);
      break;
    default:
      break;
  }
}

void CellPowerBms::transmit_can(unsigned long currentMillis) {
  /*
  // Send 1s CAN Message. NOTE; Not required to keep BMS happy
  if (currentMillis - previousMillis1s >= INTERVAL_1_S) {
    previousMillis1s = currentMillis;
    
    transmit_can_frame(&CELLPOWER_18FF50E9);
    transmit_can_frame(&CELLPOWER_18FF50E8);
    transmit_can_frame(&CELLPOWER_18FF50E7);
    transmit_can_frame(&CELLPOWER_18FF50E5);

  }
  */
}

void CellPowerBms::setup(void) {  // Performs one time setup at startup
  datalayer.system.status.battery_allows_contactor_closing = true;
  datalayer_battery->info.max_design_voltage_dV = user_selected_max_pack_voltage_dV;
  datalayer_battery->info.min_design_voltage_dV = user_selected_min_pack_voltage_dV;
  datalayer_battery->info.max_cell_voltage_mV = user_selected_max_cell_voltage_mV;
  datalayer_battery->info.min_cell_voltage_mV = user_selected_min_cell_voltage_mV;
}

void CellPowerBms::write_advanced_status(AdvancedStatusWriter& out) {
  static const char* falseTrue[2] = {"False", "True"};

  out.section(TL("States"));
  out.kv(TL("Discharge"), falseTrue[datalayer_extended.cellpower.system_state_discharge]);
  out.kv(TL("Charge"), falseTrue[datalayer_extended.cellpower.system_state_charge]);
  out.kv(TL("Cellbalancing"), falseTrue[datalayer_extended.cellpower.system_state_cellbalancing]);
  out.kv(TL("Tricklecharging"), falseTrue[datalayer_extended.cellpower.system_state_tricklecharge]);
  out.kv(TL("Idle"), falseTrue[datalayer_extended.cellpower.system_state_idle]);
  out.kv(TL("Charge completed"), falseTrue[datalayer_extended.cellpower.system_state_chargecompleted]);
  out.kv(TL("Maintenance charge"), falseTrue[datalayer_extended.cellpower.system_state_maintenancecharge]);

  out.section("IO");
  out.kv(TL("Main positive relay"), falseTrue[datalayer_extended.cellpower.IO_state_main_positive_relay]);
  out.kv(TL("Main negative relay"), falseTrue[datalayer_extended.cellpower.IO_state_main_negative_relay]);
  out.kv(TL("Charge enabled"), falseTrue[datalayer_extended.cellpower.IO_state_charge_enable]);
  out.kv(TL("Precharge relay"), falseTrue[datalayer_extended.cellpower.IO_state_precharge_relay]);
  out.kv(TL("Discharge enable"), falseTrue[datalayer_extended.cellpower.IO_state_discharge_enable]);
  out.kv(TL("IO 6"), falseTrue[datalayer_extended.cellpower.IO_state_IO_6]);
  out.kv(TL("IO 7"), falseTrue[datalayer_extended.cellpower.IO_state_IO_7]);
  out.kv(TL("IO 8"), falseTrue[datalayer_extended.cellpower.IO_state_IO_8]);

  out.section(TL("Errors"));
  out.kv(TL("Cell overvoltage"), falseTrue[datalayer_extended.cellpower.error_Cell_overvoltage]);
  out.kv(TL("Cell undervoltage"), falseTrue[datalayer_extended.cellpower.error_Cell_undervoltage]);
  out.kv(TL("Cell end of life voltage"), falseTrue[datalayer_extended.cellpower.error_Cell_end_of_life_voltage]);
  out.kv(TL("Cell voltage misread"), falseTrue[datalayer_extended.cellpower.error_Cell_voltage_misread]);
  out.kv(TL("Cell over temperature"), falseTrue[datalayer_extended.cellpower.error_Cell_over_temperature]);
  out.kv(TL("Cell under temperature"), falseTrue[datalayer_extended.cellpower.error_Cell_under_temperature]);
  out.kv(TL("Cell unmanaged"), falseTrue[datalayer_extended.cellpower.error_Cell_unmanaged]);
  out.kv(TL("LMU over temperature"), falseTrue[datalayer_extended.cellpower.error_LMU_over_temperature]);
  out.kv(TL("LMU under temperature"), falseTrue[datalayer_extended.cellpower.error_LMU_under_temperature]);
  out.kv(TL("Temp sensor open circuit"), falseTrue[datalayer_extended.cellpower.error_Temp_sensor_open_circuit]);
  out.kv(TL("Temp sensor short circuit"), falseTrue[datalayer_extended.cellpower.error_Temp_sensor_short_circuit]);
  out.kv(TL("SUB comm"), falseTrue[datalayer_extended.cellpower.error_SUB_communication]);
  out.kv(TL("LMU comm"), falseTrue[datalayer_extended.cellpower.error_LMU_communication]);
  out.kv(TL("Over current In"), falseTrue[datalayer_extended.cellpower.error_Over_current_IN]);
  out.kv(TL("Over current Out"), falseTrue[datalayer_extended.cellpower.error_Over_current_OUT]);
  out.kv(TL("Short circuit"), falseTrue[datalayer_extended.cellpower.error_Short_circuit]);
  out.kv(TL("Leak detected"), falseTrue[datalayer_extended.cellpower.error_Leak_detected]);
  out.kv(TL("Leak detection failed"), falseTrue[datalayer_extended.cellpower.error_Leak_detection_failed]);
  out.kv(TL("Voltage diff"), falseTrue[datalayer_extended.cellpower.error_Voltage_difference]);
  out.kv(TL("BMCU supply overvoltage"), falseTrue[datalayer_extended.cellpower.error_BMCU_supply_over_voltage]);
  out.kv(TL("BMCU supply undervoltage"), falseTrue[datalayer_extended.cellpower.error_BMCU_supply_under_voltage]);
  out.kv(TL("Main positive contactor"), falseTrue[datalayer_extended.cellpower.error_Main_positive_contactor]);
  out.kv(TL("Main negative contactor"), falseTrue[datalayer_extended.cellpower.error_Main_negative_contactor]);
  out.kv(TL("Precharge contactor"), falseTrue[datalayer_extended.cellpower.error_Precharge_contactor]);
  out.kv(TL("Midpack contactor"), falseTrue[datalayer_extended.cellpower.error_Midpack_contactor]);
  out.kv(TL("Precharge timeout"), falseTrue[datalayer_extended.cellpower.error_Precharge_timeout]);
  out.kv(TL("EMG connector override"), falseTrue[datalayer_extended.cellpower.error_Emergency_connector_override]);

  out.section(TL("Warnings"));
  out.kv(TL("High cell voltage"), falseTrue[datalayer_extended.cellpower.warning_High_cell_voltage]);
  out.kv(TL("Low cell voltage"), falseTrue[datalayer_extended.cellpower.warning_Low_cell_voltage]);
  out.kv(TL("High cell temperature"), falseTrue[datalayer_extended.cellpower.warning_High_cell_temperature]);
  out.kv(TL("Low cell temperature"), falseTrue[datalayer_extended.cellpower.warning_Low_cell_temperature]);
  out.kv(TL("High LMU temperature"), falseTrue[datalayer_extended.cellpower.warning_High_LMU_temperature]);
  out.kv(TL("Low LMU temperature"), falseTrue[datalayer_extended.cellpower.warning_Low_LMU_temperature]);
  out.kv(TL("SUB comm interf"), falseTrue[datalayer_extended.cellpower.warning_SUB_communication_interfered]);
  out.kv(TL("LMU comm interf"), falseTrue[datalayer_extended.cellpower.warning_LMU_communication_interfered]);
  out.kv(TL("High current In"), falseTrue[datalayer_extended.cellpower.warning_High_current_IN]);
  out.kv(TL("High current Out"), falseTrue[datalayer_extended.cellpower.warning_High_current_OUT]);
  out.kv(TL("Pack resistance diff"), falseTrue[datalayer_extended.cellpower.warning_Pack_resistance_difference]);
  out.kv(TL("High pack resistance"), falseTrue[datalayer_extended.cellpower.warning_High_pack_resistance]);
  out.kv(TL("Cell resistance diff"), falseTrue[datalayer_extended.cellpower.warning_Cell_resistance_difference]);
  out.kv(TL("High cell resistance"), falseTrue[datalayer_extended.cellpower.warning_High_cell_resistance]);
  out.kv(TL("High BMCU supply voltage"), falseTrue[datalayer_extended.cellpower.warning_High_BMCU_supply_voltage]);
  out.kv(TL("Low BMCU supply voltage"), falseTrue[datalayer_extended.cellpower.warning_Low_BMCU_supply_voltage]);
  out.kv(TL("Low SOC"), falseTrue[datalayer_extended.cellpower.warning_Low_SOC]);
  out.kv(TL("Balancing required"), falseTrue[datalayer_extended.cellpower.warning_Balancing_required_OCV_model]);
  out.kv(TL("Charger not responding"), falseTrue[datalayer_extended.cellpower.warning_Charger_not_responding]);
}
