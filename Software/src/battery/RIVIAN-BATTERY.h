#ifndef RIVIAN_BATTERY_H
#define RIVIAN_BATTERY_H
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "BatterySlotContext.h"
#include "CanBattery.h"

class RivianBattery : public CanBattery {
 public:
  RivianBattery(const BatterySlotContext& ctx) : CanBattery(ctx.can_interface) { datalayer_battery = ctx.datalayer; }
  virtual void setup(void);
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);

  BatteryAdvancedStatus get_advanced_status() override {
    BatteryAdvancedStatus status;
    AdvancedSection s;

    s.fields.push_back(kv("Voltage, pre contactors", String(datalayer_extended.rivian.pre_contactor_voltage), "dV"));
    s.fields.push_back(kv("Voltage, main contactors", String(datalayer_extended.rivian.main_contactor_voltage), "dV"));
    s.fields.push_back(kv("Voltage, reference", String(datalayer_extended.rivian.voltage_reference), "dV"));
    s.fields.push_back(
        kv("Voltage, DCFC contactors", String(datalayer_extended.rivian.DCFC_contactor_voltage), "dV"));

    s.fields.push_back(kv("SOC, max", String(datalayer_extended.rivian.battery_SOC_max), "pptt"));
    s.fields.push_back(kv("SOC, min", String(datalayer_extended.rivian.battery_SOC_min), "pptt"));

    if (datalayer_extended.rivian.NACS_charger_detected) {
      s.fields.push_back(kv("NACS charger detected", "yes"));
    }

    s.fields.push_back(
        kv("Isolation measurement ongoing", datalayer_extended.rivian.IsolationMeasurementOngoing ? "Yes" : "No"));

    String isolation_status;
    if (datalayer_extended.rivian.isolation_fault_status == 0) {
      isolation_status = "Undefined";
    } else if (datalayer_extended.rivian.isolation_fault_status == 1) {
      isolation_status = "Stable";
    } else if (datalayer_extended.rivian.isolation_fault_status == 2) {
      isolation_status = "No Fault";
    } else if (datalayer_extended.rivian.isolation_fault_status == 3) {
      isolation_status = "High Side Fault";
    } else if (datalayer_extended.rivian.isolation_fault_status == 4) {
      isolation_status = "Low Side Fault";
    } else if (datalayer_extended.rivian.isolation_fault_status == 5) {
      isolation_status = "Dual Side Fault";
    } else if (datalayer_extended.rivian.isolation_fault_status == 6) {
      isolation_status = "Iso Circuit Failure";
    } else if (datalayer_extended.rivian.isolation_fault_status == 7) {
      isolation_status = "Iso Circuit Check timeout";
    }
    s.fields.push_back(kv("Isolation Status", isolation_status));

    String interlock_status;
    if (datalayer_extended.rivian.HVIL == 0) {
      interlock_status = "NOT OK";
    } else if (datalayer_extended.rivian.HVIL == 1) {
      interlock_status = "NOT OK";
    } else if (datalayer_extended.rivian.HVIL == 2) {
      interlock_status = "NOT OK";
    } else if (datalayer_extended.rivian.HVIL == 3) {
      interlock_status = "OK";
    }
    s.fields.push_back(kv("Interlock status", interlock_status));

    String bms_state;
    if (datalayer_extended.rivian.BMS_state == 0) {
      bms_state = "Sleep";
    } else if (datalayer_extended.rivian.BMS_state == 1) {
      bms_state = "Standby";
    } else if (datalayer_extended.rivian.BMS_state == 2) {
      bms_state = "Ready";
    } else if (datalayer_extended.rivian.BMS_state == 3) {
      bms_state = "Go";
    }
    s.fields.push_back(kv("BMS State", bms_state));

    String contactor_state;
    if (datalayer_extended.rivian.contactor_state == 0) {
      contactor_state = "Open";
    } else if (datalayer_extended.rivian.contactor_state == 1) {
      contactor_state = "Closed";
    } else if (datalayer_extended.rivian.contactor_state == 2) {
      contactor_state = "Precharge";
    } else if (datalayer_extended.rivian.contactor_state == 3) {
      contactor_state = "Turning off";
    } else if (datalayer_extended.rivian.contactor_state == 4) {
      contactor_state = "Initialization";
    } else if (datalayer_extended.rivian.contactor_state == 5) {
      contactor_state = "FAILURE";
    }
    s.fields.push_back(kv("Contactor State", contactor_state));

    status.sections.push_back(s);

    AdvancedSection errors;
    errors.title = "Active errors and faults";

    if (datalayer_extended.rivian.error_relay_open) {
      errors.fields.push_back(kv("Error Relay Open", "yes"));
    }
    if (datalayer_extended.rivian.error_flags_from_BMS & 0x01) {
      errors.fields.push_back(kv("Error Isolation Single", "yes"));
    }
    if ((datalayer_extended.rivian.error_flags_from_BMS & 0x02) >> 1) {
      errors.fields.push_back(kv("Error Isolation Double", "yes"));
    }
    if ((datalayer_extended.rivian.error_flags_from_BMS & 0x04) >> 2) {
      errors.fields.push_back(kv("Error Emergency Off CRASH", "yes"));
    }
    if ((datalayer_extended.rivian.error_flags_from_BMS & 0x08) >> 3) {
      errors.fields.push_back(kv("Error Emergency Off Pilot", "yes"));
    }
    if ((datalayer_extended.rivian.error_flags_from_BMS & 0x10) >> 4) {
      errors.fields.push_back(kv("Error Emergency Off Request", "yes"));
    }
    if ((datalayer_extended.rivian.error_flags_from_BMS & 0x20) >> 5) {
      errors.fields.push_back(kv("Error Emergency Off", "yes"));
    }
    if ((datalayer_extended.rivian.error_flags_from_BMS & 0x40) >> 6) {
      errors.fields.push_back(kv("Error Contactors Welded", "yes"));
    }
    if ((datalayer_extended.rivian.error_flags_from_BMS & 0x80) >> 7) {
      errors.fields.push_back(kv("Error Limited Power", "yes"));
    }

    //HMI errors / status codes, bundle them also under errors

    if ((datalayer_extended.rivian.HMI_part1 & 0x10) >> 4) {
      errors.fields.push_back(kv("Vehicle Battery Issue", "yes"));
    }
    if ((datalayer_extended.rivian.HMI_part1 & 0x20) >> 5) {
      errors.fields.push_back(kv("Critical Battery Issue", "yes"));
    }
    if ((datalayer_extended.rivian.HMI_part1 & 0x40) >> 6) {
      errors.fields.push_back(kv("AC performance limited", "yes"));
    }
    if ((datalayer_extended.rivian.HMI_part1 & 0x80) >> 7) {
      errors.fields.push_back(kv("DC performance limited", "yes"));
    }
    if (datalayer_extended.rivian.HMI_part2 & 0x01) {
      errors.fields.push_back(kv("DC charging disabled", "yes"));
    }
    if ((datalayer_extended.rivian.HMI_part2 & 0x02) >> 1) {
      errors.fields.push_back(kv("Electric hazard", "yes"));
    }
    if ((datalayer_extended.rivian.HMI_part2 & 0x04) >> 2) {
      errors.fields.push_back(kv("Fire risk", "yes"));
    }
    if ((datalayer_extended.rivian.HMI_part2 & 0x08) >> 3) {
      errors.fields.push_back(kv("Vehicle system fault", "yes"));
    }
    if ((datalayer_extended.rivian.HMI_part2 & 0x10) >> 4) {
      errors.fields.push_back(kv("Battery electric malfunction", "yes"));
    }

    //Misc
    if (datalayer_extended.rivian.system_safe_state > 1) {
      errors.fields.push_back(kv("System safe state A active", "yes"));
    }
    if (datalayer_extended.rivian.puncture_fault) {
      errors.fields.push_back(kv("Puncture fault detected", "yes"));
    }
    if (datalayer_extended.rivian.liquid_fault) {
      errors.fields.push_back(kv("Liquid fault detected", "yes"));
    }
    if (datalayer_extended.rivian.contactor_DCFC_welded) {
      errors.fields.push_back(kv("DCFC contactor welded", "yes"));
    }

    if (datalayer_extended.rivian.slewrate_potential_violation) {
      errors.fields.push_back(kv("Slewrate potential violation", "yes"));
    }
    if (datalayer_extended.rivian.minimum_power_potential_violation) {
      errors.fields.push_back(kv("Min power potential violation", "yes"));
    }
    if (datalayer_extended.rivian.operation_limit_violation_warning) {
      errors.fields.push_back(kv("Operation limit violation warning", "yes"));
    }

    status.sections.push_back(errors);
    return status;
  }

 private:
  DATALAYER_BATTERY_TYPE* datalayer_battery;
  uint8_t calculateCRC(CAN_frame rx_frame, uint8_t length, uint8_t initial_value);

  static const int MAX_PACK_VOLTAGE_DV = 4480;
  static const int MIN_PACK_VOLTAGE_DV = 2920;
  static const int MAX_CELL_DEVIATION_MV = 150;
  static const int MAX_CELL_VOLTAGE_MV = 4200;  //Battery is put into emergency stop if one cell goes over this value
  static const int MIN_CELL_VOLTAGE_MV = 3300;  //Battery is put into emergency stop if one cell goes below this value

  uint8_t BMS_state = 0;
  uint16_t pre_contactor_voltage = 3700;
  uint16_t main_contactor_voltage = 0;
  uint16_t voltage_reference = 0;
  uint16_t DCFC_contactor_voltage = 0;
  uint16_t battery_SOC_average = 5000;
  uint16_t battery_SOC_max = 0;
  uint16_t battery_SOC_min = 0;
  int32_t battery_current = 32000;
  uint16_t kWh_available_total = 135;
  uint16_t kWh_available_max = 135;
  int16_t battery_min_temperature = 0;
  int16_t battery_max_temperature = 0;
  uint16_t battery_discharge_limit_amp = 0;
  uint16_t battery_charge_limit_amp = 0;
  uint16_t cell_min_voltage_mV = 0;
  uint16_t cell_max_voltage_mV = 0;
  uint8_t error_flags_from_BMS = 0;
  uint8_t contactor_state = 0;
  uint8_t HVIL = 0;
  uint8_t HMI_part1 = 0;
  uint8_t HMI_part2 = 0;
  uint8_t isolation_fault_status = 0;
  uint8_t system_safe_state = 0;
  bool error_relay_open = false;
  bool IsolationMeasurementOngoing = false;
  bool battery_thermal_runaway = false;
  bool puncture_fault = false;
  bool liquid_fault = false;
  bool contactor_DCFC_welded = false;
  bool NACS_charger_detected = false;
  bool slewrate_potential_violation = false;
  bool minimum_power_potential_violation = false;
  bool operation_limit_violation_warning = false;
  static const uint8_t SLEEP = 0;
  static const uint8_t STANDBY = 1;
  static const uint8_t READY = 2;
  static const uint8_t GO = 3;
  unsigned long previousMillis10 = 0;  // will store last time a 10ms CAN Message was sent

  CAN_frame RIVIAN_150 = {.FD = false,
                          .ext_ID = false,
                          .DLC = 6,
                          .ID = 0x150,
                          .data = {0x03, 0x00, 0x01, 0x00, 0x01, 0x00}};
  CAN_frame RIVIAN_420 = {.FD = false,
                          .ext_ID = false,
                          .DLC = 8,
                          .ID = 0x420,
                          .data = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame RIVIAN_41F = {.FD = false, .ext_ID = false, .DLC = 3, .ID = 0x41F, .data = {0x62, 0x10, 0x00}};
  CAN_frame RIVIAN_245 = {.FD = false,
                          .ext_ID = false,
                          .DLC = 6,
                          .ID = 0x245,
                          .data = {0x10, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame RIVIAN_200 = {.FD = false, .ext_ID = false, .DLC = 1, .ID = 0x200, .data = {0x08}};
  CAN_frame RIVIAN_207 = {.FD = false,
                          .ext_ID = false,
                          .DLC = 1,
                          .ID = 0x207,
                          .data = {0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00}};
};

#endif
