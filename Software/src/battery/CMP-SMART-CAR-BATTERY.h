#ifndef CMP_SMART_CAR_BATTERY_H
#define CMP_SMART_CAR_BATTERY_H
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "../devboard/hal/hal.h"
#include "BatterySlotContext.h"
#include "CanBattery.h"

inline const char* getContactorStates(int index) {
  switch (index) {
    case 0:
      return "Open";
    case 1:
      return "Closed";
    case 2:
      return "STUCK Open!";
    case 3:
      return "STUCK Closed!";
    default:
      return "";
  }
}

class CmpSmartCarBattery : public CanBattery {
 public:
  CmpSmartCarBattery(const BatterySlotContext& ctx) : CanBattery(ctx.can_interface) {
    datalayer_battery = ctx.datalayer;
    datalayer_cmpsmart = ctx.is_primary() ? &datalayer_extended.stellantisCMPsmart : nullptr;
  }

  virtual void setup(void);
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);

  const std::vector<BatteryCommand>& get_commands() override { return commands_; }

  bool supports_insulation_resistance() override { return true; }

  bool supports_charged_energy() { return true; }

  BatteryAdvancedStatus get_advanced_status() override {
    BatteryAdvancedStatus status;
    AdvancedSection s;

    s.fields.push_back(
        kv("Balancing active", datalayer_extended.stellantisCMPsmart.battery_balancing_active ? "Yes" : "No"));
    s.fields.push_back(
        kv("Positive contactor", getContactorStates(datalayer_extended.stellantisCMPsmart.battery_positive_contactor_state)));
    s.fields.push_back(
        kv("Negative contactor", getContactorStates(datalayer_extended.stellantisCMPsmart.battery_negative_contactor_state)));
    s.fields.push_back(
        kv("Precharge contactor", getContactorStates(datalayer_extended.stellantisCMPsmart.battery_precharge_contactor_state)));
    s.fields.push_back(kv("Wakeup reason", String(datalayer_extended.stellantisCMPsmart.hvbat_wakeup_state)));

    String battery_state;
    if (datalayer_extended.stellantisCMPsmart.battery_state == 0) {
      battery_state = "Sleep";
    } else if (datalayer_extended.stellantisCMPsmart.battery_state == 1) {
      battery_state = "Initialization";
    } else if (datalayer_extended.stellantisCMPsmart.battery_state == 2) {
      battery_state = "Wait";
    } else if (datalayer_extended.stellantisCMPsmart.battery_state == 3) {
      battery_state = "Ready";
    } else if (datalayer_extended.stellantisCMPsmart.battery_state == 4) {
      battery_state = "Preheat";
    } else if (datalayer_extended.stellantisCMPsmart.battery_state == 5) {
      battery_state = "Discharge";
    } else if (datalayer_extended.stellantisCMPsmart.battery_state == 6) {
      battery_state = "Charge";
    } else if (datalayer_extended.stellantisCMPsmart.battery_state == 7) {
      battery_state = "Fault";
    } else if (datalayer_extended.stellantisCMPsmart.battery_state == 8) {
      battery_state = "Pre-shutdown";
    } else if (datalayer_extended.stellantisCMPsmart.battery_state == 9) {
      battery_state = "Shutdown";
    } else if (datalayer_extended.stellantisCMPsmart.battery_state == 10) {
      battery_state = "Cooling";
    } else if (datalayer_extended.stellantisCMPsmart.battery_state == 11) {
      battery_state = "HV battery precondition";
    }
    s.fields.push_back(kv("Battery state", battery_state));

    s.fields.push_back(kv("Battery fault level", String(datalayer_extended.stellantisCMPsmart.battery_fault)));

    String eplug_status;
    if (datalayer_extended.stellantisCMPsmart.eplug_status == 0) {
      eplug_status = "Seated OK";
    } else if (datalayer_extended.stellantisCMPsmart.eplug_status == 1) {
      eplug_status = "Disconnected!";
    } else if (datalayer_extended.stellantisCMPsmart.eplug_status == 2) {
      eplug_status = "Open Status";
    } else if (datalayer_extended.stellantisCMPsmart.eplug_status == 3) {
      eplug_status = "Invalid";
    }
    s.fields.push_back(kv("Eplug status", eplug_status));

    String hvil_status;
    if (datalayer_extended.stellantisCMPsmart.HVIL_status == 0) {
      hvil_status = "Closed OK";
    } else if (datalayer_extended.stellantisCMPsmart.HVIL_status == 1) {
      hvil_status = "OPEN!!";
    } else if (datalayer_extended.stellantisCMPsmart.HVIL_status == 2) {
      hvil_status = "Error";
    } else if (datalayer_extended.stellantisCMPsmart.HVIL_status == 3) {
      hvil_status = "Invalid";
    }
    s.fields.push_back(kv("HVIL status", hvil_status));

    String ev_warning;
    if (datalayer_extended.stellantisCMPsmart.ev_warning == 0) {
      ev_warning = "OK No alarm";
    } else if (datalayer_extended.stellantisCMPsmart.ev_warning == 1) {
      ev_warning = "Blinking!!";
    } else if (datalayer_extended.stellantisCMPsmart.ev_warning == 2) {
      ev_warning = "ON!!";
    } else if (datalayer_extended.stellantisCMPsmart.ev_warning == 3) {
      ev_warning = "Invalid";
    }
    s.fields.push_back(kv("EV Warning", ev_warning));

    s.fields.push_back(
        kv("Authorised for usage", datalayer_extended.stellantisCMPsmart.power_auth ? "NOT authorised" : "Authorised OK"));

    String charging_status;
    if (datalayer_extended.stellantisCMPsmart.battery_charging_status == 0) {
      charging_status = "Not initiated";
    } else if (datalayer_extended.stellantisCMPsmart.battery_charging_status == 1) {
      charging_status = "In progress";
    } else if (datalayer_extended.stellantisCMPsmart.battery_charging_status == 2) {
      charging_status = "Completed";
    } else if (datalayer_extended.stellantisCMPsmart.battery_charging_status == 3) {
      charging_status = "Failure";
    } else if (datalayer_extended.stellantisCMPsmart.battery_charging_status == 3) {
      charging_status = "Stopped";
    } else if (datalayer_extended.stellantisCMPsmart.battery_charging_status == 3) {
      charging_status = "Forbidden";
    } else if (datalayer_extended.stellantisCMPsmart.battery_charging_status == 3) {
      charging_status = "Prohibited, suggest preheat or precondition";
    }
    s.fields.push_back(kv("Charging status", charging_status));

    String insulation_status;
    if (datalayer_extended.stellantisCMPsmart.insulation_fault == 0) {
      insulation_status = "OK";
    } else if (datalayer_extended.stellantisCMPsmart.insulation_fault == 1) {
      insulation_status = "Symmetrical failure!!";
    } else if (datalayer_extended.stellantisCMPsmart.insulation_fault == 2) {
      insulation_status = "Asymmetric failure HV+!!";
    } else if (datalayer_extended.stellantisCMPsmart.insulation_fault == 3) {
      insulation_status = "Asymmetric failure HV-!!";
    }
    s.fields.push_back(kv("Insulation status", insulation_status));

    String insulation_circuit_status;
    if (datalayer_extended.stellantisCMPsmart.insulation_circuit_status == 0) {
      insulation_circuit_status = "Inactive (Insulation function not enable)";
    } else if (datalayer_extended.stellantisCMPsmart.insulation_circuit_status == 1) {
      insulation_circuit_status = "Active (Insulation function enable)";
    } else if (datalayer_extended.stellantisCMPsmart.insulation_circuit_status == 2) {
      insulation_circuit_status = "FAULT!!";
    } else if (datalayer_extended.stellantisCMPsmart.insulation_circuit_status == 3) {
      insulation_circuit_status = "Insulation measurement in progress";
    }
    s.fields.push_back(kv("Insulation circuit status", insulation_circuit_status));

    String hardware_fault_status;
    if (datalayer_extended.stellantisCMPsmart.hardware_fault_status == 0) {
      hardware_fault_status += "No Fault";
    }
    if (datalayer_extended.stellantisCMPsmart.hardware_fault_status & 0b001) {
      hardware_fault_status += "FAULT! Temperature sensor!";
    }
    if ((datalayer_extended.stellantisCMPsmart.hardware_fault_status & 0b010) >> 1) {
      hardware_fault_status += "FAULT! Voltage sensing circuit!";
    }
    if ((datalayer_extended.stellantisCMPsmart.hardware_fault_status & 0b100) >> 2) {
      hardware_fault_status += "FAULT! Current sensor!";
    }
    s.fields.push_back(kv("Hardware fault status", hardware_fault_status));

    String l3_fault;
    if (datalayer_extended.stellantisCMPsmart.l3_fault == 0) {
      l3_fault += "No Fault";
    }
    if (datalayer_extended.stellantisCMPsmart.l3_fault & 0b001) {
      l3_fault += "Cell undervoltage";
    }
    if ((datalayer_extended.stellantisCMPsmart.l3_fault & 0b010) >> 1) {
      l3_fault += "Cell overvoltage";
    }
    if ((datalayer_extended.stellantisCMPsmart.l3_fault & 0b100) >> 2) {
      l3_fault += "Over temperature";
    }
    if ((datalayer_extended.stellantisCMPsmart.l3_fault & 0b1000) >> 3) {
      l3_fault += "Under temperature";
    }
    if ((datalayer_extended.stellantisCMPsmart.l3_fault & 0b10000) >> 4) {
      l3_fault += "Over discharge current";
    }
    if ((datalayer_extended.stellantisCMPsmart.l3_fault & 0b100000) >> 5) {
      l3_fault += "Pack undedr voltage";
    }
    s.fields.push_back(kv("L3 Fault", l3_fault));

    String plausibility_error;
    if (datalayer_extended.stellantisCMPsmart.plausibility_error == 0) {
      plausibility_error += "No error";
    }
    if (datalayer_extended.stellantisCMPsmart.plausibility_error & 0b001) {
      plausibility_error += "Module temperature plausibility error";
    }
    if ((datalayer_extended.stellantisCMPsmart.plausibility_error & 0b010) >> 1) {
      plausibility_error += "Cell voltage plausibility error";
    }
    if ((datalayer_extended.stellantisCMPsmart.plausibility_error & 0b100) >> 2) {
      plausibility_error += "Battery voltlage plausibility error";
    }
    if ((datalayer_extended.stellantisCMPsmart.plausibility_error & 0b1000) >> 3) {
      plausibility_error += "HVBAT Current plausibility error";
    }
    s.fields.push_back(kv("Plausibility error", plausibility_error));

    if ((datalayer_extended.stellantisCMPsmart.alert_frame3 > 0) ||
        (datalayer_extended.stellantisCMPsmart.alert_frame4 > 0)) {
      String alert;
      if (datalayer_extended.stellantisCMPsmart.alert_frame3 & 0b001) {
        alert += "Cell Undervoltage ";
      }
      if ((datalayer_extended.stellantisCMPsmart.alert_frame3 & 0b010) >> 1) {
        alert += "Cell Overvoltage ";
      }
      if ((datalayer_extended.stellantisCMPsmart.alert_frame3 & 0b100) >> 1) {
        alert += "High SOC ";
      }
      if ((datalayer_extended.stellantisCMPsmart.alert_frame3 & 0b1000) >> 1) {
        alert += "Low SOC ";
      }
      if ((datalayer_extended.stellantisCMPsmart.alert_frame3 & 0b10000) >> 1) {
        alert += "Overvoltage ";
      }
      if ((datalayer_extended.stellantisCMPsmart.alert_frame3 & 0b100000) >> 1) {
        alert += "High temperature ";
      }
      if ((datalayer_extended.stellantisCMPsmart.alert_frame3 & 0b01000000) >> 1) {
        alert += "Temperature Delta ";
      }
      if ((datalayer_extended.stellantisCMPsmart.alert_frame3 & 0b10000000) >> 1) {
        alert += "Battery ";
      }
      if ((datalayer_extended.stellantisCMPsmart.alert_frame4 & 0b10000) >> 1) {
        alert += "Contactor Opening ";
      }
      if ((datalayer_extended.stellantisCMPsmart.alert_frame4 & 0b100000) >> 1) {
        alert += "Overcharge ";
      }
      if ((datalayer_extended.stellantisCMPsmart.alert_frame4 & 0b01000000) >> 1) {
        alert += "Cell poor consistency ";
      }
      if ((datalayer_extended.stellantisCMPsmart.alert_frame4 & 0b10000000) >> 1) {
        alert += "SOC jump";
      }
      s.fields.push_back(kv("Alert", alert));
    }

    s.fields.push_back(kv("RCD line active", datalayer_extended.stellantisCMPsmart.rcd_line_active ? "Yes" : "No"));

    String active_dtc_code = String(datalayer_extended.stellantisCMPsmart.active_DTC_code);
    if (datalayer_extended.stellantisCMPsmart.active_DTC_code == 9) {
      active_dtc_code += " Temperature sensor missing between pin 21-22";
    }
    s.fields.push_back(kv("Active DTC Code", active_dtc_code));

    status.sections.push_back(s);
    return status;
  }

 private:
  void reset_DTC() { datalayer_extended.stellantisCMPsmart.UserRequestDTCreset = true; }

  std::vector<BatteryCommand> commands_ = {
      command(CMD_RESET_DTC, [this] { reset_DTC(); }),
  };

  DATALAYER_BATTERY_TYPE* datalayer_battery;
  DATALAYER_INFO_CMPSMART* datalayer_cmpsmart;

  static const int MAX_PACK_VOLTAGE_100S_DV = 3700;
  static const int MIN_PACK_VOLTAGE_100S_DV = 2900;
  static const int MAX_CELL_DEVIATION_MV = 100;
  static const int MAX_CELL_VOLTAGE_MV = 3650;
  static const int MIN_CELL_VOLTAGE_MV = 2800;

  unsigned long previousMillis10 = 0;    // will store last time a 10ms CAN Message was sent
  unsigned long previousMillis50 = 0;    // will store last time a 50ms CAN Message was sent
  unsigned long previousMillis60 = 0;    // will store last time a 60ms CAN Message was sent
  unsigned long previousMillis100 = 0;   // will store last time a 100ms CAN Message was sent
  unsigned long previousMillis1000 = 0;  // will store last time a 1000ms CAN Message was sent

  uint8_t precalculated432[16] = {0x12, 0x11, 0x10, 0x1F, 0x1E, 0x1D, 0x1C, 0x1B,
                                  0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13};

  CAN_frame CMP_211 = {.FD = false,  //VCU contactor 100ms
                       .ext_ID = false,
                       .DLC = 8,
                       .ID = 0x211,
                       .data = {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame CMP_351 = {.FD = false,  //VCU 60ms Airbag
                       .ext_ID = false,
                       .DLC = 8,
                       .ID = 0x351,
                       .data = {0x46, 0x14, 0x17, 0x00, 0x00, 0x00, 0x00, 0x0F}};
  CAN_frame CMP_432 = {.FD = false,  //VCU 50ms
                       .ext_ID = false,
                       .DLC = 8,
                       .ID = 0x432,
                       .data = {0x80, 0x10, 0x00, 0x00, 0x00, 0x00, 0x7D, 0x52}};

  //Optional CAN messages to simulate more of the vehicle towards the battery (Not required?)
  /*
    uint8_t checksum217[16] = {0x50, 0x41, 0xB2, 0xA3, 0x14, 0x05, 0xF6, 0xE7,
                             0x58, 0xC9, 0xBA, 0xAB, 0x1C, 0x8D, 0x7E, 0x6F};
  CAN_frame CMP_208 = {.FD = false,  //VCU 10ms
                       .ext_ID = false,
                       .DLC = 8,
                       .ID = 0x208,
                       .data = {0x00, 0x20, 0x00, 0x84, 0x40, 0x21, 0x00, 0x00}};

  CAN_frame CMP_217 = {.FD = false,  //VCU 10ms (Inverter motor speed)
                       .ext_ID = false,
                       .DLC = 8,
                       .ID = 0x217,
                       .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA6, 0x00, 0x00}};
  CAN_frame CMP_231 = {
      .FD = false,  //VCU preconditioning
      .ext_ID = false,
      .DLC = 8,     //
      .ID = 0x231,  //0b00 : Not active0b01 : Heating function active0b10 : Cooling function active0b11 : Reserve
      .data = {0x98, 0x59, 0x60, 0x00, 0xA3, 0x20, 0x00, 0x00}};  //Last byte, bit pos 1, has precond req
  CAN_frame CMP_241 = {.FD = false,                               //VCU vehicle speed and emg stop 10ms
                       .ext_ID = false,
                       .DLC = 8,
                       .ID = 0x241,
                       .data = {0x00, 0x00, 0x39, 0x00, 0xC8, 0x00, 0x00, 0x00}};
  CAN_frame CMP_262 = {.FD = false,  //VCU 10ms
                       .ext_ID = false,
                       .DLC = 1,
                       .ID = 0x262,
                       .data = {0x00}};

  CAN_frame CMP_421 = {.FD = false,  //VCU 50ms
                       .ext_ID = false,
                       .DLC = 8,
                       .ID = 0x421,
                       .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame CMP_422 = {.FD = false,  //100ms VCU, Configuration
                       .ext_ID = false,
                       .DLC = 8,
                       .ID = 0x422,  //Fitting, Plant,check,storage,client,APV,showroom etc.
                       .data = {0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00}};

  CAN_frame CMP_4A2 = {.FD = false,  //OBC plug 100ms
                       .ext_ID = false,
                       .DLC = 2,
                       .ID = 0x4A2,
                       .data = {0x00, 0x41}};  //second byte, 00 plugged, 64 unplugged, 41vehiclerunning
  CAN_frame CMP_552 = {.FD = false,            //VCU mileage and time 1000ms
                       .ext_ID = false,
                       .DLC = 8,
                       .ID = 0x552,
                       .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE}};
  */
  CAN_frame CMP_POLL = {.FD = false, .ext_ID = false, .DLC = 4, .ID = 0x6B4, .data = {0x03, 0x22, 0xD8, 0x13}};
  CAN_frame CMP_CLEAR_ALL_DTC = {.FD = false,
                                 .ext_ID = false,
                                 .DLC = 5,
                                 .ID = 0x6B4,
                                 .data = {0x04, 0x14, 0xFF, 0xFF, 0xFF}};
  uint32_t vehicle_time_counter = 0x088B390B;  //Taken from log on 19thOctober2025
  uint32_t main_contactor_cycle_count = 0;
  uint32_t QC_contactor_cycle_count = 0;
  uint32_t lifetime_kWh_charged = 0;
  uint32_t lifetime_kWh_discharged = 0;
  uint32_t remaining_energy_Wh = 0;
  uint32_t total_energy_when_full_Wh = 41400;
  uint32_t total_coloumb_counting_Ah = 0;
  uint32_t total_coulomb_counting_kWh = 0;

  uint16_t discharge_available_10s_power = 0;
  uint16_t discharge_available_10s_current = 0;
  uint16_t discharge_cont_available_power = 0;
  uint16_t discharge_cont_available_current = 0;
  uint16_t discharge_available_30s_current = 0;
  uint16_t discharge_available_30s_power = 0;
  uint16_t regen_charge_cont_power = 0;
  uint16_t regen_charge_30s_power = 0;
  uint16_t regen_charge_30s_current = 0;
  uint16_t regen_charge_cont_current = 0;
  uint16_t regen_charge_10s_current = 0;
  uint16_t regen_charge_10s_power = 0;
  uint16_t quick_charge_port_voltage = 0;
  uint16_t insulation_resistance_kOhm = 0;
  uint16_t DC_bus_voltage = 0;
  uint16_t charge_max_voltage = 0;
  uint16_t charge_cont_curr_max = 0;
  uint16_t charge_cont_curr_req = 0;
  uint16_t hours_spent_overvoltage = 0;
  uint16_t hours_spent_overtemperature = 0;
  uint16_t hours_spent_undertemperature = 0;
  uint16_t battery_soc = 500;
  uint16_t battery_voltage = 3300;
  uint16_t temp = 0;
  uint16_t min_cell_voltage = 3300;
  uint16_t max_cell_voltage = 3300;
  uint16_t nominal_voltage = 0;
  uint16_t charge_continue_power_limit = 0;
  uint16_t charge_energy_amount_requested = 0;
  uint16_t hours_spent_exceeding_charge_power = 0;
  uint16_t hours_spent_exceeding_discharge_power = 0;
  uint16_t SOC_actual = 0;

  int16_t battery_temperature_average = 0;
  int16_t battery_temperature_maximum = 0;
  int16_t coolant_temperature = 0;
  int16_t battery_temperature_minimum = 0;
  int16_t battery_current_dA = 0;

  uint8_t tempval = 0;
  uint8_t startup_increment = 0;
  uint8_t active_DTC_code = 0;
  uint8_t battery_quickcharge_connect_status = 0;
  //uint8_t qc_negative_contactor_status = 0;
  //uint8_t qc_positive_contactor_status = 0;
  uint8_t eplug_status = 0;
  uint8_t ev_warning = 0;
  uint8_t battery_state = 0;
  uint8_t battery_fault = 0;
  uint8_t battery_negative_contactor_state = 0;
  uint8_t battery_precharge_contactor_state = 0;
  uint8_t battery_positive_contactor_state = 0;
  uint8_t battery_connect_status = 0;
  uint8_t battery_charging_status = 0;
  uint8_t min_cell_voltage_number = 0;
  uint8_t max_cell_voltage_number = 0;
  uint8_t bulk_SOC_DC_limit = 0;
  uint8_t mux = 0;
  uint8_t startup_counter_432 = 0;
  uint8_t counter_10ms = 0;
  uint8_t counter_50ms = 0;
  uint8_t counter_60ms = 0;
  uint8_t counter_100ms = 0;
  uint8_t SOH_internal_resistance = 0;
  uint8_t SOH_estimated = 100;
  uint8_t max_temperature_probe_number = 0;
  uint8_t min_temperature_probe_number = 0;
  uint8_t number_of_temperature_sensors = 0;
  uint8_t number_of_cells = 0;
  uint8_t coolant_temperature_warning = 0;
  uint8_t heater_relay_status = 0;
  uint8_t preheating_status = 0;
  uint8_t thermal_control = 0;
  uint8_t thermal_runaway = 0;
  uint8_t thermal_runaway_module_ID = 0;
  uint8_t HVIL_status = 0;
  uint8_t hardware_fault_status = 0;
  uint8_t insulation_fault = 0;
  uint8_t temperature = 0;
  uint8_t insulation_circuit_status = 0;
  uint8_t plausibility_error = 0;
  uint8_t service_due = 0;
  uint8_t l3_fault = 0;
  uint8_t master_warning = 0;
  uint8_t hvbat_wakeup_state = 0;
  uint8_t alert_frame3 = 0;
  uint8_t alert_frame4 = 0;

  bool rcd_line_active = false;
  bool power_auth = false;
  bool battery_balancing_active = false;
  bool coolant_alarm = false;
  bool cooling_enabled = false;
  bool battery_minimum_voltage_reached_warning = false;
  bool alert_low_battery_energy = false;
};
#endif
