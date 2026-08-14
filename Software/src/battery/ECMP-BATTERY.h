#ifndef STELLANTIS_ECMP_BATTERY_H
#define STELLANTIS_ECMP_BATTERY_H
#include <cstring>
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "BatterySlotContext.h"
#include "CanBattery.h"

//#define SIMULATE_ENTIRE_VEHICLE_ECMP
//Enable this to simulate the whole car (useful for when using external diagnostic tools)

class EcmpBattery : public CanBattery {
 public:
  EcmpBattery(const BatterySlotContext& ctx) : CanBattery(ctx.can_interface) {
    datalayer_battery = ctx.datalayer;
    allows_contactor_closing = ctx.is_primary() ? ctx.contactor_flag : nullptr;
    datalayer_ecmp = ctx.is_primary() ? &datalayer_extended.stellantisECMP : nullptr;
  }

  virtual void setup(void);
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);

  const std::vector<BatteryCommand>& get_commands() override { return commands_; }

  bool supports_insulation_resistance() override { return true; }

  BatteryAdvancedStatus get_advanced_status() override {
    BatteryAdvancedStatus status;
    AdvancedSection s;

    String main_connector_state;
    if (datalayer_extended.stellantisECMP.MainConnectorState == 0) {
      main_connector_state = "Contactors open";
    } else if (datalayer_extended.stellantisECMP.MainConnectorState == 0x01) {
      main_connector_state = "Precharged";
    } else {
      main_connector_state = "Invalid";
    }
    s.fields.push_back(kv("Main Connector State", main_connector_state));

    s.fields.push_back(kv("Insulation Resistance", String(datalayer_extended.stellantisECMP.InsulationResistance), "kOhm"));
    s.fields.push_back(kv("Interlock", datalayer_extended.stellantisECMP.InterlockOpen == true ? "BROKEN!" : "Seated OK"));

    String insulation_diag;
    if (datalayer_extended.stellantisECMP.InsulationDiag == 0) {
      insulation_diag = "No failure";
    } else if (datalayer_extended.stellantisECMP.InsulationDiag == 1) {
      insulation_diag = "Symmetric failure";
    } else {  //4 Invalid, 5-7 illegal, wrap em under one text
      insulation_diag = "N/A";
    }
    s.fields.push_back(kv("Insulation Diag", insulation_diag));

    String weld_check;
    if (datalayer_extended.stellantisECMP.pid_welding_detection == 0) {
      weld_check = "OK";
    } else if (datalayer_extended.stellantisECMP.pid_welding_detection == 255) {
      weld_check = "N/A";
    } else {  //Problem
      weld_check = "WELDED!" + String(datalayer_extended.stellantisECMP.pid_welding_detection);
    }
    s.fields.push_back(kv("Contactor weld check", weld_check));

    String opening_reason;
    if (datalayer_extended.stellantisECMP.pid_reason_open == 7) {
      opening_reason = "Invalid Status";
    } else if (datalayer_extended.stellantisECMP.pid_reason_open == 255) {
      opening_reason = "N/A";
    } else {  //Problem (Also status 0 might be OK?)
      opening_reason = "Unknown" + String(datalayer_extended.stellantisECMP.pid_reason_open);
    }
    s.fields.push_back(kv("Contactor opening reason", opening_reason));

    s.fields.push_back(kv("Status of power switch",
                          datalayer_extended.stellantisECMP.pid_contactor_status == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_contactor_status)));
    s.fields.push_back(kv("Negative power switch control",
                          datalayer_extended.stellantisECMP.pid_negative_contactor_control == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_negative_contactor_control)));
    s.fields.push_back(kv("Negative power switch status",
                          datalayer_extended.stellantisECMP.pid_negative_contactor_status == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_negative_contactor_status)));
    s.fields.push_back(kv("Positive power switch control",
                          datalayer_extended.stellantisECMP.pid_positive_contactor_control == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_positive_contactor_control)));
    s.fields.push_back(kv("Positive power switch status",
                          datalayer_extended.stellantisECMP.pid_positive_contactor_status == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_positive_contactor_status)));
    s.fields.push_back(kv("Contactor negative",
                          datalayer_extended.stellantisECMP.pid_contactor_negative == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_contactor_negative)));
    s.fields.push_back(kv("Contactor positive",
                          datalayer_extended.stellantisECMP.pid_contactor_positive == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_contactor_positive)));
    s.fields.push_back(kv("Precharge control",
                          datalayer_extended.stellantisECMP.pid_precharge_relay_control == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_precharge_relay_control)));
    s.fields.push_back(kv("Precharge status",
                          datalayer_extended.stellantisECMP.pid_precharge_relay_status == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_precharge_relay_status)));
    s.fields.push_back(kv("Recharge Status",
                          datalayer_extended.stellantisECMP.pid_recharge_status == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_recharge_status)));
    s.fields.push_back(kv("Delta temperature",
                          datalayer_extended.stellantisECMP.pid_delta_temperature == 127
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_delta_temperature),
                          "°C"));
    s.fields.push_back(kv("Lowest temperature",
                          datalayer_extended.stellantisECMP.pid_lowest_temperature == 127
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_lowest_temperature),
                          "°C"));
    s.fields.push_back(kv("Average temperature",
                          datalayer_extended.stellantisECMP.pid_average_temperature == 127
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_average_temperature),
                          "°C"));
    s.fields.push_back(kv("Highest temperature",
                          datalayer_extended.stellantisECMP.pid_highest_temperature == 127
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_highest_temperature),
                          "°C"));
    s.fields.push_back(kv("Coldest module",
                          datalayer_extended.stellantisECMP.pid_coldest_module == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_coldest_module)));
    s.fields.push_back(kv("Hottest module",
                          datalayer_extended.stellantisECMP.pid_hottest_module == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_hottest_module)));
    s.fields.push_back(kv("Average cell voltage",
                          datalayer_extended.stellantisECMP.pid_avg_cell_voltage == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_avg_cell_voltage),
                          "mV"));
    s.fields.push_back(kv("High precision current",
                          datalayer_extended.stellantisECMP.pid_current == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_current),
                          "mA"));
    s.fields.push_back(kv("Insulation resistance neg-gnd",
                          datalayer_extended.stellantisECMP.pid_insulation_res_neg == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_insulation_res_neg),
                          "kOhm"));
    s.fields.push_back(kv("Insulation resistance pos-gnd",
                          datalayer_extended.stellantisECMP.pid_insulation_res_pos == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_insulation_res_pos),
                          "kOhm"));
    s.fields.push_back(kv("Max current 10s",
                          datalayer_extended.stellantisECMP.pid_max_current_10s == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_max_current_10s)));
    s.fields.push_back(kv("Max discharge power 10s",
                          datalayer_extended.stellantisECMP.pid_max_discharge_10s == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_max_discharge_10s)));
    s.fields.push_back(kv("Max discharge power 30s",
                          datalayer_extended.stellantisECMP.pid_max_discharge_30s == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_max_discharge_30s)));
    s.fields.push_back(kv("Max charge power 10s",
                          datalayer_extended.stellantisECMP.pid_max_charge_10s == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_max_charge_10s)));
    s.fields.push_back(kv("Max charge power 30s",
                          datalayer_extended.stellantisECMP.pid_max_charge_30s == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_max_charge_30s)));
    s.fields.push_back(kv("Energy capacity",
                          datalayer_extended.stellantisECMP.pid_energy_capacity == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_energy_capacity)));
    s.fields.push_back(kv("Highest cell number",
                          datalayer_extended.stellantisECMP.pid_highest_cell_voltage_num == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_highest_cell_voltage_num)));
    s.fields.push_back(kv("Lowest cell voltage number",
                          datalayer_extended.stellantisECMP.pid_lowest_cell_voltage_num == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_lowest_cell_voltage_num)));
    s.fields.push_back(kv("Sum of all cell voltages",
                          datalayer_extended.stellantisECMP.pid_sum_of_cells == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_sum_of_cells),
                          "dV"));
    s.fields.push_back(kv("Cell min capacity",
                          datalayer_extended.stellantisECMP.pid_cell_min_capacity == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_cell_min_capacity)));
    s.fields.push_back(kv("Cell voltage measurement status",
                          datalayer_extended.stellantisECMP.pid_cell_voltage_measurement_status == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_cell_voltage_measurement_status)));
    s.fields.push_back(kv("Battery Insulation Resistance",
                          datalayer_extended.stellantisECMP.pid_insulation_res == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_insulation_res),
                          "kOhm"));
    s.fields.push_back(kv("Pack voltage",
                          datalayer_extended.stellantisECMP.pid_pack_voltage == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_pack_voltage),
                          "dV"));
    s.fields.push_back(kv("Highest cell voltage",
                          datalayer_extended.stellantisECMP.pid_high_cell_voltage == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_high_cell_voltage),
                          "mV"));
    s.fields.push_back(kv("Lowest cell voltage",
                          datalayer_extended.stellantisECMP.pid_low_cell_voltage == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_low_cell_voltage),
                          "mV"));
    s.fields.push_back(kv("Battery Energy",
                          datalayer_extended.stellantisECMP.pid_battery_energy == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_battery_energy)));
    s.fields.push_back(kv("Collision information Counter",
                          datalayer_extended.stellantisECMP.pid_crash_counter == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_crash_counter)));
    s.fields.push_back(kv("Collision Counter recieved by Wire",
                          datalayer_extended.stellantisECMP.pid_wire_crash == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_wire_crash)));
    s.fields.push_back(kv("Collision data sent from car to battery",
                          datalayer_extended.stellantisECMP.pid_CAN_crash == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_CAN_crash)));
    s.fields.push_back(kv("History data",
                          datalayer_extended.stellantisECMP.pid_history_data == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_history_data)));
    s.fields.push_back(kv("Low SOC counter",
                          datalayer_extended.stellantisECMP.pid_lowsoc_counter == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_lowsoc_counter)));
    s.fields.push_back(kv("Last CAN failure detail",
                          datalayer_extended.stellantisECMP.pid_last_can_failure_detail == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_last_can_failure_detail)));
    s.fields.push_back(kv("HW version number",
                          datalayer_extended.stellantisECMP.pid_hw_version_num == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_hw_version_num)));
    s.fields.push_back(kv("SW version number",
                          datalayer_extended.stellantisECMP.pid_sw_version_num == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_sw_version_num)));
    s.fields.push_back(kv("Factory mode",
                          datalayer_extended.stellantisECMP.pid_factory_mode_control == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_factory_mode_control)));

    char readableSerialNumber[14];  // One extra space for null terminator
    memcpy(readableSerialNumber, datalayer_extended.stellantisECMP.pid_battery_serial,
           sizeof(datalayer_extended.stellantisECMP.pid_battery_serial));
    readableSerialNumber[13] = '\0';  // Null terminate the string
    s.fields.push_back(kv("Battery serial", String(readableSerialNumber)));

    uint8_t day = (datalayer_extended.stellantisECMP.pid_date_of_manufacture >> 16) & 0xFF;
    uint8_t month = (datalayer_extended.stellantisECMP.pid_date_of_manufacture >> 8) & 0xFF;
    uint8_t year = datalayer_extended.stellantisECMP.pid_date_of_manufacture & 0xFF;
    s.fields.push_back(kv("Date of manufacture", String(day) + "/" + String(month) + "/" + String(year)));

    s.fields.push_back(kv("Aux fuse state",
                          datalayer_extended.stellantisECMP.pid_aux_fuse_state == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_aux_fuse_state)));
    s.fields.push_back(kv("Battery state",
                          datalayer_extended.stellantisECMP.pid_battery_state == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_battery_state)));
    s.fields.push_back(kv("Precharge short circuit",
                          datalayer_extended.stellantisECMP.pid_precharge_short_circuit == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_precharge_short_circuit)));
    s.fields.push_back(kv("Service plug state",
                          datalayer_extended.stellantisECMP.pid_eservice_plug_state == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_eservice_plug_state)));
    s.fields.push_back(kv("Main fuse state",
                          datalayer_extended.stellantisECMP.pid_mainfuse_state == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_mainfuse_state)));
    s.fields.push_back(kv("Most critical fault",
                          datalayer_extended.stellantisECMP.pid_most_critical_fault == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_most_critical_fault)));
    s.fields.push_back(kv("Current time",
                          datalayer_extended.stellantisECMP.pid_current_time == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_current_time),
                          "ticks"));
    s.fields.push_back(kv("Time sent by car",
                          datalayer_extended.stellantisECMP.pid_time_sent_by_car == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_time_sent_by_car),
                          "ticks"));
    s.fields.push_back(kv("12V",
                          datalayer_extended.stellantisECMP.pid_12v == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_12v)));

    String v12_abnormal;
    if (datalayer_extended.stellantisECMP.pid_12v_abnormal == 255) {
      v12_abnormal = "N/A";
    } else if (datalayer_extended.stellantisECMP.pid_12v_abnormal == 0) {
      v12_abnormal = "No";
    } else {
      v12_abnormal = "Yes";
    }
    s.fields.push_back(kv("12V abnormal", v12_abnormal));

    s.fields.push_back(kv("HVIL IN Voltage",
                          datalayer_extended.stellantisECMP.pid_hvil_in_voltage == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_hvil_in_voltage),
                          "mV"));
    s.fields.push_back(kv("HVIL Out Voltage",
                          datalayer_extended.stellantisECMP.pid_hvil_out_voltage == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_hvil_out_voltage),
                          "mV"));
    s.fields.push_back(kv("HVIL State",
                          datalayer_extended.stellantisECMP.pid_hvil_state == 255
                              ? "N/A"
                              : (datalayer_extended.stellantisECMP.pid_hvil_state == 0
                                     ? "OK"
                                     : String(datalayer_extended.stellantisECMP.pid_hvil_state))));
    s.fields.push_back(kv("BMS State",
                          datalayer_extended.stellantisECMP.pid_bms_state == 255
                              ? "N/A"
                              : (datalayer_extended.stellantisECMP.pid_bms_state == 0
                                     ? "OK"
                                     : String(datalayer_extended.stellantisECMP.pid_bms_state))));
    s.fields.push_back(kv("Vehicle speed",
                          datalayer_extended.stellantisECMP.pid_vehicle_speed == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_vehicle_speed),
                          "km/h"));
    s.fields.push_back(kv("Time spent over 55c",
                          datalayer_extended.stellantisECMP.pid_time_spent_over_55c == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_time_spent_over_55c),
                          "minutes"));
    s.fields.push_back(kv("Contactor lifetime closing counter",
                          datalayer_extended.stellantisECMP.pid_contactor_closing_counter == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_contactor_closing_counter),
                          "cycles"));
    s.fields.push_back(kv("State of Health Cell-1",
                          datalayer_extended.stellantisECMP.pid_SOH_cell_1 == 255
                              ? "N/A"
                              : String(datalayer_extended.stellantisECMP.pid_SOH_cell_1)));

    // Alert fields render after the MysteryVan section (when present), so they belong to
    // whichever section is current at that point.
    auto push_alert_fields = [](AdvancedSection& section) {
      section.fields.push_back(kv("Alert Battery", datalayer_extended.stellantisECMP.ALERT_BATT ? "Yes" : "No"));
      section.fields.push_back(kv("Alert Low SOC", datalayer_extended.stellantisECMP.ALERT_LOW_SOC ? "Yes" : "No"));
      section.fields.push_back(kv("Alert High SOC", datalayer_extended.stellantisECMP.ALERT_HIGH_SOC ? "Yes" : "No"));
      section.fields.push_back(kv("Alert SOC Jump", datalayer_extended.stellantisECMP.ALERT_SOC_JUMP ? "Yes" : "No"));
      section.fields.push_back(
          kv("Alert Overcharge", datalayer_extended.stellantisECMP.ALERT_OVERCHARGE ? "Yes" : "No"));
      section.fields.push_back(
          kv("Alert Temp Diff", datalayer_extended.stellantisECMP.ALERT_TEMP_DIFF ? "Yes" : "No"));
      section.fields.push_back(
          kv("Alert Temp High", datalayer_extended.stellantisECMP.ALERT_HIGH_TEMP ? "Yes" : "No"));
      section.fields.push_back(
          kv("Alert Overvoltage", datalayer_extended.stellantisECMP.ALERT_OVERVOLTAGE ? "Yes" : "No"));
      section.fields.push_back(
          kv("Alert Cell Overvoltage", datalayer_extended.stellantisECMP.ALERT_CELL_OVERVOLTAGE ? "Yes" : "No"));
      section.fields.push_back(
          kv("Alert Cell Undervoltage", datalayer_extended.stellantisECMP.ALERT_CELL_UNDERVOLTAGE ? "Yes" : "No"));
      section.fields.push_back(
          kv("Alert Cell Poor Consistency", datalayer_extended.stellantisECMP.ALERT_CELL_POOR_CONSIST ? "Yes" : "No"));
    };

    if (datalayer_extended.stellantisECMP.MysteryVan) {
      status.sections.push_back(s);

      AdvancedSection mystery;
      mystery.title = "MysteryVan platform detected!";

      String contactor_state;
      if (datalayer_extended.stellantisECMP.CONTACTORS_STATE == 0) {
        contactor_state = "Open";
      } else if (datalayer_extended.stellantisECMP.CONTACTORS_STATE == 1) {
        contactor_state = "Precharge";
      } else if (datalayer_extended.stellantisECMP.CONTACTORS_STATE == 2) {
        contactor_state = "Closed";
      }
      mystery.fields.push_back(kv("Contactor State", contactor_state));

      mystery.fields.push_back(kv("Crash Memorized", datalayer_extended.stellantisECMP.CrashMemorized ? "Yes" : "No"));

      String opening_reason_mv;
      if (datalayer_extended.stellantisECMP.CONTACTOR_OPENING_REASON == 0) {
        opening_reason_mv = "No error";
      } else if (datalayer_extended.stellantisECMP.CONTACTOR_OPENING_REASON == 1) {
        opening_reason_mv = "Crash!";
      } else if (datalayer_extended.stellantisECMP.CONTACTOR_OPENING_REASON == 2) {
        opening_reason_mv = "12V supply source undervoltage";
      } else if (datalayer_extended.stellantisECMP.CONTACTOR_OPENING_REASON == 3) {
        opening_reason_mv = "12V supply source overvoltage";
      } else if (datalayer_extended.stellantisECMP.CONTACTOR_OPENING_REASON == 4) {
        opening_reason_mv = "Battery temperature";
      } else if (datalayer_extended.stellantisECMP.CONTACTOR_OPENING_REASON == 5) {
        opening_reason_mv = "Interlock line open";
      } else if (datalayer_extended.stellantisECMP.CONTACTOR_OPENING_REASON == 6) {
        opening_reason_mv = "e-Service plug disconnected";
      }
      mystery.fields.push_back(kv("Contactor Opening Reason", opening_reason_mv));

      String fault_type;
      if (datalayer_extended.stellantisECMP.TBMU_FAULT_TYPE == 0) {
        fault_type = "No fault";
      } else if (datalayer_extended.stellantisECMP.TBMU_FAULT_TYPE == 1) {
        fault_type = "FirstLevelFault: Warning Lamp";
      } else if (datalayer_extended.stellantisECMP.TBMU_FAULT_TYPE == 2) {
        fault_type = "SecondLevelFault: Stop Lamp";
      } else if (datalayer_extended.stellantisECMP.TBMU_FAULT_TYPE == 3) {
        fault_type = "ThirdLevelFault: Stop Lamp + contactor opening (EPS shutdown)";
      } else if (datalayer_extended.stellantisECMP.TBMU_FAULT_TYPE == 4) {
        fault_type = "FourthLevelFault: Stop Lamp + Active Discharge";
      } else if (datalayer_extended.stellantisECMP.TBMU_FAULT_TYPE == 5) {
        fault_type = "Inhibition of powertrain activation";
      } else if (datalayer_extended.stellantisECMP.TBMU_FAULT_TYPE == 6) {
        fault_type = "Reserved";
      }
      mystery.fields.push_back(kv("Battery fault type", fault_type));

      mystery.fields.push_back(
          kv("FC insulation minus resistance", String(datalayer_extended.stellantisECMP.HV_BATT_FC_INSU_MINUS_RES), "kOhm"));
      mystery.fields.push_back(
          kv("FC insulation plus resistance", String(datalayer_extended.stellantisECMP.HV_BATT_FC_INSU_PLUS_RES), "kOhm"));
      mystery.fields.push_back(kv("FC vehicle insulation plus resistance",
                                  String(datalayer_extended.stellantisECMP.HV_BATT_FC_VHL_INSU_PLUS_RES), "kOhm"));
      mystery.fields.push_back(kv("FC vehicle insulation plus resistance",
                                  String(datalayer_extended.stellantisECMP.HV_BATT_ONLY_INSU_MINUS_RES), "kOhm"));

      push_alert_fields(mystery);
      status.sections.push_back(mystery);
    } else {
      push_alert_fields(s);
      status.sections.push_back(s);
    }

    return status;
  }

 private:
  void reset_DTC() { UserRequestDTCreset = true; }
  void reset_crash() { UserRequestCollisionReset = true; }
  void reset_contactor() { UserRequestContactorReset = true; }
  void clear_isolation() { UserRequestIsolationReset = true; }
  void set_factory_mode() { UserRequestDisableIsoMonitoring = true; }

  std::vector<BatteryCommand> commands_ = {
      command(CMD_RESET_DTC, [this] { reset_DTC(); }),
      command(CMD_RESET_CRASH, [this] { reset_crash(); }),
      command(CMD_RESET_CONTACTOR, [this] { reset_contactor(); }),
      command(CMD_CLEAR_ISOLATION, [this] { clear_isolation(); }),
      command(CMD_SET_FACTORY_MODE, [this] { set_factory_mode(); }),
  };

  DATALAYER_BATTERY_TYPE* datalayer_battery;
  bool* allows_contactor_closing;
  DATALAYER_INFO_ECMP* datalayer_ecmp;
  static const int MAX_PACK_VOLTAGE_DV = 4546;
  static const int MIN_PACK_VOLTAGE_DV = 3580;
  static const int MAX_CELL_DEVIATION_MV = 100;
  static const int MAX_CELL_VOLTAGE_MV = 4250;
  static const int MIN_CELL_VOLTAGE_MV = 3280;

  unsigned long previousMillis10 = 0;    //- will store last time a 10ms CAN Message was sent
  unsigned long previousMillis20 = 0;    // will store last time a 20ms CAN Message was sent
  unsigned long previousMillis50 = 0;    // will store last time a 50ms CAN Message was sent
  unsigned long previousMillis100 = 0;   // will store last time a 100ms CAN Message was sent
  unsigned long previousMillis250 = 0;   // will store last time a 250ms CAN Message was sent
  unsigned long previousMillis500 = 0;   // will store last time a 500ms CAN Message was sent
  unsigned long previousMillis1000 = 0;  // will store last time a 1000ms CAN Message was sent
  unsigned long previousMillis5000 = 0;  // will store last time a 1000ms CAN Message was sent
  CAN_frame ECMP_010 = {.FD = false, .ext_ID = false, .DLC = 1, .ID = 0x010, .data = {0xB4}};  //VCU_BCM_Crash 100ms
  CAN_frame ECMP_0F0 = {.FD = false,  //VCU2_0F0 (Common) 20ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,
                        .DLC = 8,
                        .ID = 0x0F0,
                        .data = {0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}};
  CAN_frame ECMP_0F2 = {.FD = false,      //CtrlMCU1_0F2 10ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  // NOTE. Changes on BMS state
                        .DLC = 8,
                        .ID = 0x0F2,
                        .data = {0x7D, 0x00, 0x4E, 0x20, 0x00, 0x00, 0x60, 0x0D}};

  CAN_frame ECMP_110 = {.FD = false,      //??? 10ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  // NOTE. Changes on BMS state
                        .DLC = 8,
                        .ID = 0x110,
                        .data = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x87, 0x05}};
  static constexpr CAN_frame ECMP_111 = {.FD = false,      //??? 10ms periodic (Perfectly emulated in Battery-Emulator)
                                         .ext_ID = false,  //Same content always, fully static
                                         .DLC = 8,
                                         .ID = 0x111,
                                         .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame ECMP_112 = {.FD = false,      //MCU1_112 10ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  //Same content always, only CRC changes at end
                        .DLC = 8,
                        .ID = 0x112,
                        .data = {0x4E, 0x20, 0x00, 0x0F, 0xA0, 0x7D, 0x00, 0x0A}};
  static constexpr CAN_frame ECMP_114 = {.FD = false,      //??? 10ms periodic (Perfectly emulated in Battery-Emulator)
                                         .ext_ID = false,  //Same content always, fully static
                                         .DLC = 8,
                                         .ID = 0x114,
                                         .data = {0x00, 0x00, 0x00, 0x7D, 0x07, 0xD0, 0x7D, 0x00}};
  static constexpr CAN_frame ECMP_0C5 = {.FD = false,  //DC2_0C5 10ms periodic (Perfectly emulated in Battery-Emulator)
                                         .ext_ID = false,  //Same content always, fully static
                                         .DLC = 8,
                                         .ID = 0x0C5,
                                         .data = {0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame ECMP_17B = {.FD = false,      //VCU_PCANInfo_17B 10ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  // NOTE. Changes on BMS state
                        .DLC = 8,
                        .ID = 0x17B,
                        .data = {0x00, 0x00, 0x00, 0x7E, 0x78, 0x00, 0x00, 0x0F}};  // NOTE. Changes on BMS state
  static constexpr CAN_frame ECMP_230 = {.FD = false,  //OBC3_230 50ms periodic (Perfectly emulated in Battery-Emulator)
                                         .ext_ID = false,  //Same content always, fully static
                                         .DLC = 8,
                                         .ID = 0x230,
                                         .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame ECMP_27A = {
      .FD = false,      //VCU_BSI_Wakeup_27A message 50ms periodic (Perfectly emulated in Battery-Emulator)
      .ext_ID = false,  // NOTE. Changes on BMS state
      .DLC = 8,         //Contains SEV main state, position of the BSI shunt park, ACC status
      .ID = 0x27A,      // electric network state, powetrain status, Wakeups, diagmux, APC activation
      .data = {0x4F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame ECMP_3D0 = {.FD = false,      //Not in logs, but makes speed go to 0km/h in diag tool when we send this
                        .ext_ID = false,  //Only sent in idle state
                        .DLC = 8,
                        .ID = 0x3D0,
                        .data = {0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame ECMP_345 = {.FD = false,      //DC1_345 100ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  // NOTE. Changes on BMS state
                        .DLC = 8,
                        .ID = 0x345,
                        .data = {0x45, 0x57, 0x00, 0x04, 0x00, 0x00, 0x06, 0x31}};
  static constexpr CAN_frame ECMP_382 = {
      //BSIInfo_382 (VCU) PSA specific 100ms periodic (Perfectly emulated in Battery-Emulator)
      .FD = false,  //Same content always, fully static
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x382,  //Frame1 has rollerbenchmode request, frame2 has generic powertrain cycle sync status
      .data = {0x02, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame ECMP_3A2 = {.FD = false,      //OBC2_3A2 100ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  // NOTE. Changes on BMS state
                        .DLC = 8,
                        .ID = 0x3A2,
                        .data = {0x03, 0xE8, 0x00, 0x00, 0x81, 0x00, 0x08, 0x02}};
  CAN_frame ECMP_3A3 = {.FD = false,      //OBC1_3A3 100ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  // NOTE. Changes on BMS state
                        .DLC = 8,
                        .ID = 0x3A3,
                        .data = {0x4A, 0x4A, 0x40, 0x00, 0x00, 0x08, 0x00, 0x0F}};
  static constexpr CAN_frame ECMP_439 = {.FD = false,      //OBC4 1s periodic (Perfectly emulated in Battery-Emulator)
                                         .ext_ID = false,  //Same content always, fully static
                                         .DLC = 8,
                                         .ID = 0x439,
                                         .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

  CAN_frame ECMP_552 = {.FD = false,  //VCU_552 1s periodic (Perfectly handled in Battery-Emulator)
                        .ext_ID = false,
                        .DLC = 8,     //552 seems to be tracking time in byte 0-3
                        .ID = 0x552,  // distance in km in byte 4-6, temporal reset counter in byte 7
                        .data = {0x00, 0x02, 0x95, 0x6D, 0x00, 0xD7, 0xB5, 0xFE}};

  CAN_frame ECMP_POLL = {.FD = false, .ext_ID = false, .DLC = 4, .ID = 0x6B4, .data = {0x03, 0x22, 0xD8, 0x66}};
  static constexpr CAN_frame ECMP_ACK = {.FD = false,  //Ack frame
                                         .ext_ID = false,
                                         .DLC = 3,
                                         .ID = 0x6B4,
                                         .data = {0x30, 0x00, 0x00}};
  static constexpr CAN_frame ECMP_DIAG_START = {.FD = false,
                                                .ext_ID = false,
                                                .DLC = 3,
                                                .ID = 0x6B4,
                                                .data = {0x02, 0x10, 0x03}};
  //Start diagnostic session (extended diagnostic session, mode 0x10 with sub-mode 0x03)
  static constexpr CAN_frame ECMP_CONTACTOR_RESET_START = {.FD = false,
                                                           .ext_ID = false,
                                                           .DLC = 5,
                                                           .ID = 0x6B4,
                                                           .data = {0x04, 0x31, 0x01, 0xDD, 0x35}};
  static constexpr CAN_frame ECMP_CONTACTOR_RESET_PROGRESS = {.FD = false,
                                                              .ext_ID = false,
                                                              .DLC = 5,
                                                              .ID = 0x6B4,
                                                              .data = {0x04, 0x31, 0x03, 0xDD, 0x35}};
  static constexpr CAN_frame ECMP_COLLISION_RESET_START = {.FD = false,
                                                           .ext_ID = false,
                                                           .DLC = 5,
                                                           .ID = 0x6B4,
                                                           .data = {0x04, 0x31, 0x01, 0xDF, 0x60}};
  static constexpr CAN_frame ECMP_COLLISION_RESET_PROGRESS = {.FD = false,
                                                              .ext_ID = false,
                                                              .DLC = 5,
                                                              .ID = 0x6B4,
                                                              .data = {0x04, 0x31, 0x03, 0xDF, 0x60}};
  static constexpr CAN_frame ECMP_ISOLATION_RESET_START = {.FD = false,
                                                           .ext_ID = false,
                                                           .DLC = 5,
                                                           .ID = 0x6B4,
                                                           .data = {0x04, 0x31, 0x01, 0xDF, 0x46}};
  static constexpr CAN_frame ECMP_ISOLATION_RESET_PROGRESS = {.FD = false,
                                                              .ext_ID = false,
                                                              .DLC = 8,
                                                              .ID = 0x6B4,
                                                              .data = {0x04, 0x31, 0x03, 0xDF, 0x46}};
  static constexpr CAN_frame ECMP_RESET_DONE = {.FD = false,
                                                .ext_ID = false,
                                                .DLC = 3,
                                                .ID = 0x6B4,
                                                .data = {0x02, 0x3E, 0x00}};
  static constexpr CAN_frame ECMP_FACTORY_MODE_ACTIVATION = {.FD = false,
                                                             .ext_ID = false,
                                                             .DLC = 5,
                                                             .ID = 0x6B4,
                                                             .data = {0x04, 0x2E, 0xD9, 0x00, 0x01}};
  static constexpr CAN_frame ECMP_DISABLE_ISOLATION_REQ = {.FD = false,
                                                           .ext_ID = false,
                                                           .DLC = 5,
                                                           .ID = 0x6B4,
                                                           .data = {0x04, 0x31, 0x02, 0xDF, 0xE1}};
  static constexpr CAN_frame ECMP_ACK_MESSAGE = {.FD = false,
                                                 .ext_ID = false,
                                                 .DLC = 3,
                                                 .ID = 0x6B4,
                                                 .data = {0x02, 0x3E, 0x00}};
  static constexpr CAN_frame ECMP_CLEAR_DTC = {.FD = false,
                                               .ext_ID = false,
                                               .DLC = 5,
                                               .ID = 0x6B4,
                                               .data = {0x04, 0x14, 0xFF, 0xFF, 0xFF}};

#ifdef SIMULATE_ENTIRE_VEHICLE_ECMP
  static constexpr CAN_frame ECMP_0AE = {.FD = false,
                                         .ext_ID = false,
                                         .DLC = 5,
                                         .ID = 0x0AE,
                                         .data = {0x04, 0x77, 0x7A, 0x5E, 0xDF}};
  static constexpr CAN_frame ECMP_041 = {.FD = false, .ext_ID = false, .DLC = 1, .ID = 0x041, .data = {0x00}};
  CAN_frame ECMP_486 = {.FD = false,      //??? 1s periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  // NOTE. Changes on BMS state
                        .DLC = 8,
                        .ID = 0x486,
                        .data = {0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}};
  static constexpr CAN_frame ECMP_786 = {.FD = false,      //1s periodic
                                         .ext_ID = false,  //Always static in HV mode
                                         .DLC = 8,
                                         .ID = 0x786,
                                         .data = {0x38, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}};
  static constexpr CAN_frame ECMP_591 = {.FD = false,      //1s periodic
                                         .ext_ID = false,  //Always static in HV mode
                                         .DLC = 8,
                                         .ID = 0x591,
                                         .data = {0x38, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}};
  CAN_frame ECMP_794 = {.FD = false,  //Unsure who sends this. Could it be BMU?
                        .ext_ID = false,
                        .DLC = 8,
                        .ID = 0x794,
                        .data = {0xB8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}};  // NOTE. Changes on BMS state
  static constexpr CAN_frame ECMP_55F = {.FD = false,      //5s periodic (Perfectly emulated in Battery-Emulator)
                                         .ext_ID = false,  //Same content always, fully static
                                         .DLC = 1,
                                         .ID = 0x55F,
                                         .data = {0x82}};
  CAN_frame ECMP_31D = {.FD = false,      //??? 100ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  //Same content always, fully static
                        .DLC = 8,
                        .ID = 0x31D,
                        .data = {0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42}};
  CAN_frame ECMP_351 = {.FD = false,      //??? 100ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  // NOTE. Changes on BMS state
                        .DLC = 8,
                        .ID = 0x351,
                        .data = {0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x0E}};
  CAN_frame ECMP_372 = {.FD = false,      //??? 100ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  // NOTE. Changes on BMS state
                        .DLC = 8,
                        .ID = 0x372,
                        .data = {0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};  // NOTE. Changes on BMS state
  static constexpr CAN_frame ECMP_37F = {
      .FD = false,      //??? 100ms periodic (Perfectly emulated in Battery-Emulator)
      .ext_ID = false,  // Seems to be a bunch of temperature measurements? Static for now
      .DLC = 8,
      .ID = 0x37F,
      .data = {0x45, 0x49, 0x51, 0x45, 0x45, 0x00, 0x45, 0x45}};
  static constexpr CAN_frame ECMP_0A6 = {
      .FD = false,
      .ext_ID = false,
      .DLC = 2,
      .ID = 0x0A6,
      .data = {0x02, 0x00}};              //Content changes after 12minutes of runtime (not emulated)
  CAN_frame ECMP_383 = {.FD = false,      //??? 100ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  // NOTE. Changes on BMS state
                        .DLC = 8,
                        .ID = 0x383,
                        .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame ECMP_31E = {.FD = false,      //??? 100ms periodic (Perfectly emulated in Battery-Emulator)
                        .ext_ID = false,  // NOTE. Changes on BMS state
                        .DLC = 8,
                        .ID = 0x31E,
                        .data = {0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08}};
#endif

  uint32_t ticks_552 = 0x0BC8CFC6;
  uint32_t pid_insulation_res_neg = NOT_SAMPLED_YET;
  uint32_t pid_insulation_res_pos = NOT_SAMPLED_YET;
  uint32_t pid_max_current_10s = NOT_SAMPLED_YET;
  uint32_t pid_max_discharge_10s = NOT_SAMPLED_YET;
  uint32_t pid_max_discharge_30s = NOT_SAMPLED_YET;
  uint32_t pid_max_charge_10s = NOT_SAMPLED_YET;
  uint32_t pid_max_charge_30s = NOT_SAMPLED_YET;
  uint32_t pid_energy_capacity = NOT_SAMPLED_YET;
  uint32_t pid_insulation_res = NOT_SAMPLED_YET;
  uint32_t pid_crash_counter = NOT_SAMPLED_YET;
  uint32_t pid_history_data = NOT_SAMPLED_YET;
  uint32_t pid_last_can_failure_detail = NOT_SAMPLED_YET;
  uint32_t pid_hw_version_num = NOT_SAMPLED_YET;
  uint32_t pid_sw_version_num = NOT_SAMPLED_YET;
  uint32_t pid_vehicle_speed = NOT_SAMPLED_YET;
  uint32_t pid_time_spent_over_55c = NOT_SAMPLED_YET;
  uint32_t pid_contactor_closing_counter = NOT_SAMPLED_YET;
  uint32_t pid_date_of_manufacture = NOT_SAMPLED_YET;
  uint32_t pid_current_time = NOT_SAMPLED_YET;
  uint32_t pid_time_sent_by_car = NOT_SAMPLED_YET;

  int32_t pid_current = NOT_SAMPLED_YET;

  static const uint8_t NOT_SAMPLED_YET = 255;
  static const uint8_t COMPLETED_STATE = 0;
  static const uint16_t PID_WELD_CHECK = 0xD814;
  static const uint16_t PID_CONT_REASON_OPEN = 0xD812;
  static const uint16_t PID_CONTACTOR_STATUS = 0xD813;
  static const uint16_t PID_NEG_CONT_CONTROL = 0xD44F;
  static const uint16_t PID_NEG_CONT_STATUS = 0xD453;
  static const uint16_t PID_POS_CONT_CONTROL = 0xD44E;
  static const uint16_t PID_POS_CONT_STATUS = 0xD452;
  static const uint16_t PID_CONTACTOR_NEGATIVE = 0xD44C;
  static const uint16_t PID_CONTACTOR_POSITIVE = 0xD44D;
  static const uint16_t PID_PRECHARGE_RELAY_CONTROL = 0xD44B;
  static const uint16_t PID_PRECHARGE_RELAY_STATUS = 0xD451;
  static const uint16_t PID_RECHARGE_STATUS = 0xD864;
  static const uint16_t PID_DELTA_TEMPERATURE = 0xD878;
  static const uint16_t PID_COLDEST_MODULE = 0xD446;
  static const uint16_t PID_LOWEST_TEMPERATURE = 0xD87D;
  static const uint16_t PID_AVERAGE_TEMPERATURE = 0xD877;
  static const uint16_t PID_HIGHEST_TEMPERATURE = 0xD817;
  static const uint16_t PID_HOTTEST_MODULE = 0xD445;
  static const uint16_t PID_AVG_CELL_VOLTAGE = 0xD43D;
  static const uint16_t PID_CURRENT = 0xD816;
  static const uint16_t PID_INSULATION_NEG = 0xD87C;
  static const uint16_t PID_INSULATION_POS = 0xD87B;
  static const uint16_t PID_MAX_CURRENT_10S = 0xD876;
  static const uint16_t PID_MAX_DISCHARGE_10S = 0xD873;
  static const uint16_t PID_MAX_DISCHARGE_30S = 0xD874;
  static const uint16_t PID_MAX_CHARGE_10S = 0xD871;
  static const uint16_t PID_MAX_CHARGE_30S = 0xD872;
  static const uint16_t PID_ENERGY_CAPACITY = 0xD860;
  static const uint16_t PID_HIGH_CELL_NUM = 0xD43B;
  static const uint16_t PID_LOW_CELL_NUM = 0xD43C;
  static const uint16_t PID_SUM_OF_CELLS = 0xD438;
  static const uint16_t PID_CELL_MIN_CAPACITY = 0xD413;
  static const uint16_t PID_CELL_VOLTAGE_MEAS_STATUS = 0xD48A;
  static const uint16_t PID_INSULATION_RES = 0xD47A;
  static const uint16_t PID_PACK_VOLTAGE = 0xD815;
  static const uint16_t PID_HIGH_CELL_VOLTAGE = 0xD870;
  static const uint16_t PID_ALL_CELL_VOLTAGES = 0xD440;  //Multi-frame
  static const uint16_t PID_LOW_CELL_VOLTAGE = 0xD86F;
  static const uint16_t PID_BATTERY_ENERGY = 0xD865;
  static const uint16_t PID_CELLBALANCE_STATUS = 0xD46F;      //Multi-frame?
  static const uint16_t PID_CELLBALANCE_HWERR_MASK = 0xD470;  //Multi-frame
  static const uint16_t PID_CRASH_COUNTER = 0xD42F;
  static const uint16_t PID_WIRE_CRASH = 0xD87F;
  static const uint16_t PID_CAN_CRASH = 0xD48D;
  static const uint16_t PID_HISTORY_DATA = 0xD465;
  static const uint16_t PID_LOWSOC_COUNTER = 0xD492;           //Not supported on all batteris
  static const uint16_t PID_LAST_CAN_FAILURE_DETAIL = 0xD89E;  //Not supported on all batteris
  static const uint16_t PID_HW_VERSION_NUM = 0xF193;           //Not supported on all batteris
  static const uint16_t PID_SW_VERSION_NUM = 0xF195;           //Not supported on all batteris
  static const uint16_t PID_FACTORY_MODE_CONTROL = 0xD900;
  static const uint16_t PID_BATTERY_SERIAL = 0xD901;
  static const uint16_t PID_ALL_CELL_SOH = 0xD4B5;
  static const uint16_t PID_AUX_FUSE_STATE = 0xD86C;
  static const uint16_t PID_BATTERY_STATE = 0xD811;
  static const uint16_t PID_PRECHARGE_SHORT_CIRCUIT = 0xD4D8;
  static const uint16_t PID_ESERVICE_PLUG_STATE = 0xD86A;
  static const uint16_t PID_MAINFUSE_STATE = 0xD86B;
  static const uint16_t PID_MOST_CRITICAL_FAULT = 0xD481;
  static const uint16_t PID_CURRENT_TIME = 0xD47F;
  static const uint16_t PID_TIME_SENT_BY_CAR = 0xD4CA;
  static const uint16_t PID_12V = 0xD822;
  static const uint16_t PID_12V_ABNORMAL = 0xD42B;
  static const uint16_t PID_HVIL_IN_VOLTAGE = 0xD46B;
  static const uint16_t PID_HVIL_OUT_VOLTAGE = 0xD46A;
  static const uint16_t PID_HVIL_STATE = 0xD869;
  static const uint16_t PID_BMS_STATE = 0xD45A;
  static const uint16_t PID_VEHICLE_SPEED = 0xD802;
  static const uint16_t PID_TIME_SPENT_OVER_55C = 0xE082;
  static const uint16_t PID_CONTACTOR_CLOSING_COUNTER = 0xD416;
  static const uint16_t PID_DATE_OF_MANUFACTURE = 0xF18B;
  uint16_t battery_voltage = 370;
  uint16_t battery_soc = 0;
  uint16_t cellvoltages[108];
  uint16_t battery_AllowedMaxChargeCurrent = 0;
  uint16_t battery_AllowedMaxDischargeCurrent = 0;
  uint16_t battery_insulationResistanceKOhm = 0;
  uint16_t pid_lowsoc_counter = NOT_SAMPLED_YET;
  uint16_t poll_state = PID_WELD_CHECK;
  uint16_t incoming_poll = 0;
  uint16_t pid_hvil_out_voltage = NOT_SAMPLED_YET;
  uint16_t pid_most_critical_fault = NOT_SAMPLED_YET;
  uint16_t pid_avg_cell_voltage = NOT_SAMPLED_YET;
  uint16_t pid_sum_of_cells = NOT_SAMPLED_YET;
  uint16_t pid_cell_min_capacity = NOT_SAMPLED_YET;
  uint16_t pid_pack_voltage = NOT_SAMPLED_YET;
  uint16_t pid_high_cell_voltage = NOT_SAMPLED_YET;
  uint16_t pid_low_cell_voltage = NOT_SAMPLED_YET;
  uint16_t pid_12v = 12345;  //Initialized to over 12V to not trigger low 12V event
  uint16_t pid_hvil_in_voltage = NOT_SAMPLED_YET;
  uint16_t pid_SOH_cell_1 = NOT_SAMPLED_YET;
  uint16_t SOE_MAX_CURRENT_TEMP = 0;
  uint16_t FRONT_MACHINE_POWER_LIMIT = 0;
  uint16_t REAR_MACHINE_POWER_LIMIT = 0;
  uint16_t EVSE_INSTANT_DC_HV_CURRENT = 0;
  uint16_t HV_BATT_SOE_HD = 0;
  uint16_t HV_BATT_SOE_MAX = 0;
  uint16_t HV_BATT_STABLE_CHARGE_POWER_HD = 0;
  uint16_t HV_BATT_STABLE_DISCH_POWER_HD = 0;
  uint16_t HV_BATT_NOMINAL_DISCH_POWER_HD = 0;
  uint16_t MAX_ALLOW_DISCHRG_CURRENT = 0;
  uint16_t HV_STORAGE_MAX_I = 0;
  uint16_t HV_BATT_PEAK_DISCH_POWER_HD = 0;
  uint16_t HV_BATT_PEAK_CH_POWER_HD = 0;
  uint16_t HV_BATT_NOM_CH_POWER_HD = 0;
  uint16_t MAX_ALLOW_CHRG_CURRENT = 0;
  uint16_t HV_BATT_FC_INSU_MINUS_RES, HV_BATT_FC_INSU_PLUS_RES, HV_BATT_FC_VHL_INSU_PLUS_RES,
      HV_BATT_ONLY_INSU_MINUS_RES = 0;
  uint16_t MIN_ALLOW_DISCHRG_VOLTAGE = 0;
  uint16_t HV_BATT_NOMINAL_DISCH_CURR_HD, HV_BATT_PEAK_DISCH_CURR_HD, HV_BATT_STABLE_DISCH_CURR_HD = 0;
  uint16_t HV_BATT_NOMINAL_CHARGE_CURR_HD, HV_BATT_PEAK_CHARGE_CURR_HD, HV_BATT_STABLE_CHARGE_CURR_HD = 0;
  uint16_t HV_BATT_REAL_VOLT_HD = 0;
  uint16_t HV_BATT_NOM_CH_VOLTAGE = 0;
  uint16_t HV_BATT_REAL_POWER_HD = 0;
  uint16_t MAX_ALLOW_CHRG_POWER = 0;
  uint16_t MAX_ALLOW_DISCHRG_POWER = 0;
  uint16_t HV_BATT_SOC = 0;
  uint16_t HV_BATT_GENERATED_HEAT_RATE = 0;
  uint16_t BMS_DC_RELAY_MES_EVSE_VOLTAGE = 0;
  uint16_t HV_BATT_COP_VOLTAGE = 0;

  int16_t HV_BATT_MAX_REAL_CURR = 0;
  int16_t HV_BATT_REAL_CURR_HD = 0;
  int16_t battery_current = 0;
  int16_t battery_highestTemperature = 0;
  int16_t battery_lowestTemperature = 0;
  int16_t HV_BATT_COP_CURRENT = 0;

  uint8_t ContactorResetStatemachine = 0;
  uint8_t CollisionResetStatemachine = 0;
  uint8_t IsolationResetStatemachine = 0;
  uint8_t DisableIsoMonitoringStatemachine = 0;
  uint8_t timeSpentDisableIsoMonitoring = 0;
  uint8_t timeSpentContactorReset = 0;
  uint8_t timeSpentCollisionReset = 0;
  uint8_t timeSpentIsolationReset = 0;
  uint8_t countIsolationReset = 0;
  uint8_t counter_10ms = 0;
  uint8_t counter_20ms = 0;
  uint8_t counter_50ms = 0;
  uint8_t counter_100ms = 0;
  uint8_t counter_010 = 0;
  uint8_t battery_MainConnectorState = 0;
  uint8_t battery_insulation_failure_diag = 0;
  uint8_t pid_welding_detection = NOT_SAMPLED_YET;
  uint8_t pid_reason_open = NOT_SAMPLED_YET;
  uint8_t pid_contactor_status = NOT_SAMPLED_YET;
  uint8_t pid_negative_contactor_control = NOT_SAMPLED_YET;
  uint8_t pid_negative_contactor_status = NOT_SAMPLED_YET;
  uint8_t pid_positive_contactor_control = NOT_SAMPLED_YET;
  uint8_t pid_positive_contactor_status = NOT_SAMPLED_YET;
  uint8_t pid_contactor_negative = NOT_SAMPLED_YET;
  uint8_t pid_contactor_positive = NOT_SAMPLED_YET;
  uint8_t pid_precharge_relay_control = NOT_SAMPLED_YET;
  uint8_t pid_precharge_relay_status = NOT_SAMPLED_YET;
  uint8_t pid_recharge_status = NOT_SAMPLED_YET;
  uint8_t pid_coldest_module = NOT_SAMPLED_YET;
  uint8_t pid_hottest_module = NOT_SAMPLED_YET;
  uint8_t pid_highest_cell_voltage_num = NOT_SAMPLED_YET;
  uint8_t pid_lowest_cell_voltage_num = NOT_SAMPLED_YET;
  uint8_t pid_cell_voltage_measurement_status = NOT_SAMPLED_YET;
  uint8_t pid_battery_energy = NOT_SAMPLED_YET;
  uint8_t pid_12v_abnormal = NOT_SAMPLED_YET;
  uint8_t pid_factory_mode_control = NOT_SAMPLED_YET;
  uint8_t pid_battery_serial[14] = {0};
  uint8_t pid_aux_fuse_state = NOT_SAMPLED_YET;
  uint8_t pid_battery_state = NOT_SAMPLED_YET;
  uint8_t pid_precharge_short_circuit = NOT_SAMPLED_YET;
  uint8_t pid_eservice_plug_state = NOT_SAMPLED_YET;
  uint8_t pid_mainfuse_state = NOT_SAMPLED_YET;
  uint8_t pid_hvil_state = NOT_SAMPLED_YET;
  uint8_t pid_bms_state = NOT_SAMPLED_YET;
  uint8_t pid_wire_crash = NOT_SAMPLED_YET;
  uint8_t pid_CAN_crash = NOT_SAMPLED_YET;
  uint8_t EVSE_STATE = 0;
  uint8_t FAST_CHARGE_CONTACTOR_STATE = 0;
  uint8_t BMS_FASTCHARGE_STATUS = 0;
  uint8_t HV_BATT_NOM_CH_CURRENT = 0;
  uint8_t TBMU_FAULT_TYPE = 0;
  uint8_t CONTACTORS_STATE = 0;
  uint8_t NUMBER_PROBE_TEMP_MAX, NUMBER_PROBE_TEMP_MIN = 0;
  uint8_t NUMBER_OF_TEMPERATURE_SENSORS_IN_BATTERY = 0;
  uint8_t NUMBER_OF_CELL_MEASUREMENTS_IN_BATTERY = 0;
  uint8_t CONTACTOR_OPENING_REASON = 0;

  int8_t pid_delta_temperature = 127;
  int8_t pid_lowest_temperature = 127;
  int8_t pid_average_temperature = 127;
  int8_t pid_highest_temperature = 127;
  int8_t BMS_PROBETEMP[7] = {0};
  int8_t TEMPERATURE_MINIMUM_C = 0;

  bool HighPrecisionCurrentSampling = 1;
  bool CMD_RESET_MIL = false;
  bool REQ_BLINK_STOP_AND_SERVICE_LAMP = false;
  bool REQ_MIL_LAMP_CONTINOUS = false;
  bool HV_BATT_CRASH_MEMORIZED = false;
  bool HV_BATT_COLD_CRANK_ACK = false;
  bool HV_BATT_CHARGE_NEEDED_STATE = false;
  bool RC01_PERM_SYNTH_TBMU = false;
  bool battery_RelayOpenRequest = false;
  bool battery_InterlockOpen = false;
  bool MysteryVan = false;
  bool TBCU_48V_WAKEUP = false;
  bool REQ_CLEAR_DTC_TBMU = false;
  bool HV_BATT_DISCONT_WARNING_OPEN = false;
  bool ALERT_CELL_POOR_CONSIST, ALERT_OVERCHARGE, ALERT_BATT, ALERT_LOW_SOC, ALERT_HIGH_SOC, ALERT_SOC_JUMP,
      ALERT_TEMP_DIFF, ALERT_HIGH_TEMP, ALERT_OVERVOLTAGE, ALERT_CELL_OVERVOLTAGE, ALERT_CELL_UNDERVOLTAGE = false;

  bool UserRequestDTCreset = false;
  bool UserRequestContactorReset = false;
  bool UserRequestCollisionReset = false;
  bool UserRequestIsolationReset = false;
  bool UserRequestDisableIsoMonitoring = false;

  uint8_t data_010_CRC[8] = {0xB4, 0x96, 0x78, 0x5A, 0x3C, 0x1E, 0xF0, 0xD2};
  uint8_t data_3A2_CRC[16] = {0x0C, 0x1B, 0x2A, 0x39, 0x48, 0x57,
                              0x66, 0x75, 0x84, 0x93, 0xA2, 0xB1};                 // NOTE. Changes on BMS state
  uint8_t data_345_content[16] = {0x04, 0xF5, 0xE6, 0xD7, 0xC8, 0xB9, 0xAA, 0x9B,  // NOTE. Changes on BMS state
                                  0x8C, 0x7D, 0x6E, 0x5F, 0x40, 0x31, 0x22, 0x13};
};
#endif
