#ifndef ATTO_3_BATTERY_H
#define ATTO_3_BATTERY_H

#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"

#include <Arduino.h>
#include "../devboard/webserver/advanced_api.h"
#include "BatterySlotContext.h"
#include "CanBattery.h"

class BydAttoBattery : public CanBattery {
 public:
  // Global datalayer_extended.bydAtto3 / bydAtto3_2: webserver, MQTT and comm_nvm read them.
  BydAttoBattery(const BatterySlotContext& ctx) : CanBattery(ctx.can_interface) {
    datalayer_battery = ctx.datalayer;
    allows_contactor_closing = ctx.is_primary() ? ctx.contactor_flag : nullptr;
    datalayer_bydatto = ctx.is_primary() ? &datalayer_extended.bydAtto3 : &datalayer_extended.bydAtto3_2;
  }

  virtual void setup(void);
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);

  bool supports_charged_energy() { return true; }

  const std::vector<BatteryCommand>& get_commands() override { return commands_; }

  bool supports_insulation_resistance() override { return true; }

  const char* get_dtc_json_filename() override { return "byd_atto3_dtc.json"; }

  BatteryAdvancedStatus get_advanced_status() override {
    BatteryAdvancedStatus status;
    DATALAYER_INFO_BYDATTO3* byd_datalayer = datalayer_bydatto;
    const String s = (datalayer_bydatto == &datalayer_extended.bydAtto3) ? "" : "2";
    AdvancedSection sec;

    const auto& dl_bat = *datalayer_battery;
    sec.fields.push_back(kv("Detected cells", String(dl_bat.info.number_of_cells)));

    String contactor_control_state;
    switch (byd_datalayer->contactor_control_state) {
      case 0:
        contactor_control_state = "Closing";
        break;
      case 1:
        contactor_control_state = "Closed (live)";
        break;
      case 2:
        contactor_control_state = "Preparing to open";
        break;
      case 3:
        contactor_control_state = "Opening";
        break;
      case 4:
        contactor_control_state = "Standby / idle";
        break;
      case 5:
        contactor_control_state = "Open requested";
        break;
      case 6:
        contactor_control_state = "Open (settling)";
        break;
      case 7:
        contactor_control_state = "Held open (fault / e-stop / startup)";
        break;
      default:
        contactor_control_state = "Unknown";
    }
    sec.fields.push_back(kv("BE contactor state", contactor_control_state));

    sec.fields.push_back(kv("Main contactors", byd_datalayer->contactor_main_closed
                                                    ? "Closed — battery connected"
                                                    : "Open — battery disconnected"));
    sec.fields.push_back(kv("Precharge state", byd_datalayer->contactor_precharging ? "Active" : "Idle"));
    // Bit2 (0x04) = car on/off (clear during car-off AC charging even though HV is live),
    // not literal HV-bus energisation.
    sec.fields.push_back(kv("HV active", byd_datalayer->contactor_hv_active ? "Yes" : "No"));

    // Pack mode read straight from the 0x344 byte0 state table (not re-derived per-bit).
    String pack_mode;
    switch (byd_datalayer->contactor_feedback) {
      case 0x00:
        pack_mode = "Disconnected";
        break;
      case 0x02:
        pack_mode = "Open standby";
        break;
      case 0x42:
        pack_mode = "Precharging";
        break;
      case 0x80:
        pack_mode = "Closed, HV inactive";
        break;
      case 0x84:
        pack_mode = "Closed idle, HV active";
        break;
      case 0x81:
        pack_mode = "Charging, car off";
        break;
      case 0x85:
        pack_mode = "Charging, HV active";
        break;
      case 0x82:
        pack_mode = "Drive-ready pending";
        break;
      case 0x86:
        pack_mode = "Drive ready";
        break;
      default: {
        if (!(byd_datalayer->contactor_feedback & 0x80)) {
          pack_mode = "Disconnected";
        } else if (byd_datalayer->contactor_feedback & 0x01) {
          pack_mode = "Charging";
        } else if (byd_datalayer->contactor_feedback & 0x02) {
          pack_mode = "Drive";
        } else {
          pack_mode = "Closed idle";
        }
        char modeStr[10];
        snprintf(modeStr, sizeof(modeStr), " (0x%02X)", byd_datalayer->contactor_feedback);
        pack_mode += modeStr;
      }
    }
    sec.fields.push_back(kv("BMS pack mode", pack_mode));

    char feedbackStr[5];
    snprintf(feedbackStr, sizeof(feedbackStr), "0x%02X", byd_datalayer->contactor_feedback);
    // 0x344 byte1 low nibble: a BMS state code whose meaning is unconfirmed (reads 1 in
    // idle/drive/discharge alike). byte0 is the real charge/drive truth, so show this raw.
    sec.fields.push_back(
        kv("BMS raw status", "mode " + String(feedbackStr) + ", state " + String(byd_datalayer->discharge_status)));

    float soc_measured = static_cast<float>(byd_datalayer->SOC_highprec) * 0.1f;
    float BMS_maxChargePower = static_cast<float>(byd_datalayer->chargePower) * 0.1f;
    float BMS_maxDischargePower = static_cast<float>(byd_datalayer->dischargePower) * 0.1f;

    sec.fields.push_back(kv("SOC measured", String(soc_measured), "%"));
    sec.fields.push_back(kv("SOC OBD2", String(byd_datalayer->SOC_polled), "%"));
    if (byd_datalayer->pack_voltage_dV > 0) {
      sec.fields.push_back(kv("Pack voltage", String(byd_datalayer->pack_voltage_dV / 10.0f, 1), "V"));
    } else {
      sec.fields.push_back(kv("Pack voltage", "Not received"));
    }

    for (int i = 0; i < 13; i++) {
      if (byd_datalayer->battery_temperatures[i] != 215) {
        sec.fields.push_back(kv("Temperature sensor " + String(i + 1), String(byd_datalayer->battery_temperatures[i]),
                                "°C"));
      }
    }

    sec.fields.push_back(kv("Max discharge power", String(BMS_maxDischargePower), "kW"));
    sec.fields.push_back(kv("Max charge (regen) power", String(BMS_maxChargePower), "kW"));
    sec.fields.push_back(kv("Total charged", String(byd_datalayer->total_charged_kwh), "kWh"));
    sec.fields.push_back(kv("Total discharged", String(byd_datalayer->total_discharged_kwh), "kWh"));
    sec.fields.push_back(kv("Total charged (Ah)", String(byd_datalayer->total_charged_ah), "Ah"));
    sec.fields.push_back(kv("Total discharged (Ah)", String(byd_datalayer->total_discharged_ah), "Ah"));
    sec.fields.push_back(kv("Charge times", String(byd_datalayer->charge_times)));
    sec.fields.push_back(kv("Times of full power", String(byd_datalayer->times_full_power)));
    sec.fields.push_back(kv("Min cell voltage number", String(byd_datalayer->BMS_min_cell_voltage_number)));
    sec.fields.push_back(kv("Max cell voltage number", String(byd_datalayer->BMS_max_cell_voltage_number)));
    sec.fields.push_back(kv("Min temp module number", String(byd_datalayer->BMS_min_temp_module_number)));
    sec.fields.push_back(kv("Max temp module number", String(byd_datalayer->BMS_max_temp_module_number)));
    sec.fields.push_back(kv("Seed", String(byd_datalayer->seed)));
    sec.fields.push_back(kv("SolvedKey", String(byd_datalayer->solvedKey)));

    if (byd_datalayer->servicemode == 0) {
      sec.fields.push_back(kv("ServiceMode", "No command ran yet"));
    } else if (byd_datalayer->servicemode == 1) {
      sec.fields.push_back(kv("ServiceMode", "REJECTED"));
    } else if (byd_datalayer->servicemode == 2) {
      sec.fields.push_back(kv("ServiceMode", "APPROVED!"));
    }

    sec.fields.push_back(
        kv("Capacity original", String((byd_datalayer->BMS_capacity_original_calibration) / 100), "AH"));
    sec.fields.push_back(
        kv("Capacity current", String((byd_datalayer->BMS_capacity_current_calibration) / 100), "AH"));
    sec.fields.push_back(kv("SOC original", String(byd_datalayer->BMC_SOC_original_calibration), "%"));
    sec.fields.push_back(kv("SOC current", String(byd_datalayer->BMC_SOC_current_calibration), "%"));

    {
      uint32_t dwell_sec = byd_datalayer->autocal_dwell_accumulated_ms / 1000;
      uint32_t dwell_min = dwell_sec / 60;
      uint32_t dwell_rem = dwell_sec % 60;
      uint32_t grace_sec = byd_datalayer->autocal_grace_timer_ms / 1000;
      float autocal_current_A = static_cast<float>(byd_datalayer->autocal_current_dA) / 10.0f;
      const char* current_direction = "idle";
      if (byd_datalayer->autocal_current_dA < 0) {
        current_direction = "discharge";
      } else if (byd_datalayer->autocal_current_dA > 0) {
        current_direction = "charge";
      }

      String current_in_range;
      if (!byd_datalayer->autocal_crit_taper) {
        current_in_range = "Waiting for taper";
      } else if (byd_datalayer->autocal_crit_low_current) {
        current_in_range = "Yes (chg ≤3A, disch ≤0.5A)";
      } else {
        current_in_range = "No — " + String(grace_sec) + "s / 60s";
      }

      AdvancedField autocal_table;
      autocal_table.kind = AdvancedFieldKind::Table;
      autocal_table.label = "Auto-calibration status";
      autocal_table.columns = {"Metric", "Value"};
      autocal_table.rows.push_back({"Contactors", byd_datalayer->autocal_crit_contactors ? "OK" : "Open"});
      autocal_table.rows.push_back({"Full / In taper?", byd_datalayer->autocal_crit_taper ? "Yes" : "No"});
      autocal_table.rows.push_back(
          {"Battery current", String(autocal_current_A, 1) + " A (" + String(current_direction) + ")"});
      autocal_table.rows.push_back({"Current in range", current_in_range});
      autocal_table.rows.push_back({"Dwell time", String(dwell_min) + "m " + String(dwell_rem) + "s / 10m"});
      autocal_table.rows.push_back({"SOC drift", String(byd_datalayer->autocal_drift_percent, 1) + "% / threshold " +
                                                       String(byd_datalayer->auto_calibrate_soc_drift_percent) + "%"});
      autocal_table.rows.push_back(
          {"Cooldown", byd_datalayer->autocal_crit_cooldown_ready ? "Ready" : "Waiting"});
      sec.fields.push_back(autocal_table);
    }

    status.sections.push_back(sec);

    AdvancedSection iso;
    iso.title = "Isolation resistance monitor";

    String monitoring;
    if (!byd_datalayer->iso_status_valid) {
      monitoring = "Unknown";
    } else if (!byd_datalayer->contactor_main_closed) {
      // Monitoring status from 0x35E b0 bit0x80; only unambiguous when the pack is closed
      monitoring = "Inactive (pack open)";
    } else {
      monitoring = byd_datalayer->iso_measurement_active ? "On" : "Off";
    }
    iso.fields.push_back(kv("Monitoring", monitoring));

    if (byd_datalayer->insulation_valid) {
      float iso_kohm = static_cast<float>(byd_datalayer->insulation_ohm_per_volt) *
                       (datalayer_battery->status.voltage_dV / 10.0f) / 1000.0f;
      iso.fields.push_back(kv("Insulation resistance",
                              String(iso_kohm, 1) + " kΩ (" + String(byd_datalayer->insulation_ohm_per_volt) + " Ω/V)"));
    } else {
      iso.fields.push_back(kv("Insulation resistance", "Not received"));
    }

    // Command feedback, only shown while something is pending or after a failure
    if (byd_datalayer->iso_command_status == 1) {
      iso.fields.push_back(kv("Last command", "Sending…"));
    } else if (byd_datalayer->iso_command_status == 3) {
      iso.fields.push_back(kv("Last command", "Rejected"));
    } else if (byd_datalayer->iso_command_status == 4) {
      iso.fields.push_back(kv("Last command", "No response"));
    }
    status.sections.push_back(iso);

    auto& dtc = datalayer_battery->dtc;
    status.sections.push_back(dtc_advanced_section(*this, dtc, DtcCodeStyle::kStandard));
    return status;
  }

 private:
  void calibrate_SOC() { datalayer_bydatto->UserRequestCalibrateSOC = true; }
  void request_open_contactors() { requestContactorOpen = true; }
  void request_close_contactors() { requestContactorClose = true; }
  void read_DTC() { datalayer_bydatto->UserRequestDTCreadout = true; }
  void reset_crash() { datalayer_bydatto->UserRequestCrashReset = true; }
  void reset_DTC() { datalayer_bydatto->UserRequestDTCreset = true; }
  void iso_monitor_enable() { datalayer_bydatto->UserRequestIsoRoutineEnable = true; }
  void iso_monitor_disable() { datalayer_bydatto->UserRequestIsoRoutineDisable = true; }

  std::vector<BatteryCommand> commands_ = {
      command(CMD_CALIBRATE_SOC, [this] { calibrate_SOC(); }),
      command(CMD_CONTACTOR_CLOSE, [this] { request_close_contactors(); }),
      command(CMD_CONTACTOR_OPEN, [this] { request_open_contactors(); }),
      command(CMD_READ_DTC, [this] { read_DTC(); }),
      command(CMD_RESET_CRASH, [this] { reset_crash(); }),
      command(CMD_RESET_DTC, [this] { reset_DTC(); }),
      command(CMD_ISO_MONITOR_ENABLE, [this] { iso_monitor_enable(); },
              [this] { return datalayer_bydatto == &datalayer_extended.bydAtto3; }),
      command(CMD_ISO_MONITOR_DISABLE, [this] { iso_monitor_disable(); },
              [this] { return datalayer_bydatto == &datalayer_extended.bydAtto3; }),
  };

  DATALAYER_BATTERY_TYPE* datalayer_battery;
  DATALAYER_INFO_BYDATTO3* datalayer_bydatto;
  bool* allows_contactor_closing;

  // Ramp down settings that are used when SOC is estimated from voltage
  static const int RAMPDOWN_SOC = 100;  // SOC to start ramping down from. Value set here is scaled by 10 (100 = 10.0%)
  static const int RAMPDOWN_POWER_ALLOWED =
      10000;  // Power to start ramp down from, set a lower value to limit the power even further as SOC decreases

  unsigned long previousMillis50 = 0;   // will store last time a 50ms CAN Message was send
  unsigned long previousMillis100 = 0;  // will store last time a 100ms CAN Message was send
  unsigned long previousMillis200 = 0;  // will store last time a 200ms CAN Message was send
  uint64_t last_auto_calibrate_ms = 0;  // Cooldown timer for auto-calibration
  uint32_t autocal_dwell_ms = 0;        // Valid low-current/full time
  uint32_t autocal_grace_start_ms = 0;  // When current left the valid window
  uint16_t cap_slewed_dA = 0;           // Charge-taper slewed current cap
  uint32_t taper_last_ms = 0;
  bool taper_initialized = false;  // explicit flag avoids cap_slewed_dA starting at 0

  static const int POLL_TIMES_FULL_POWER = 0x0004;  // Using Carscanner name for now.
  // 0x0005/0x0008/0x0009 (SOC, voltage, current) come from 0x444 and 0x438, not polled.
  // 0x000A/0x000E (allowed charge/discharge power) come from 0x345 at ~100ms, not polled.
  static const int POLL_CHARGE_TIMES = 0x000B;  // Using Carscanner name for now.
  static const int POLL_TOTAL_CHARGED_AH = 0x000F;
  static const int POLL_TOTAL_DISCHARGED_AH = 0x0010;
  static const int POLL_TOTAL_CHARGED_KWH = 0x0011;
  static const int POLL_TOTAL_DISCHARGED_KWH = 0x0012;
  // 0x002A-0x002D (cell min/max number + voltage) are sourced from the 0x446 broadcast, not polled.
  // 0x002E-0x0032 (temperatures and sensor numbers) are sourced from the 0x447 broadcast, not polled.
  static const int POLL_MODULE_1_LOWEST_MV_NUMBER = 0x016C;
  static const int POLL_MODULE_1_LOWEST_CELL_MV = 0x016D;
  static const int POLL_MODULE_1_HIGHEST_MV_NUMBER = 0x016E;
  static const int POLL_MODULE_1_HIGH_CELL_MV = 0x016F;
  static const int POLL_MODULE_1_HIGH_TEMP = 0x0171;
  static const int POLL_MODULE_1_LOW_TEMP = 0x0173;
  static const int POLL_MODULE_2_LOWEST_MV_NUMBER = 0x0174;
  static const int POLL_MODULE_2_LOWEST_CELL_MV = 0x0175;
  static const int POLL_MODULE_2_HIGHEST_MV_NUMBER = 0x0176;
  static const int POLL_MODULE_2_HIGH_CELL_MV = 0x0177;
  static const int POLL_MODULE_2_HIGH_TEMP = 0x0179;
  static const int POLL_MODULE_2_LOW_TEMP = 0x017B;
  static const int POLL_MODULE_3_LOWEST_MV_NUMBER = 0x017C;
  static const int POLL_MODULE_3_LOWEST_CELL_MV = 0x017D;
  static const int POLL_MODULE_3_HIGHEST_MV_NUMBER = 0x017E;
  static const int POLL_MODULE_3_HIGH_CELL_MV = 0x017F;
  static const int POLL_MODULE_3_HIGH_TEMP = 0x0181;
  static const int POLL_MODULE_3_LOW_TEMP = 0x0183;
  static const int POLL_MODULE_4_LOWEST_MV_NUMBER = 0x0184;
  static const int POLL_MODULE_4_LOWEST_CELL_MV = 0x0185;
  static const int POLL_MODULE_4_HIGHEST_MV_NUMBER = 0x0186;
  static const int POLL_MODULE_4_HIGH_CELL_MV = 0x0187;
  static const int POLL_MODULE_4_HIGH_TEMP = 0x0189;
  static const int POLL_MODULE_4_LOW_TEMP = 0x018B;
  static const int POLL_MODULE_5_LOWEST_MV_NUMBER = 0x018C;
  static const int POLL_MODULE_5_LOWEST_CELL_MV = 0x018D;
  static const int POLL_MODULE_5_HIGHEST_MV_NUMBER = 0x018E;
  static const int POLL_MODULE_5_HIGH_CELL_MV = 0x018F;
  static const int POLL_MODULE_5_HIGH_TEMP = 0x0191;
  static const int POLL_MODULE_5_LOW_TEMP = 0x0193;
  static const int POLL_MODULE_6_LOWEST_MV_NUMBER = 0x0194;
  static const int POLL_MODULE_6_LOWEST_CELL_MV = 0x0195;
  static const int POLL_MODULE_6_HIGHEST_MV_NUMBER = 0x0196;
  static const int POLL_MODULE_6_HIGH_CELL_MV = 0x0197;
  static const int POLL_MODULE_6_HIGH_TEMP = 0x0199;
  static const int POLL_MODULE_6_LOW_TEMP = 0x019B;
  static const int POLL_MODULE_7_LOWEST_MV_NUMBER = 0x019C;
  static const int POLL_MODULE_7_LOWEST_CELL_MV = 0x019D;
  static const int POLL_MODULE_7_HIGHEST_MV_NUMBER = 0x019E;
  static const int POLL_MODULE_7_HIGH_CELL_MV = 0x019F;
  static const int POLL_MODULE_7_HIGH_TEMP = 0x01A1;
  static const int POLL_MODULE_7_LOW_TEMP = 0x01A3;
  static const int POLL_MODULE_8_LOWEST_MV_NUMBER = 0x01A4;
  static const int POLL_MODULE_8_LOWEST_CELL_MV = 0x01A5;
  static const int POLL_MODULE_8_HIGHEST_MV_NUMBER = 0x01A6;
  static const int POLL_MODULE_8_HIGH_CELL_MV = 0x01A7;
  static const int POLL_MODULE_8_HIGH_TEMP = 0x01A9;
  static const int POLL_MODULE_8_LOW_TEMP = 0x01AB;
  static const int POLL_MODULE_9_LOWEST_MV_NUMBER = 0x01AC;
  static const int POLL_MODULE_9_LOWEST_CELL_MV = 0x01AD;
  static const int POLL_MODULE_9_HIGHEST_MV_NUMBER = 0x01AE;
  static const int POLL_MODULE_9_HIGH_CELL_MV = 0x01AF;
  static const int POLL_MODULE_9_HIGH_TEMP = 0x01B1;
  static const int POLL_MODULE_9_LOW_TEMP = 0x01B3;
  static const int POLL_MODULE_10_LOWEST_MV_NUMBER = 0x01B4;
  static const int POLL_MODULE_10_LOWEST_CELL_MV = 0x01B5;
  static const int POLL_MODULE_10_HIGHEST_MV_NUMBER = 0x01B6;
  static const int POLL_MODULE_10_HIGH_CELL_MV = 0x01B7;
  static const int POLL_MODULE_10_HIGH_TEMP = 0x01B9;
  static const int POLL_MODULE_10_LOW_TEMP = 0x01BB;
  static const int POLL_FOR_ORIGINAL_CALIBRATION = 0x1FFE;
  static const int POLL_FOR_CURRENT_CALIBRATION = 0x1FFC;

  static const uint16_t MAX_CELL_DEVIATION_MV = 230;
  static const uint16_t MAX_CELL_VOLTAGE_MV = 3650;  //Charging stops if one cell exceeds this value
  static const uint16_t MIN_CELL_VOLTAGE_MV = 2800;  //Discharging stops if one cell goes below this value

  uint16_t rampdown_power = 0;
  uint16_t poll_state = POLL_FOR_ORIGINAL_CALIBRATION;
  uint16_t pid_reply = 0;
  uint16_t battery_voltage = 0;                  // Whole volts from 0x444, used for the 0x441 link voltage
  uint16_t battery_voltage_dV = 0;               // Deci-volts from 0x438, primary pack voltage
  uint16_t battery_insulation_ohm_per_volt = 0;  // 0x43A, multiply by pack voltage for Ohms
  uint16_t battery_highprecision_SOC = 0;
  uint16_t battery_estimated_SOC = 0;
  uint16_t BMS_SOC = 0;
  uint16_t BMS_lowest_cell_voltage_mV = 3300;
  uint16_t BMS_highest_cell_voltage_mV = 3300;
  uint16_t BMS_allowed_charge_power = 0;
  uint16_t BMS_charge_times = 0;
  uint16_t BMS_allowed_discharge_power = 0;
  uint16_t BMS_total_charged_ah = 0;
  uint16_t BMS_total_discharged_ah = 0;
  uint16_t BMS_total_charged_kwh = 0;
  uint16_t BMS_total_discharged_kwh = 0;
  uint16_t BMS_times_full_power = 0;
  uint16_t BMS_capacity_original_calibration = 0;
  uint16_t BMC_SOC_original_calibration = 0;
  uint16_t BMS_capacity_current_calibration = 0;
  uint16_t BMC_SOC_current_calibration = 0;
  uint16_t seed = 0;
  uint16_t solvedKey = 0;

  int16_t battery_temperature_ambient = 0;
  int16_t battery_calc_min_temperature = 0;
  int16_t battery_calc_max_temperature = 0;
  int16_t battery_current_dA = 0;  // 0x444, deci-amps, negative while charging
  int16_t BMS_lowest_cell_temperature = 0;
  int16_t BMS_highest_cell_temperature = 0;
  int16_t BMS_average_cell_temperature = 0;

  static const uint8_t NOT_DETERMINED_YET = 0;
  static const uint8_t STANDARD_RANGE = 1;
  static const uint8_t EXTENDED_RANGE = 2;
  static const uint8_t NOT_RUNNING = 0xFF;
  static const uint8_t STARTED = 0;
  static const uint8_t RUNNING_STEP_1 = 1;
  static const uint8_t RUNNING_STEP_2 = 2;
  static const uint8_t RUNNING_STEP_3 = 3;
  static const uint8_t RUNNING_STEP_4 = 4;
  uint8_t battery_type = NOT_DETERMINED_YET;
  uint8_t stateMachineClearCrash = NOT_RUNNING;
  uint8_t stateMachineCalibrateSOC = NOT_RUNNING;

  // Isolation monitor routine (RoutineControl 0x2008); shares the 0x7E7 session with SOC cal.
  uint8_t stateMachineIsoRoutine = NOT_RUNNING;
  uint8_t isoRoutineAction = 0;  // 1 disable (31 01), 2 enable (31 02)
  uint8_t increaseTimeoutIso = 0;
  // keep_iso_disabled enforcement: re-send disable after each BMS start (monitor re-enables on power-up)
  bool bms_was_alive = false;
  bool iso_reassert_needed = false;
  unsigned long bms_alive_since_ms = 0;
  unsigned long iso_reassert_attempt_ms = 0;

  // DTC readout: request 0x19 02 09, reassemble the 0x59 02 ISO-TP reply, parse 4 bytes per DTC.
  static const int MAX_DTC_COUNT = 30;
  uint8_t stateMachineReadDTC = NOT_RUNNING;
  uint8_t stateMachineEraseDTC = NOT_RUNNING;
  unsigned long dtc_request_millis = 0;
  uint8_t dtc_buffer[140] = {0};
  uint16_t dtc_rx_expected = 0;
  uint16_t dtc_rx_len = 0;
  bool dtc_rx_active = false;
  void parseDTCResponse();

  /* Software contactor control: step the transmitted 0x12D frame through the same payload
  states the real VCU uses (taken from CAN logs of two cars). Byte 6 is a rolling counter,
  byte 7 the 0x441-style checksum over bytes 0-6.
  0x12D states (bytes 0-5):
  - standby:      50 14 02 10 04 31  ignition off, pack open
  - active ack:   50 18 02 20 04 31  ignition off, pack closed (also seen throughout AC charging)
  - close/active: A0 28 02 A0 0C 71  BMS starts precharge ~60ms later
  - drive ready:  A0 28 00 22 0C 31  car sends this ~0.4s after the main contactor closes
  - shutdown:     A0 28 02 60 04 31  BMS drops HV-active ~0.3s later
  Car power-off: shutdown (~1.4s) -> active ack -> pack opens (instant when driving, waits for
  charging to finish) -> ~2.5s -> standby. The pack never opens on the shutdown step itself.
  Logs show another close pattern (50 18 12 20 44 31) for parked aux/DC-DC closes - not used here.
  0x344 byte 0 reports state, by bit:
  - bit7 0x80 = main contactor closed
  - bit6 0x40 = precharge in progress
  - bit2 0x04 = HV active
  - bit1 0x02 = drive flag (set when idle/driving, clear while charging)
  - bit0 0x01 = charge flag
  Drive close: 02 -> 42 -> 82 -> 86. Charge close: 02 -> 42 -> 82 -> 81. Charge open: 81 -> 80 -> 00.
  In charge mode the BMS ignores the 0x12D shutdown and opens on charge completion instead.
  0x84 (closed, HV, no mode flag) also opens and re-closes fine once byte 7 is correct. */
  static const uint8_t CONTACTORS_CLOSING = 0;             // Close pattern sent, drive-ready transition pending
  static const uint8_t CONTACTORS_ACTIVE = 1;              // Drive-ready pattern, closed and running
  static const uint8_t CONTACTORS_AWAIT_ZERO_CURRENT = 2;  // Open asked for, waiting for current to drop
  static const uint8_t CONTACTORS_OPENING = 3;             // Holding the shutdown pattern (~1.4s like the car)
  static const uint8_t CONTACTORS_STANDBY = 4;             // Standby pattern, contactors open
  static const uint8_t CONTACTORS_OPEN_REQUESTED = 5;      // Active-ack held, waiting for the BMS to open
  static const uint8_t CONTACTORS_OPEN_SETTLE = 6;         // Open confirmed, settling before standby
  static const uint8_t CONTACTORS_BOOT_ESTOP = 7;          // Booted held open (fault/stop/inverter), holding
                                                           // active-ack until 0x344 tells us the pack state

  // 0x344 byte 0 feedback bits
  static const uint8_t BMS_FEEDBACK_MAIN_CLOSED = 0x80;
  static const uint8_t BMS_FEEDBACK_PRECHARGING = 0x40;
  static const uint8_t BMS_FEEDBACK_HV_ACTIVE = 0x04;
  static const uint8_t BMS_FEEDBACK_DRIVE_FLAG = 0x02;
  static const uint8_t BMS_FEEDBACK_CHARGE_FLAG = 0x01;

  static const int16_t OPEN_MAX_CURRENT_dA = 25;           // Open only below 2.5A
  static const uint32_t ZERO_CURRENT_MIN_WAIT_MS = 5000;   // Let the inverter settle before trusting current
  static const uint32_t ZERO_CURRENT_TIMEOUT_MS = 10000;   // Force the open if current never drops
  static const uint32_t OPEN_SHUTDOWN_HOLD_MS = 1500;      // Hold the shutdown pattern, like the car
  static const uint32_t OPEN_CONFIRM_TIMEOUT_MS = 6000;    // Warn if the BMS hasn't opened by now
  static const uint32_t OPEN_TO_STANDBY_DELAY_MS = 2500;   // Car's wait between open and standby
  static const uint32_t CLOSE_CONFIRM_TIMEOUT_MS = 15000;  // Warn if the BMS hasn't closed by now

  uint8_t contactorState = CONTACTORS_CLOSING;  // Boot default: close right away, as before
  uint8_t contactor_feedback = 0;               // Raw 0x344 byte 0
  unsigned long contactorStateEntryMillis = 0;
  unsigned long closeConfirmStartMillis = 0;
  unsigned long lastCurrentSampleMillis = 0;
  unsigned long lastContactorFeedbackMillis = 0;  // 0 = no 0x344 received yet
  bool closeConfirmPending = false;               // Only for user closes, not the boot default
  bool openTimeoutEventSent = false;              // Open-delay warning fired once per attempt
  bool requestContactorOpen = false;
  bool requestContactorClose = false;
  bool previousContactorsAllowedClosed = false;  // Combined fault + equipment-stop + inverter-permission state
  bool contactorControlInitialized = false;

  void set_12D_payload(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5);
  void handle_contactor_control(unsigned long currentMillis);

  uint8_t counter_50ms = 0;
  uint8_t counter_100ms = 0;
  uint8_t frame6_counter = 0xB;
  uint8_t BMS_SOH = 99;
  uint8_t BMS_min_cell_voltage_number = 0;
  uint8_t BMS_min_temp_module_number = 0;
  uint8_t BMS_max_cell_voltage_number = 0;
  uint8_t BMS_max_temp_module_number = 0;
  uint8_t battery_frame_index = 0;
  uint8_t discharge_status = 14;
  uint8_t increaseTimeoutSOC = 0;
  static const uint8_t REJECTED = 1;
  static const uint8_t APPROVED = 2;
  uint8_t servicemode = NOT_DETERMINED_YET;
  uint8_t secondsSinceStartup = 0;

  bool BMS_voltage_available = false;
  bool battery_insulation_valid = false;        // Zero is a valid 0x43A fault reading, so track receipt separately
  bool battery_iso_measurement_active = false;  // 0x35E b0 bit0x80
  unsigned long last_35E_ms = 0;                // 0 = 0x35E not yet received (staleness)
  bool calibrationAH_seeded = false;

  int16_t battery_daughterboard_temperatures[13] = {-40, -40, -40, -40, -40, -40, -40, -40, -40, -40, -40, -40, -40};
  uint16_t battery_cellvoltages[MAX_AMOUNT_CELLS] = {0};

  /* Extra CAN info 
  - 0x40D supposedly has vehicle model in frame1
  - 0x2B6 contains date in frame0-6
  - 
  
    */

  /*12D 
  - Byte0(bits7:6) = IG1 Relay state
  - Byte1(bits3:2) = IG3 Relay state
  - Byte1(bits5:4) = IG4 Relay state*/
  CAN_frame ATTO_3_12D = {.FD = false,
                          .ext_ID = false,
                          .DLC = 8,
                          .ID = 0x12D,
                          .data = {0xA0, 0x28, 0x02, 0xA0, 0x0C, 0x71, 0xCF, 0x49}};
  CAN_frame ATTO_3_441 = {.FD = false,
                          .ext_ID = false,
                          .DLC = 8,
                          .ID = 0x441,
                          .data = {0x98, 0x3A, 0x88, 0x13, 0x07, 0x00, 0xFF, 0x8C}};
  CAN_frame ATTO_3_7E7_POLL = {.FD = false,
                               .ext_ID = false,
                               .DLC = 8,
                               .ID = 0x7E7,  //Poll PID 03 22 1F FE (POLL_FOR_ORIGINAL_CALIBRATION)
                               .data = {0x03, 0x22, 0x1F, 0xFE, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame ATTO_3_7E7_ACK = {.FD = false,
                              .ext_ID = false,
                              .DLC = 8,
                              .ID = 0x7E7,  //ACK frame for long PIDs
                              .data = {0x30, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame ATTO_3_7E7_CLEAR_CRASH = {.FD = false,
                                      .ext_ID = false,
                                      .DLC = 8,
                                      .ID = 0x7E7,
                                      .data = {0x02, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame ATTO_3_7E7_RESET_SOC = {.FD = false,
                                    .ext_ID = false,
                                    .DLC = 8,
                                    .ID = 0x7E7,  //This sets SOC to 100.00% (0x27 10) , and AH to 150.00 (0x3A 98)
                                    .data = {0x07, 0x2E, 0x1F, 0xFC, 0x10, 0x27, 0x98, 0x3A}};
  CAN_frame ATTO_3_7E7_READ_DTC = {.FD = false,
                                   .ext_ID = false,
                                   .DLC = 8,
                                   .ID = 0x7E7,  //ReadDTCInformation, reportDTCByStatusMask, mask 0x09
                                   .data = {0x03, 0x19, 0x02, 0x09, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame ATTO_3_7E7_DTC_FC = {.FD = false,
                                 .ext_ID = false,
                                 .DLC = 8,
                                 .ID = 0x7E7,  //Flow control for the DTC reply, BS 0 (send all), STmin 5ms
                                 .data = {0x30, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00}};

  void handle_auto_soc_calibration(bool crit_taper, uint32_t dt_ms, uint32_t now_ms);
};

#endif
