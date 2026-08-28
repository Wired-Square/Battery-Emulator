#ifndef CELLPOWER_BMS_H
#define CELLPOWER_BMS_H
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "BatterySlotContext.h"
#include "CanBattery.h"

class CellPowerBms : public CanBattery {
 public:
  CellPowerBms(const BatterySlotContext& ctx) : CanBattery(ctx.can_interface, CAN_Speed::CAN_SPEED_250KBPS) {
    datalayer_battery = ctx.datalayer;
  }

  virtual void setup(void);
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);

  void write_advanced_status(AdvancedStatusWriter& out) override {
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

 private:
  DATALAYER_BATTERY_TYPE* datalayer_battery;
  unsigned long previousMillis1s = 0;  // will store last time a 1s CAN Message was sent

  /*Actual content messages
  // Optional add-on charger module. Might not be needed to send these towards the BMS to keep it happy.
  CAN_frame CELLPOWER_18FF50E9 = {.FD = false,
                                  .ext_ID = true,
                                  .DLC = 5,
                                  .ID = 0x18FF50E9,
                                  .data = {0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame CELLPOWER_18FF50E8 = {.FD = false,
                                  .ext_ID = true,
                                  .DLC = 5,
                                  .ID = 0x18FF50E8,
                                  .data = {0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame CELLPOWER_18FF50E7 = {.FD = false,
                                  .ext_ID = true,
                                  .DLC = 5,
                                  .ID = 0x18FF50E7,
                                  .data = {0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame CELLPOWER_18FF50E5 = {.FD = false,
                                  .ext_ID = true,
                                  .DLC = 5,
                                  .ID = 0x18FF50E5,
                                  .data = {0x00, 0x00, 0x00, 0x00, 0x00}};
  */
  bool system_state_discharge = false;
  bool system_state_charge = false;
  bool system_state_cellbalancing = false;
  bool system_state_tricklecharge = false;
  bool system_state_idle = false;
  bool system_state_chargecompleted = false;
  bool system_state_maintenancecharge = false;
  bool IO_state_main_positive_relay = false;
  bool IO_state_main_negative_relay = false;
  bool IO_state_charge_enable = false;
  bool IO_state_precharge_relay = false;
  bool IO_state_discharge_enable = false;
  bool IO_state_IO_6 = false;
  bool IO_state_IO_7 = false;
  bool IO_state_IO_8 = false;
  bool error_Cell_overvoltage = false;
  bool error_Cell_undervoltage = false;
  bool error_Cell_end_of_life_voltage = false;
  bool error_Cell_voltage_misread = false;
  bool error_Cell_over_temperature = false;
  bool error_Cell_under_temperature = false;
  bool error_Cell_unmanaged = false;
  bool error_LMU_over_temperature = false;
  bool error_LMU_under_temperature = false;
  bool error_Temp_sensor_open_circuit = false;
  bool error_Temp_sensor_short_circuit = false;
  bool error_SUB_communication = false;
  bool error_LMU_communication = false;
  bool error_Over_current_IN = false;
  bool error_Over_current_OUT = false;
  bool error_Short_circuit = false;
  bool error_Leak_detected = false;
  bool error_Leak_detection_failed = false;
  bool error_Voltage_difference = false;
  bool error_BMCU_supply_over_voltage = false;
  bool error_BMCU_supply_under_voltage = false;
  bool error_Main_positive_contactor = false;
  bool error_Main_negative_contactor = false;
  bool error_Precharge_contactor = false;
  bool error_Midpack_contactor = false;
  bool error_Precharge_timeout = false;
  bool error_Emergency_connector_override = false;
  bool warning_High_cell_voltage = false;
  bool warning_Low_cell_voltage = false;
  bool warning_High_cell_temperature = false;
  bool warning_Low_cell_temperature = false;
  bool warning_High_LMU_temperature = false;
  bool warning_Low_LMU_temperature = false;
  bool warning_SUB_communication_interfered = false;
  bool warning_LMU_communication_interfered = false;
  bool warning_High_current_IN = false;
  bool warning_High_current_OUT = false;
  bool warning_Pack_resistance_difference = false;
  bool warning_High_pack_resistance = false;
  bool warning_Cell_resistance_difference = false;
  bool warning_High_cell_resistance = false;
  bool warning_High_BMCU_supply_voltage = false;
  bool warning_Low_BMCU_supply_voltage = false;
  bool warning_Low_SOC = false;
  bool warning_Balancing_required_OCV_model = false;
  bool warning_Charger_not_responding = false;
  uint16_t cell_voltage_max_mV = 3700;
  uint16_t cell_voltage_min_mV = 3700;
  int8_t pack_temperature_high_C = 0;
  int8_t pack_temperature_low_C = 0;
  uint16_t battery_pack_voltage_dV = 3700;
  int16_t battery_pack_current_dA = 0;
  uint8_t battery_SOH_percentage = 99;
  uint8_t battery_SOC_percentage = 50;
  uint16_t battery_remaining_dAh = 0;
  uint8_t cell_with_highest_voltage = 0;
  uint8_t cell_with_lowest_voltage = 0;
  uint16_t requested_charge_current_dA = 0;
  uint16_t average_charge_current_dA = 0;
  uint16_t actual_charge_current_dA = 0;
  bool requested_exceeding_average_current = 0;
  bool error_state = false;
};

#endif
