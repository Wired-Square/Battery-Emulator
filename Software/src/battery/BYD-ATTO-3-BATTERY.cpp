#include "BYD-ATTO-3-BATTERY.h"
#include <Arduino.h>  //For millis()
#include <cstring>    //For unit test
#include "../communication/can/comm_can.h"
#include "../communication/contactorcontrol/comm_contactorcontrol.h"
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "../devboard/utils/events.h"
#include "../devboard/utils/logging.h"

// BYD UDS 0x27 Seed-to-Key Algorithm (Endian-Safe)
uint16_t byd_generate_key(uint16_t seed, uint32_t keyK) {
  // Step 1: XOR mixing
  // By keeping everything in standard integer variables,
  // bitwise shifts act on the logical value, ignoring hardware endianness.
  uint32_t a = seed ^ (seed >> 1);
  uint32_t b = keyK ^ (seed >> 2);

  // Step 2: Calculate the result
  uint32_t result = b ^ (a << 3);

  // Step 3: Return the lower 16 bits
  return (uint16_t)(result & 0xFFFF);
}

// Inverted-sum checksum over bytes 0-6, used as byte 7 of both 0x441 and 0x12D
uint8_t computeBydChecksum(const uint8_t* u8) {
  int sum = 0;
  for (int i = 0; i < 7; ++i) {
    sum += u8[i];
  }
  uint8_t lsb = static_cast<uint8_t>(sum & 0xFF);
  return static_cast<uint8_t>(~lsb & 0xFF);
}

void BydAttoBattery::set_12D_payload(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5) {
  ATTO_3_12D.data.u8[0] = b0;
  ATTO_3_12D.data.u8[1] = b1;
  ATTO_3_12D.data.u8[2] = b2;
  ATTO_3_12D.data.u8[3] = b3;
  ATTO_3_12D.data.u8[4] = b4;
  ATTO_3_12D.data.u8[5] = b5;
}

void BydAttoBattery::
    update_values() {  //This function maps all the values fetched via CAN to the correct parameters used for modbus

  if (battery_voltage_dV > 0) {
    datalayer_battery->status.voltage_dV = battery_voltage_dV;  //0x438, 0.1V resolution, prioritized
  } else if (battery_voltage > 0) {
    datalayer_battery->status.voltage_dV = battery_voltage * 10;  //0x444 whole-volt fallback
  }

  // We assume pack is not crashed, and use periodically transmitted SOC
  datalayer_battery->status.real_soc = battery_highprecision_SOC * 10;

  datalayer_battery->status.soh_pptt = BMS_SOH * 100;

  datalayer_battery->status.current_dA = -battery_current_dA;

  datalayer_battery->status.remaining_capacity_Wh = static_cast<uint32_t>(
      (static_cast<double>(datalayer_battery->status.real_soc) / 10000) * datalayer_battery->info.total_capacity_Wh);

  datalayer_battery->status.max_discharge_power_W = BMS_allowed_discharge_power * 100;

  datalayer_battery->status.max_charge_power_W = BMS_allowed_charge_power * 100;

  datalayer_battery->status.cell_max_voltage_mV = BMS_highest_cell_voltage_mV;

  datalayer_battery->status.cell_min_voltage_mV = BMS_lowest_cell_voltage_mV;

  // AC-like top-of-charge taper

  // Tune thresholds here
  const uint16_t V_TAPER_START_mV = 3420;  // begin tapering here
  const uint16_t V_TAPER_END_mV = 3500;    // reach tail current by here (stay below hard clamp region)

  const uint16_t D_TAPER_START_mV = 40;  // begin tapering if delta exceeds this
  const uint16_t D_TAPER_END_mV = 80;    // reach tail current by here

  const uint8_t TAIL_CURRENT_dA = 10;  // 1.0A tail (deci-amps). You can set to 1 for 0.1A.

  // Slew limits to make taper gradual
  const uint16_t DOWN_RATE_dA_per_s = 2;  // ramp down at 0.2A/s  (change to 5 for 0.5A/s)
  const uint16_t UP_RATE_dA_per_s = 1;    // ramp up at 0.1A/s

  const uint16_t cell_max_mV = datalayer_battery->status.cell_max_voltage_mV;
  const uint16_t cell_min_mV = datalayer_battery->status.cell_min_voltage_mV;
  const uint16_t delta_mV = (cell_max_mV > cell_min_mV) ? (cell_max_mV - cell_min_mV) : 0;

  // Start from the user manual limit (deci-amps), but don't allow taper to go below tail current.
  uint16_t user_cap_dA = datalayer.battery.settings.max_user_set_charge_dA;
  if (user_cap_dA < TAIL_CURRENT_dA)
    user_cap_dA = TAIL_CURRENT_dA;

  // Compute taper progress 0..1 from voltage and delta; take whichever is "worse".
  auto clamp01 = [](float x) -> float {
    if (x < 0.0f)
      return 0.0f;
    if (x > 1.0f)
      return 1.0f;
    return x;
  };

  float vprog = 0.0f;
  if (cell_max_mV > V_TAPER_START_mV) {
    const uint16_t denom = (V_TAPER_END_mV > V_TAPER_START_mV) ? (V_TAPER_END_mV - V_TAPER_START_mV) : 1;
    vprog = float(cell_max_mV - V_TAPER_START_mV) / float(denom);
  }

  // Gate delta-taper on voltage: only allow cell spread to trigger taper when
  // cell_max is already above V_TAPER_START_mV. This prevents low-SOC spread
  // from incorrectly restricting current.
  float dprog = 0.0f;
  if (cell_max_mV > V_TAPER_START_mV && delta_mV > D_TAPER_START_mV) {
    const uint16_t denom = (D_TAPER_END_mV > D_TAPER_START_mV) ? (D_TAPER_END_mV - D_TAPER_START_mV) : 1;
    dprog = float(delta_mV - D_TAPER_START_mV) / float(denom);
  }

  const float prog = clamp01((vprog > dprog) ? vprog : dprog);

  // Desired current cap (deci-amps): linearly reduce from user_cap -> tail as prog goes 0 -> 1
  uint16_t cap_target_dA = user_cap_dA;
  if (prog > 0.0f) {
    const float span = float(user_cap_dA - TAIL_CURRENT_dA);
    cap_target_dA = uint16_t(float(TAIL_CURRENT_dA) + (1.0f - prog) * span);
    if (cap_target_dA < TAIL_CURRENT_dA)
      cap_target_dA = TAIL_CURRENT_dA;
  }

  // Slew-limit the cap so it changes smoothly over time
  const uint32_t now_ms = (uint32_t)millis64();
  if (!taper_initialized) {
    taper_last_ms = now_ms;
    cap_slewed_dA = user_cap_dA;  // seed slewer at full current, not zero
    taper_initialized = true;
  }

  uint32_t dt_ms = now_ms - taper_last_ms;
  taper_last_ms = now_ms;
  if (dt_ms == 0)
    dt_ms = 1;

  uint32_t down_step = (uint32_t)DOWN_RATE_dA_per_s * dt_ms / 1000;
  uint32_t up_step = (uint32_t)UP_RATE_dA_per_s * dt_ms / 1000;
  if (down_step < 1)
    down_step = 1;
  if (up_step < 1)
    up_step = 1;

  if (cap_target_dA < cap_slewed_dA) {
    const uint16_t diff = cap_slewed_dA - cap_target_dA;
    const uint16_t step = (down_step >= diff) ? diff : (uint16_t)down_step;
    cap_slewed_dA -= step;
  } else if (cap_target_dA > cap_slewed_dA) {
    const uint16_t diff = cap_target_dA - cap_slewed_dA;
    const uint16_t step = (up_step >= diff) ? diff : (uint16_t)up_step;
    cap_slewed_dA += step;
  }

  const bool crit_taper = (prog >= 0.95f && cap_slewed_dA <= TAIL_CURRENT_dA);
  handle_auto_soc_calibration(crit_taper, dt_ms, now_ms);

  // Convert current cap (dA) -> power cap (W): P = I(dA) * V(dV) / 100
  const uint32_t power_cap_W = (uint32_t(cap_slewed_dA) * uint32_t(datalayer_battery->status.voltage_dV)) / 100;

  // Apply taper by capping the allowed charge power reported to the rest of BE/inverter logic
  if (datalayer_battery->status.max_charge_power_W > power_cap_W) {
    datalayer_battery->status.max_charge_power_W = power_cap_W;
  }
  // End taper

  // Hold power at zero until the pack confirms closed (0x344 bit7), and while opening/idle
  if (!(contactor_feedback & BMS_FEEDBACK_MAIN_CLOSED) ||
      (contactorState != CONTACTORS_CLOSING && contactorState != CONTACTORS_ACTIVE)) {
    datalayer_battery->status.max_charge_power_W = 0;
    datalayer_battery->status.max_discharge_power_W = 0;
  }

  // Pack-internal contactors: DC bus is live once the pack confirms the main contactor
  // closed (same 0x344 bit7 feedback used to gate power above). Guarded so the GPIO
  // contactor state machine stays authoritative when enabled.
  if (!contactor_control_enabled) {
    datalayer.system.status.dc_bus_live = (contactor_feedback & BMS_FEEDBACK_MAIN_CLOSED) != 0;
  }

  datalayer_battery->status.total_discharged_battery_Wh = BMS_total_discharged_kwh * 1000;
  datalayer_battery->status.total_charged_battery_Wh = BMS_total_charged_kwh * 1000;

  // Count detected cells based on which cell voltage readings are nonzero, up to the max supported by datalayer
  for (uint8_t cell_num = 0; cell_num < MAX_AMOUNT_CELLS; cell_num++) {
    if (battery_cellvoltages[cell_num] > 0) {
      datalayer_battery->info.number_of_cells = cell_num + 1;
    } else {
      break;  // Stop counting at the first zero reading, assuming cells are numbered sequentially from 1
    }
  }

  //Map all cell voltages to the global array
  if (datalayer_battery->info.number_of_cells < MAX_AMOUNT_CELLS) {  //Sanity check
    memcpy(datalayer_battery->status.cell_voltages_mV, battery_cellvoltages,
           datalayer_battery->info.number_of_cells * sizeof(uint16_t));
  }

  //After some time has passed after startup, we assume we have read all cellvoltages at least once, so we can trust the cell count and calculated design voltage limits.
  //Before that, we keep the limits wide to avoid cutting off the battery in case cell count is not read correctly yet.
  if (secondsSinceStartup > 120) {
    //Based on the number of cells, calculate the max and min design voltage of the pack.
    if (datalayer_battery->info.number_of_cells >
        80) {  //Sanity check to avoid setting wrong limits in case cell count is not read correctly
      datalayer_battery->info.max_design_voltage_dV =
          (datalayer_battery->info.number_of_cells * MAX_CELL_VOLTAGE_MV) / 100;
      datalayer_battery->info.min_design_voltage_dV =
          (datalayer_battery->info.number_of_cells * MIN_CELL_VOLTAGE_MV) / 100;
    }
  } else {
    secondsSinceStartup++;
  }

  if ((BMS_lowest_cell_temperature != 0) && (BMS_highest_cell_temperature != 0)) {
    //Avoid triggering high delta if only one of the values is available
    datalayer_battery->status.temperature_min_dC = BMS_lowest_cell_temperature * 10;
    datalayer_battery->status.temperature_max_dC = BMS_highest_cell_temperature * 10;
  }

  if (battery_insulation_valid) {
    // BMS reports insulation in Ohm per Volt of pack voltage. Convert to kOhm.
    datalayer_battery->status.insulation_resistance_kOhm =
        (uint16_t)(((uint32_t)battery_insulation_ohm_per_volt * datalayer_battery->status.voltage_dV) / 10000u);
    datalayer_battery->status.insulation_resistance_available = true;
  }

  // Update webserver datalayer
  if (datalayer_bydatto) {
    datalayer_bydatto->SOC_highprec = battery_highprecision_SOC;
    datalayer_bydatto->SOC_polled = BMS_SOC;
    datalayer_bydatto->pack_voltage_dV = battery_voltage_dV;
    datalayer_bydatto->insulation_ohm_per_volt = battery_insulation_ohm_per_volt;
    datalayer_bydatto->insulation_valid = battery_insulation_valid;
    datalayer_bydatto->iso_status_valid = (last_35E_ms != 0) && ((millis() - last_35E_ms) < 3000);
    datalayer_bydatto->iso_measurement_active = battery_iso_measurement_active;
    datalayer_bydatto->battery_temperatures[0] = battery_daughterboard_temperatures[0];
    datalayer_bydatto->battery_temperatures[1] = battery_daughterboard_temperatures[1];
    datalayer_bydatto->battery_temperatures[2] = battery_daughterboard_temperatures[2];
    datalayer_bydatto->battery_temperatures[3] = battery_daughterboard_temperatures[3];
    datalayer_bydatto->battery_temperatures[4] = battery_daughterboard_temperatures[4];
    datalayer_bydatto->battery_temperatures[5] = battery_daughterboard_temperatures[5];
    datalayer_bydatto->battery_temperatures[6] = battery_daughterboard_temperatures[6];
    datalayer_bydatto->battery_temperatures[7] = battery_daughterboard_temperatures[7];
    datalayer_bydatto->battery_temperatures[8] = battery_daughterboard_temperatures[8];
    datalayer_bydatto->battery_temperatures[9] = battery_daughterboard_temperatures[9];
    datalayer_bydatto->battery_temperatures[10] = battery_daughterboard_temperatures[10];
    datalayer_bydatto->battery_temperatures[11] = battery_daughterboard_temperatures[11];
    datalayer_bydatto->battery_temperatures[12] = battery_daughterboard_temperatures[12];
    datalayer_bydatto->BMS_capacity_original_calibration = BMS_capacity_original_calibration;
    datalayer_bydatto->BMC_SOC_original_calibration = BMC_SOC_original_calibration;
    datalayer_bydatto->BMS_capacity_current_calibration = BMS_capacity_current_calibration;
    datalayer_bydatto->BMC_SOC_current_calibration = BMC_SOC_current_calibration;
    // Pre-fill from BMS on first read so the web page shows real pack AH, not the 150AH default
    if (!calibrationAH_seeded && BMS_capacity_current_calibration > 0) {
      datalayer_bydatto->calibrationTargetAH = BMS_capacity_current_calibration / 100;
      calibrationAH_seeded = true;
    }
    datalayer_bydatto->chargePower = BMS_allowed_charge_power;
    datalayer_bydatto->charge_times = BMS_charge_times;
    datalayer_bydatto->dischargePower = BMS_allowed_discharge_power;
    datalayer_bydatto->total_charged_ah = BMS_total_charged_ah;
    datalayer_bydatto->total_discharged_ah = BMS_total_discharged_ah;
    datalayer_bydatto->total_charged_kwh = BMS_total_charged_kwh;
    datalayer_bydatto->total_discharged_kwh = BMS_total_discharged_kwh;
    datalayer_bydatto->times_full_power = BMS_times_full_power;
    datalayer_bydatto->BMS_min_cell_voltage_number = BMS_min_cell_voltage_number;
    datalayer_bydatto->BMS_min_temp_module_number = BMS_min_temp_module_number;
    datalayer_bydatto->BMS_max_cell_voltage_number = BMS_max_cell_voltage_number;
    datalayer_bydatto->BMS_max_temp_module_number = BMS_max_temp_module_number;
    datalayer_bydatto->discharge_status = discharge_status;
    datalayer_bydatto->seed = seed;
    datalayer_bydatto->solvedKey = solvedKey;
    datalayer_bydatto->servicemode = servicemode;
    datalayer_bydatto->contactor_control_state = contactorState;
    datalayer_bydatto->contactor_feedback = contactor_feedback;
    datalayer_bydatto->contactor_main_closed = (contactor_feedback & BMS_FEEDBACK_MAIN_CLOSED) != 0;
    datalayer_bydatto->contactor_precharging = (contactor_feedback & BMS_FEEDBACK_PRECHARGING) != 0;
    datalayer_bydatto->contactor_hv_active = (contactor_feedback & BMS_FEEDBACK_HV_ACTIVE) != 0;
    datalayer_bydatto->contactor_drive_flag = (contactor_feedback & BMS_FEEDBACK_DRIVE_FLAG) != 0;
    datalayer_bydatto->contactor_charge_flag = (contactor_feedback & BMS_FEEDBACK_CHARGE_FLAG) != 0;

    // Update requests from webserver datalayer. All 0x7E7 diagnostics share the request/reply IDs,
    // so only one may run at a time; DTC reception counts as busy too.
    bool diag_busy = (stateMachineClearCrash != NOT_RUNNING) || (stateMachineCalibrateSOC != NOT_RUNNING) ||
                     (stateMachineReadDTC != NOT_RUNNING) || (stateMachineEraseDTC != NOT_RUNNING) ||
                     (stateMachineIsoRoutine != NOT_RUNNING) || dtc_rx_active ||
                     datalayer_bydatto->dtc_read_in_progress || cell_balance_time_requested.load();

    if (datalayer_bydatto->UserRequestCrashReset && !diag_busy) {
      stateMachineClearCrash = STARTED;
      datalayer_bydatto->UserRequestCrashReset = false;
      diag_busy = true;
    }

    if (datalayer_bydatto->UserRequestCalibrateSOC && !diag_busy) {
      stateMachineCalibrateSOC = STARTED;
      datalayer_bydatto->UserRequestCalibrateSOC = false;
      diag_busy = true;
    }

    if (datalayer_bydatto->UserRequestDTCreadout && !diag_busy) {
      stateMachineReadDTC = STARTED;
      datalayer_bydatto->dtc_read_in_progress = true;
      datalayer_battery->dtc.dtc_read_failed = false;
      dtc_request_millis = millis();
      datalayer_bydatto->UserRequestDTCreadout = false;
      diag_busy = true;
    }

    if (datalayer_bydatto->UserRequestDTCreset && !diag_busy) {
      stateMachineEraseDTC = STARTED;
      datalayer_bydatto->UserRequestDTCreset = false;
      diag_busy = true;
    }

    if ((datalayer_bydatto->UserRequestIsoRoutineDisable || datalayer_bydatto->UserRequestIsoRoutineEnable) &&
        !diag_busy) {
      isoRoutineAction = datalayer_bydatto->UserRequestIsoRoutineDisable ? 1 : 2;
      datalayer_bydatto->iso_command_status = 1;  // running
      increaseTimeoutIso = 0;
      stateMachineIsoRoutine = STARTED;
      datalayer_bydatto->UserRequestIsoRoutineDisable = false;
      datalayer_bydatto->UserRequestIsoRoutineEnable = false;
      diag_busy = true;
    }

    // keep_iso_disabled: the monitor re-enables on every BMS power-up, so re-send disable after each
    // BMS start (arm on the CAN alive edge, retry until accepted).
    bool bms_alive = (lastContactorFeedbackMillis != 0) && ((millis() - lastContactorFeedbackMillis) < 3000);
    if (bms_alive && !bms_was_alive) {
      bms_alive_since_ms = millis();
      if (datalayer_bydatto->keep_iso_disabled) {
        iso_reassert_needed = true;
        iso_reassert_attempt_ms = 0;
      }
    }
    bms_was_alive = bms_alive;
    if (!datalayer_bydatto->keep_iso_disabled) {
      iso_reassert_needed = false;
    }
    // Disable early and retry fast to beat the BMS's boot-time insulation trip.
    if (iso_reassert_needed && bms_alive && (millis() - bms_alive_since_ms > 1000) &&
        (iso_reassert_attempt_ms == 0 || (millis() - iso_reassert_attempt_ms) > 2000) && !diag_busy) {
      isoRoutineAction = 1;  // disable
      datalayer_bydatto->iso_command_status = 1;
      increaseTimeoutIso = 0;
      stateMachineIsoRoutine = STARTED;
      iso_reassert_attempt_ms = millis();
      diag_busy = true;
    }
    if (iso_reassert_needed && iso_reassert_attempt_ms != 0 && datalayer_bydatto->iso_command_status == 2) {
      iso_reassert_needed = false;  // accepted, monitor disabled until the next BMS restart
    }
    // Fail the read if the BMS never answers
    if (datalayer_bydatto->dtc_read_in_progress && (millis() - dtc_request_millis > 2000)) {
      datalayer_bydatto->dtc_read_in_progress = false;
      datalayer_battery->dtc.dtc_read_failed = true;
      dtc_rx_active = false;
    }
  }
}

void BydAttoBattery::handle_auto_soc_calibration(bool crit_taper, uint32_t dt_ms, uint32_t now_ms) {
  if (!datalayer_bydatto)
    return;
  const int16_t current_dA = datalayer_battery->status.current_dA;
  const bool crit_low_current =
      (current_dA >= kAutoCalMinDischargeCurrentDa && current_dA <= kAutoCalMaxChargeCurrentDa);

  if (!crit_taper) {
    autocal_dwell_ms = 0;
    autocal_grace_start_ms = 0;
  } else if (crit_low_current) {
    autocal_grace_start_ms = 0;
    autocal_dwell_ms += dt_ms;
    if (autocal_dwell_ms > kAutoCalDwellRequiredMs)
      autocal_dwell_ms = kAutoCalDwellRequiredMs;
  } else {
    if (autocal_grace_start_ms == 0) {
      autocal_grace_start_ms = now_ms;
    }
    if ((now_ms - autocal_grace_start_ms) >= kAutoCalCurrentGraceMs) {
      autocal_dwell_ms = 0;
      autocal_grace_start_ms = 0;
    }
  }

  const uint64_t now64 = millis64();

  const bool crit_dwell = (autocal_dwell_ms >= kAutoCalDwellRequiredMs);
  const bool crit_drift =
      (battery_highprecision_SOC < 1000 &&
       (1000 - battery_highprecision_SOC) > (uint16_t)(datalayer_bydatto->auto_calibrate_soc_drift_percent * 10));
  const bool crit_cooldown = ((now64 - last_auto_calibrate_ms) > kAutoCalCooldownMs);
  // Only calibrate when the pack itself reports closed, not just when BE permits closing
  const bool crit_contactors = (contactor_feedback & BMS_FEEDBACK_MAIN_CLOSED) != 0;
  uint32_t current_spike_ms = 0;
  if (crit_taper && !crit_low_current && autocal_grace_start_ms != 0) {
    current_spike_ms = now_ms - autocal_grace_start_ms;
  }

  datalayer_bydatto->autocal_crit_taper = crit_taper;
  datalayer_bydatto->autocal_crit_low_current = crit_low_current;
  datalayer_bydatto->autocal_dwell_accumulated_ms = autocal_dwell_ms;
  datalayer_bydatto->autocal_grace_timer_ms = current_spike_ms;
  datalayer_bydatto->autocal_drift_percent =
      (battery_highprecision_SOC < 1000) ? (float)(1000 - battery_highprecision_SOC) / 10.0f : 0.0f;
  datalayer_bydatto->autocal_current_dA = current_dA;
  datalayer_bydatto->autocal_crit_dwell = crit_dwell;
  datalayer_bydatto->autocal_crit_drift = crit_drift;
  datalayer_bydatto->autocal_crit_cooldown_ready = crit_cooldown;
  datalayer_bydatto->autocal_crit_contactors = crit_contactors;

  if (datalayer_bydatto->auto_calibrate_soc_enabled &&
      !datalayer_bydatto->UserRequestCalibrateSOC &&  // don't fight manual request
      stateMachineCalibrateSOC == NOT_RUNNING && crit_contactors && crit_taper && crit_low_current && crit_dwell &&
      crit_drift && crit_cooldown) {

    set_event(EVENT_BYD_AUTO_SOC_CALIBRATION, (uint8_t)((1000 - battery_highprecision_SOC) / 10));

    datalayer_bydatto->calibrationTargetSOC = 100;
    if (BMS_capacity_current_calibration > 0) {  // guard against startup zero
      datalayer_bydatto->calibrationTargetAH = BMS_capacity_current_calibration / 100;
    }
    datalayer_bydatto->UserRequestCalibrateSOC = true;

    last_auto_calibrate_ms = now64;
    autocal_dwell_ms = 0;
  }
}

void BydAttoBattery::handle_incoming_can_frame(CAN_frame rx_frame) {
  switch (rx_frame.ID) {
    case 0x244:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x245:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      if (rx_frame.data.u8[0] == 0x01) {
        battery_temperature_ambient = (rx_frame.data.u8[4] - 40);
      }
      break;
    case 0x286:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x334:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x338:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x344:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      contactor_feedback = rx_frame.data.u8[0];
      lastContactorFeedbackMillis = millis();
      discharge_status = (rx_frame.data.u8[1] & 0x0F);
      break;
    case 0x345:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      if (rx_frame.data.u8[7] == computeBydChecksum(rx_frame.data.u8)) {
        BMS_allowed_discharge_power = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[0];  // 0.1kW, same as DID 0x000E
        BMS_allowed_charge_power = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[2];     // 0.1kW, same as DID 0x000A
      } else {
        datalayer_battery->status.CAN_error_counter++;
      }
      break;
    case 0x347:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x34A:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x35E:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      // b0 bit 0x80 = isolation measurement active (set = running, clear = disabled/idle)
      if (rx_frame.data.u8[7] == computeBydChecksum(rx_frame.data.u8)) {
        battery_iso_measurement_active = (rx_frame.data.u8[0] & 0x80) != 0;
        last_35E_ms = millis();
      } else {
        datalayer_battery->status.CAN_error_counter++;
      }
      break;
    case 0x360:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x36C:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x438:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      if (rx_frame.data.u8[7] == computeBydChecksum(rx_frame.data.u8)) {
        battery_voltage_dV = (rx_frame.data.u8[6] << 8) | rx_frame.data.u8[5];
      } else {
        datalayer_battery->status.CAN_error_counter++;
      }
      break;
    case 0x43A:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      if (rx_frame.data.u8[7] == computeBydChecksum(rx_frame.data.u8)) {
        battery_insulation_ohm_per_volt = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[2];
        battery_insulation_valid = true;
      } else {
        datalayer_battery->status.CAN_error_counter++;
      }
      break;
    case 0x43B:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x43C:
      if (rx_frame.data.u8[0] == 0x00) {  //Mux
        battery_daughterboard_temperatures[0] = (rx_frame.data.u8[1] - 40);
        battery_daughterboard_temperatures[1] = (rx_frame.data.u8[2] - 40);
        battery_daughterboard_temperatures[2] = (rx_frame.data.u8[3] - 40);
        battery_daughterboard_temperatures[3] = (rx_frame.data.u8[4] - 40);
        battery_daughterboard_temperatures[4] = (rx_frame.data.u8[5] - 40);
        battery_daughterboard_temperatures[5] = (rx_frame.data.u8[6] - 40);
      }
      if (rx_frame.data.u8[0] == 0x01) {  //Mux
        //Some packs have unpopulated modules at the end, (0xFF), we dont show those in webserver
        battery_daughterboard_temperatures[6] = (rx_frame.data.u8[1] - 40);
        battery_daughterboard_temperatures[7] = (rx_frame.data.u8[2] - 40);
        battery_daughterboard_temperatures[8] = (rx_frame.data.u8[3] - 40);
        battery_daughterboard_temperatures[9] = (rx_frame.data.u8[4] - 40);
        battery_daughterboard_temperatures[10] = (rx_frame.data.u8[5] - 40);
        battery_daughterboard_temperatures[11] = (rx_frame.data.u8[6] - 40);
      }
      break;
    case 0x43D:  //Cellvoltage monitoring, 54 frames for 160cells
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      battery_frame_index = rx_frame.data.u8[0];

      if (battery_frame_index < (MAX_AMOUNT_CELLS / 3)) {
        uint8_t base_index = battery_frame_index * 3;
        for (uint8_t i = 0; i < 3; i++) {
          uint16_t cell_voltage = (((rx_frame.data.u8[2 * (i + 1)] & 0x0F) << 8) | rx_frame.data.u8[2 * i + 1]);
          if (cell_voltage != 0xFFF) {  //Some packs have unpopulated modules at the end
            battery_cellvoltages[base_index + i] = cell_voltage;
          }
        }
      }
      break;
    case 0x444:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      if (rx_frame.data.u8[7] == computeBydChecksum(rx_frame.data.u8)) {
        battery_voltage = ((rx_frame.data.u8[1] & 0x0F) << 8) | rx_frame.data.u8[0];
        battery_current_dA = (int16_t)(((rx_frame.data.u8[3] << 8) | rx_frame.data.u8[2]) - 5000);
        lastCurrentSampleMillis = millis();
        BMS_SOH = rx_frame.data.u8[4];
        BMS_SOC = rx_frame.data.u8[5];  // Whole-percent SOC, same basis as DID 0x0005 (not the 0x447 basis)
        BMS_voltage_available = true;
      } else {
        datalayer_battery->status.CAN_error_counter++;
      }
      break;
    case 0x445:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x446:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      // Broadcast cell extrema summary (~1Hz), fresher than the UDS round-robin. b0/b4 = 1-based
      // min/max cell number; min/max mV are 12-bit little-endian in b1:b2 / b5:b6.
      BMS_min_cell_voltage_number = rx_frame.data.u8[0];
      BMS_lowest_cell_voltage_mV = rx_frame.data.u8[1] | ((rx_frame.data.u8[2] & 0x0F) << 8);
      BMS_max_cell_voltage_number = rx_frame.data.u8[4];
      BMS_highest_cell_voltage_mV = rx_frame.data.u8[5] | ((rx_frame.data.u8[6] & 0x0F) << 8);
      break;
    case 0x447:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      if (rx_frame.data.u8[7] == computeBydChecksum(rx_frame.data.u8)) {
        BMS_min_temp_module_number = rx_frame.data.u8[0];  // 1-based coldest sensor
        BMS_lowest_cell_temperature = (rx_frame.data.u8[1] - 40);
        BMS_max_temp_module_number = rx_frame.data.u8[2];  // 1-based hottest sensor
        BMS_highest_cell_temperature = (rx_frame.data.u8[3] - 40);
        battery_highprecision_SOC = ((rx_frame.data.u8[5] & 0x0F) << 8) | rx_frame.data.u8[4];  // 03 E0 = 992 = 99.2%
        BMS_average_cell_temperature = (rx_frame.data.u8[6] - 40);
      } else {
        datalayer_battery->status.CAN_error_counter++;
      }
      break;
    case 0x47B:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x524:
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x7EF:  //OBD2 PID reply from battery
      // DTC reply (0x19 0x02 -> 0x59 0x02) is multi-frame. Reassemble it, ACK the first frame with
      // our own flow control, and parse once complete. Done before the single-frame PID parsing so
      // the consecutive frames aren't mistaken for PIDs.
      if (rx_frame.data.u8[0] == 0x10 && rx_frame.data.u8[2] == 0x59 && rx_frame.data.u8[3] == 0x02) {
        dtc_rx_expected = ((rx_frame.data.u8[0] & 0x0F) << 8) | rx_frame.data.u8[1];
        dtc_rx_len = 0;
        for (uint8_t i = 2; i < 8 && dtc_rx_len < sizeof(dtc_buffer); i++) {
          dtc_buffer[dtc_rx_len++] = rx_frame.data.u8[i];
        }
        dtc_rx_active = true;
        transmit_can_frame(&ATTO_3_7E7_DTC_FC);
        break;
      }
      if (dtc_rx_active && (rx_frame.data.u8[0] & 0xF0) == 0x20) {
        for (uint8_t i = 1; i < 8 && dtc_rx_len < sizeof(dtc_buffer); i++) {
          dtc_buffer[dtc_rx_len++] = rx_frame.data.u8[i];
        }
        if (dtc_rx_len >= dtc_rx_expected) {
          dtc_rx_len = dtc_rx_expected;  // Drop ISO-TP padding so it isn't parsed as a DTC
          parseDTCResponse();
          dtc_rx_active = false;
        }
        break;
      }
      if ((rx_frame.data.u8[0] == 0x04) && (rx_frame.data.u8[1] == 0x67) && (rx_frame.data.u8[2] == 0x01)) {
        seed = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        solvedKey = byd_generate_key(seed, 0x63);  //For now key can be either 0xbd or 0x63, 50/50 of guessing right
      }
      if ((rx_frame.data.u8[0] == 0x03) && (rx_frame.data.u8[1] == 0x7F) &&
          !(awaiting_cell_balance_reply() && rx_frame.data.u8[2] == 0x22)) {
        servicemode = REJECTED;
      }
      if ((rx_frame.data.u8[0] == 0x02) && (rx_frame.data.u8[1] == 0x67) && (rx_frame.data.u8[2] == 0x02) &&
          (rx_frame.data.u8[3] == 0xAA)) {
        servicemode = APPROVED;
      }

      // ISO routine reply (only while it is running): 71 = accepted; any 7F = rejected (session,
      // security, or routine NRC). Ends the machine on the reply.
      if (stateMachineIsoRoutine != NOT_RUNNING) {
        if (rx_frame.data.u8[1] == 0x71 && rx_frame.data.u8[3] == 0x20 && rx_frame.data.u8[4] == 0x08) {
          datalayer_bydatto->iso_command_status = 2;  // routine accepted
          stateMachineIsoRoutine = NOT_RUNNING;
        } else if (rx_frame.data.u8[1] == 0x7F) {
          datalayer_bydatto->iso_command_status = 3;  // NRC
          stateMachineIsoRoutine = NOT_RUNNING;
        }
      }

      if (rx_frame.data.u8[0] == 0x10) {
        transmit_can_frame(&ATTO_3_7E7_ACK);  //Send next line request
      }

      if (handle_cell_balance_time_reply(rx_frame)) {
        break;
      }
      pid_reply = ((rx_frame.data.u8[2] << 8) | rx_frame.data.u8[3]);
      switch (pid_reply) {
        case POLL_FOR_ORIGINAL_CALIBRATION:
          BMS_capacity_original_calibration = (rx_frame.data.u8[7] << 8) | rx_frame.data.u8[6];
          BMC_SOC_original_calibration = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        case POLL_FOR_CURRENT_CALIBRATION:
          BMS_capacity_current_calibration = (rx_frame.data.u8[7] << 8) | rx_frame.data.u8[6];
          BMC_SOC_current_calibration = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        case POLL_CHARGE_TIMES:
          BMS_charge_times = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        case POLL_TOTAL_CHARGED_AH:
          BMS_total_charged_ah = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        case POLL_TOTAL_DISCHARGED_AH:
          BMS_total_discharged_ah = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        case POLL_TOTAL_CHARGED_KWH:
          BMS_total_charged_kwh = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        case POLL_TOTAL_DISCHARGED_KWH:
          BMS_total_discharged_kwh = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        case POLL_TIMES_FULL_POWER:
          BMS_times_full_power = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[4];
          break;
        default:  //Unrecognized reply
          break;
      }
      break;
    default:
      break;
  }
}

// Parse a reassembled UDS ReadDTCInformation (0x59 0x02) reply: 3 header bytes (59 02 mask) then
// 4 bytes per DTC (3-byte code + status). Stores raw codes; HTML decodes them to strings.
void BydAttoBattery::parseDTCResponse() {
  if (dtc_buffer[0] != 0x59 || dtc_buffer[1] != 0x02) {
    datalayer_battery->dtc.dtc_read_failed = true;
    datalayer_bydatto->dtc_read_in_progress = false;
    return;
  }
  uint8_t count = 0;
  for (uint16_t off = 3; (off + 4 <= dtc_rx_len) && (count < MAX_DTC_COUNT); off += 4) {
    uint32_t code =
        ((uint32_t)dtc_buffer[off] << 16) | ((uint32_t)dtc_buffer[off + 1] << 8) | (uint32_t)dtc_buffer[off + 2];
    uint8_t status = dtc_buffer[off + 3];
    if (code == 0 || status == 0) {  // Empty slot or cleared DTC
      continue;
    }
    datalayer_battery->dtc.dtc_codes[count] = code;
    datalayer_battery->dtc.dtc_status[count] = status;
    count++;
  }
  // Display order: active (status bit0) first, then ascending code.
  uint32_t* codes = datalayer_battery->dtc.dtc_codes;
  uint8_t* sts = datalayer_battery->dtc.dtc_status;
  for (uint8_t a = 1; a < count; a++) {
    uint32_t c = codes[a];
    uint8_t s = sts[a];
    int b = a - 1;
    while (b >= 0) {
      bool keyActive = (s & 0x01);
      bool bActive = (sts[b] & 0x01);
      bool keyFirst = (keyActive != bActive) ? keyActive : (c < codes[b]);
      if (!keyFirst) {
        break;
      }
      codes[b + 1] = codes[b];
      sts[b + 1] = sts[b];
      b--;
    }
    codes[b + 1] = c;
    sts[b + 1] = s;
  }
  datalayer_battery->dtc.dtc_count = count;
  datalayer_battery->dtc.dtc_last_read_millis = millis();
  datalayer_battery->dtc.dtc_read_failed = false;
  datalayer_bydatto->dtc_read_in_progress = false;
}

// Software contactor state machine. Steps the transmitted 0x12D frame between the vehicle's
// payload states and confirms each move against 0x344 feedback. See the header for the states.
void BydAttoBattery::handle_contactor_control(unsigned long currentMillis) {
  // Hold open on a fault, the equipment stop, or when the inverter withdraws permission (Solax/SMA
  // gate closing until ready). Edge-trigger on the combined signal so the buttons and inverter
  // can't fight; a held-open reason at boot keeps active-ack until 0x344 reports state.
  bool contactorsAllowedClosed = !datalayer.system.info.equipment_stop_active &&
                                 datalayer.system.status.inverter_allows_contactor_closing &&
                                 datalayer.system.status.system_status != FAULT;
  if (!contactorControlInitialized) {
    previousContactorsAllowedClosed = contactorsAllowedClosed;
    contactorControlInitialized = true;
    if (!contactorsAllowedClosed) {
      set_12D_payload(0x50, 0x18, 0x02, 0x20, 0x04, 0x31);  // Active-ack pattern
      contactorState = CONTACTORS_BOOT_ESTOP;
    }
  }
  if (contactorsAllowedClosed != previousContactorsAllowedClosed) {
    previousContactorsAllowedClosed = contactorsAllowedClosed;
    if (!contactorsAllowedClosed) {
      requestContactorOpen = true;
    } else {
      requestContactorClose = true;
    }
  }

  if (requestContactorOpen) {
    requestContactorOpen = false;
    closeConfirmPending = false;
    if (contactorState == CONTACTORS_CLOSING || contactorState == CONTACTORS_ACTIVE) {
      set_event(EVENT_BYD_CONTACTOR_OPEN_REQ, 0);
      if (contactor_feedback & BMS_FEEDBACK_MAIN_CLOSED) {
        // Pack is closed - power is already zeroed (update_values) so the inverter winds down while we wait
        contactorState = CONTACTORS_AWAIT_ZERO_CURRENT;
        contactorStateEntryMillis = currentMillis;
      } else {
        // Asked to open mid-precharge - drop to standby now so the BMS stops closing
        set_12D_payload(0x50, 0x14, 0x02, 0x10, 0x04, 0x31);  // Standby pattern
        contactorState = CONTACTORS_STANDBY;
      }
    }
  }

  if (requestContactorClose) {
    requestContactorClose = false;
    if (datalayer.system.info.equipment_stop_active) {
      // Don't close while the stop is active - releasing the stop is what asks to close
    } else if (contactorState == CONTACTORS_AWAIT_ZERO_CURRENT) {
      // Cancel the pending open (shutdown not sent yet). If the pack already closed, resume the
      // drive-ready hold; if it was still precharging, resume the close so it finishes properly
      set_event(EVENT_BYD_CONTACTOR_CLOSE_REQ, 1);
      if (contactor_feedback & BMS_FEEDBACK_MAIN_CLOSED) {
        set_12D_payload(0xA0, 0x28, 0x00, 0x22, 0x0C, 0x31);  // Drive-ready pattern
        contactorState = CONTACTORS_ACTIVE;
      } else {
        set_12D_payload(0xA0, 0x28, 0x02, 0xA0, 0x0C, 0x71);  // Close/active pattern
        counter_50ms = 0;                                     // Re-run the drive-ready transition
        contactorState = CONTACTORS_CLOSING;
        closeConfirmPending = true;
        closeConfirmStartMillis = currentMillis;
      }
    } else if (contactorState == CONTACTORS_STANDBY || contactorState == CONTACTORS_OPEN_REQUESTED ||
               contactorState == CONTACTORS_OPEN_SETTLE || contactorState == CONTACTORS_BOOT_ESTOP) {
      // Car re-closes straight from the active-ack frame, so allow close from any open state
      set_event(EVENT_BYD_CONTACTOR_CLOSE_REQ, 0);
      set_12D_payload(0xA0, 0x28, 0x02, 0xA0, 0x0C, 0x71);  // Close/active pattern
      counter_50ms = 0;                                     // Re-run the drive-ready transition
      contactorState = CONTACTORS_CLOSING;
      closeConfirmPending = true;
      closeConfirmStartMillis = currentMillis;
    }
  }

  switch (contactorState) {
    case CONTACTORS_CLOSING:
      counter_50ms++;
      // Hold off drive-ready until the pack reports closed (0x344 bit7), keeping the car's ~1.15s min
      if (counter_50ms > 23 && (contactor_feedback & BMS_FEEDBACK_MAIN_CLOSED)) {
        ATTO_3_12D.data.u8[2] = 0x00;  // Goes from 02->00
        ATTO_3_12D.data.u8[3] = 0x22;  // Goes from A0->22
        ATTO_3_12D.data.u8[5] = 0x31;  // Goes from 71->31
        contactorState = CONTACTORS_ACTIVE;
      }
      break;
    case CONTACTORS_ACTIVE:
      break;
    case CONTACTORS_AWAIT_ZERO_CURRENT:
      // Only act on a current reading taken after the settle wait, so a stale low sample
      // from before the request can't authorise opening under load
      if ((int32_t)(lastCurrentSampleMillis - contactorStateEntryMillis) >= (int32_t)ZERO_CURRENT_MIN_WAIT_MS &&
          battery_current_dA > -OPEN_MAX_CURRENT_dA && battery_current_dA < OPEN_MAX_CURRENT_dA) {
        set_12D_payload(0xA0, 0x28, 0x02, 0x60, 0x04, 0x31);  // Shutdown pattern
        contactorState = CONTACTORS_OPENING;
        contactorStateEntryMillis = currentMillis;
      } else if (currentMillis - contactorStateEntryMillis >= ZERO_CURRENT_TIMEOUT_MS) {
        // Timed out - open anyway. Flag whether a fresh reading stayed high (0) or none arrived (1)
        bool had_fresh_sample =
            (int32_t)(lastCurrentSampleMillis - contactorStateEntryMillis) >= (int32_t)ZERO_CURRENT_MIN_WAIT_MS;
        set_event(EVENT_BYD_CONTACTOR_FORCE_OPEN, had_fresh_sample ? 0 : 1);
        set_12D_payload(0xA0, 0x28, 0x02, 0x60, 0x04, 0x31);  // Shutdown pattern
        contactorState = CONTACTORS_OPENING;
        contactorStateEntryMillis = currentMillis;
      }
      break;
    case CONTACTORS_OPENING:
      // Hold shutdown like the car (~1.4s), then the active-ack frame. Pack doesn't open yet
      if (currentMillis - contactorStateEntryMillis >= OPEN_SHUTDOWN_HOLD_MS) {
        set_12D_payload(0x50, 0x18, 0x02, 0x20, 0x04, 0x31);  // Active-ack pattern, ignition off
        contactorState = CONTACTORS_OPEN_REQUESTED;
        contactorStateEntryMillis = currentMillis;
        openTimeoutEventSent = false;
      }
      break;
    case CONTACTORS_OPEN_REQUESTED:
      // Hold until the BMS reports open - check bit7 only since the mode bits vary. Require a
      // frame received since we started holding, so a stale reading can't confirm the open
      if ((int32_t)(lastContactorFeedbackMillis - contactorStateEntryMillis) >= 0 &&
          (contactor_feedback & BMS_FEEDBACK_MAIN_CLOSED) == 0) {
        clear_event(EVENT_BYD_CONTACTOR_MISMATCH);
        contactorState = CONTACTORS_OPEN_SETTLE;
        contactorStateEntryMillis = currentMillis;
      } else if (!openTimeoutEventSent && currentMillis - contactorStateEntryMillis >= OPEN_CONFIRM_TIMEOUT_MS) {
        set_event(EVENT_BYD_CONTACTOR_MISMATCH, 2);  // Flag the delay but keep holding
        openTimeoutEventSent = true;
      }
      break;
    case CONTACTORS_OPEN_SETTLE:
      // Open confirmed, drop to standby after the car's ~2.5s wait
      if (currentMillis - contactorStateEntryMillis >= OPEN_TO_STANDBY_DELAY_MS) {
        set_12D_payload(0x50, 0x14, 0x02, 0x10, 0x04, 0x31);  // Standby pattern
        contactorState = CONTACTORS_STANDBY;
      }
      break;
    case CONTACTORS_STANDBY:
      break;
    case CONTACTORS_BOOT_ESTOP:
      // Booted with a held-open reason (fault/stop/inverter). Once the pack reports in: already
      // open -> standby, still closed -> run the full open sequence
      if (lastContactorFeedbackMillis != 0) {
        if (contactor_feedback & BMS_FEEDBACK_MAIN_CLOSED) {
          set_event(EVENT_BYD_CONTACTOR_OPEN_REQ, 1);
          contactorState = CONTACTORS_AWAIT_ZERO_CURRENT;
          contactorStateEntryMillis = currentMillis;
        } else {
          set_12D_payload(0x50, 0x14, 0x02, 0x10, 0x04, 0x31);  // Standby pattern
          contactorState = CONTACTORS_STANDBY;
        }
      }
      break;
    default:
      break;
  }

  if (closeConfirmPending) {
    // Require a frame received since the close was commanded, not a stale closed reading
    if ((int32_t)(lastContactorFeedbackMillis - closeConfirmStartMillis) >= 0 &&
        (contactor_feedback & BMS_FEEDBACK_MAIN_CLOSED)) {
      clear_event(EVENT_BYD_CONTACTOR_MISMATCH);
      closeConfirmPending = false;
    } else if (currentMillis - closeConfirmStartMillis >= CLOSE_CONFIRM_TIMEOUT_MS) {
      // Never confirmed closed - fall back to standby (open, no power) instead of sitting active
      set_event(EVENT_BYD_CONTACTOR_MISMATCH, 3);
      set_12D_payload(0x50, 0x14, 0x02, 0x10, 0x04, 0x31);  // Standby pattern
      contactorState = CONTACTORS_STANDBY;
      closeConfirmPending = false;
    }
  }
}

bool BydAttoBattery::request_cell_balance_times() {
  if (datalayer_battery->info.number_of_cells == 0) {
    return false;
  }
  bool idle = false;
  return cell_balance_time_requested.compare_exchange_strong(idle, true);
}

void BydAttoBattery::begin_cell_balance_time_scan(unsigned long currentMillis) {
  cell_balance_time_data.begin(min(datalayer_battery->info.number_of_cells, CellSeriesBuffer::kMaxCells));
  cell_balance_time_cell = 0;
  cell_balance_time_retries = 0;
  cell_balance_time_waiting = false;
  cell_balance_time_queued = true;
  cell_balance_time_scan_millis = currentMillis;
}

bool BydAttoBattery::diagnostics_idle() const {
  return stateMachineClearCrash == NOT_RUNNING && stateMachineCalibrateSOC == NOT_RUNNING &&
         stateMachineReadDTC == NOT_RUNNING && stateMachineEraseDTC == NOT_RUNNING &&
         stateMachineIsoRoutine == NOT_RUNNING && !dtc_rx_active && !datalayer_bydatto->dtc_read_in_progress;
}

void BydAttoBattery::finish_cell_balance_time_scan() {
  cell_balance_time_active = false;
  cell_balance_time_waiting = false;
  cell_balance_time_data.finish();
  cell_balance_time_requested.store(false);
}

// Cell timers are little-endian uint16 values from DIDs 0x0040-0x00BD. Returns true when the frame
// belonged to the scan and needs no further parsing.
bool BydAttoBattery::handle_cell_balance_time_reply(const CAN_frame& frame) {
  if (!awaiting_cell_balance_reply()) {
    return false;
  }

  if (frame.data.u8[0] >= 0x05 && frame.data.u8[1] == 0x62) {
    const uint16_t reply_did = (static_cast<uint16_t>(frame.data.u8[2]) << 8) | frame.data.u8[3];
    if (reply_did != CELL_BALANCE_TIME_DID_BASE + cell_balance_time_cell + 1) {
      return false;
    }
    cell_balance_time_data.set(cell_balance_time_cell,
                               frame.data.u8[4] | (static_cast<uint16_t>(frame.data.u8[5]) << 8));
    advance_cell_balance_time_cell();
    return true;
  }

  // A pending response keeps the current DID active; other negative replies mark it unreadable.
  if (frame.data.u8[0] >= 0x03 && frame.data.u8[1] == 0x7F && frame.data.u8[2] == 0x22) {
    if (frame.data.u8[3] == 0x78) {
      cell_balance_time_request_millis = millis();
      return true;
    }
    advance_cell_balance_time_cell();
    return true;
  }

  return false;
}

// Every exit from a DID - answered, refused or timed out - goes through here, so the cursor, the
// retry count and the end-of-scan check cannot drift apart.
void BydAttoBattery::advance_cell_balance_time_cell() {
  cell_balance_time_cell++;
  cell_balance_time_retries = 0;
  cell_balance_time_waiting = false;
  if (cell_balance_time_cell >= cell_balance_time_data.expected()) {
    finish_cell_balance_time_scan();
  }
}

void BydAttoBattery::handle_cell_balance_time_poll(unsigned long currentMillis) {
  if (cell_balance_time_queued) {
    cell_balance_time_queued = false;
    cell_balance_time_active = true;
  }
  if (!cell_balance_time_active) {
    return;
  }
  if (currentMillis - cell_balance_time_scan_millis >= CELL_BALANCE_TIME_SCAN_TIMEOUT_MS) {
    finish_cell_balance_time_scan();
    return;
  }

  if (cell_balance_time_waiting) {
    if (currentMillis - cell_balance_time_request_millis < CELL_BALANCE_TIME_TIMEOUT_MS) {
      return;
    }
    if (cell_balance_time_retries < CELL_BALANCE_TIME_RETRIES) {
      cell_balance_time_retries++;
    } else {
      advance_cell_balance_time_cell();
      if (!cell_balance_time_active) {
        return;
      }
    }
  }

  const uint16_t did = CELL_BALANCE_TIME_DID_BASE + cell_balance_time_cell + 1;
  ATTO_3_7E7_POLL.data.u8[2] = static_cast<uint8_t>(did >> 8);
  ATTO_3_7E7_POLL.data.u8[3] = static_cast<uint8_t>(did);
  transmit_can_frame(&ATTO_3_7E7_POLL);
  cell_balance_time_request_millis = currentMillis;
  cell_balance_time_waiting = true;
}

void BydAttoBattery::write_cell_series(CellSeriesWriter& out) {
  static constexpr CellSeriesInfo kBalanceTime{"balance_hours", TL("Cell balance time"), "h", CellSeriesKind::Counter,
                                               0};
  // Requested but not yet begun: the battery task starts the scan on its next tick.
  const bool queued = cell_balance_time_requested.load() && !cell_balance_time_active;
  cell_balance_time_data.publish(out, kBalanceTime,
                                 queued ? CellSeriesState::Pending : cell_balance_time_data.state());
}

void BydAttoBattery::transmit_can(unsigned long currentMillis) {
  //Send 50ms message
  if (currentMillis - previousMillis50 >= INTERVAL_50_MS) {
    previousMillis50 = currentMillis;

    // Set close contactors to allowed (Useful for crashed packs, started via contactor control thru GPIO)
    if (allows_contactor_closing) {
      if (datalayer.system.status.system_status == ACTIVE) {
        *allows_contactor_closing = true;
      } else {  // Fault state, open contactors!
        *allows_contactor_closing = false;
      }
    }

    handle_contactor_control(currentMillis);

    // Byte 6 = rolling counter (high nibble counts up, low nibble 0xF), byte 7 = checksum
    frame6_counter = (frame6_counter + 1) & 0x0F;
    ATTO_3_12D.data.u8[6] = (0x0F | (frame6_counter << 4));
    ATTO_3_12D.data.u8[7] = computeBydChecksum(ATTO_3_12D.data.u8);

    transmit_can_frame(&ATTO_3_12D);
  }
  // Send 100ms CAN Message
  if (currentMillis - previousMillis100 >= INTERVAL_100_MS) {
    previousMillis100 = currentMillis;

    if (counter_100ms < 100) {
      counter_100ms++;
    }

    if (counter_100ms > 3) {
      // Bytes 4-5 = link voltage; matched to the pack it's the precharge-done signal that closes
      // the contactors. Report the low floating link while open, else we hold close while opening.
      bool report_link_voltage_low = !BMS_voltage_available || contactorState == CONTACTORS_OPEN_SETTLE ||
                                     contactorState == CONTACTORS_STANDBY || contactorState == CONTACTORS_BOOT_ESTOP;
      if (report_link_voltage_low) {
        ATTO_3_441.data.u8[4] = 0x0C;
        ATTO_3_441.data.u8[5] = 0x00;
        ATTO_3_441.data.u8[6] = 0xFF;
        ATTO_3_441.data.u8[7] = 0x87;
      } else {
        ATTO_3_441.data.u8[4] = (uint8_t)(battery_voltage - 1);
        ATTO_3_441.data.u8[5] = ((battery_voltage - 1) >> 8);
        ATTO_3_441.data.u8[6] = 0xFF;
        ATTO_3_441.data.u8[7] = computeBydChecksum(ATTO_3_441.data.u8);
      }
    }

    transmit_can_frame(&ATTO_3_441);

    switch (stateMachineClearCrash) {
      case STARTED:
        // DiagnosticSesssionControl enter extendedDiagnosticSession
        ATTO_3_7E7_CLEAR_CRASH.data = {0x02, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
        transmit_can_frame(&ATTO_3_7E7_CLEAR_CRASH);
        stateMachineClearCrash = RUNNING_STEP_1;
        break;
      case RUNNING_STEP_1:
        ATTO_3_7E7_CLEAR_CRASH.data = {0x04, 0x14, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00};
        transmit_can_frame(&ATTO_3_7E7_CLEAR_CRASH);
        stateMachineClearCrash = RUNNING_STEP_2;
        break;
      case RUNNING_STEP_2:
        ATTO_3_7E7_CLEAR_CRASH.data = {0x03, 0x19, 0x02, 0x09, 0x00, 0x00, 0x00, 0x00};
        transmit_can_frame(&ATTO_3_7E7_CLEAR_CRASH);
        stateMachineClearCrash = NOT_RUNNING;
        break;
      case NOT_RUNNING:
        break;
      default:
        break;
    }
    switch (stateMachineReadDTC) {
      case STARTED:
        transmit_can_frame(&ATTO_3_7E7_READ_DTC);
        stateMachineReadDTC = NOT_RUNNING;
        break;
      default:
        break;
    }
    switch (stateMachineEraseDTC) {
      case STARTED:
        // DiagnosticSessionControl, default session
        ATTO_3_7E7_CLEAR_CRASH.data = {0x02, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
        transmit_can_frame(&ATTO_3_7E7_CLEAR_CRASH);
        stateMachineEraseDTC = RUNNING_STEP_1;
        break;
      case RUNNING_STEP_1:
        // ClearDiagnosticInformation, all groups
        ATTO_3_7E7_CLEAR_CRASH.data = {0x04, 0x14, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00};
        transmit_can_frame(&ATTO_3_7E7_CLEAR_CRASH);
        stateMachineEraseDTC = NOT_RUNNING;
        datalayer_bydatto->UserRequestDTCreadout = true;  // Re-read so the table reflects the cleared state
        break;
      default:
        break;
    }
    switch (stateMachineCalibrateSOC) {
      case STARTED:
        // DiagnosticSesssionControl enter extendedDiagnosticSession
        ATTO_3_7E7_RESET_SOC.data = {0x02, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
        transmit_can_frame(&ATTO_3_7E7_RESET_SOC);
        stateMachineCalibrateSOC = RUNNING_STEP_1;
        break;
      case RUNNING_STEP_1:
        // SecurityAccess requestSeed
        ATTO_3_7E7_RESET_SOC.data = {0x02, 0x27, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
        transmit_can_frame(&ATTO_3_7E7_RESET_SOC);
        stateMachineCalibrateSOC = RUNNING_STEP_2;
        break;
      case RUNNING_STEP_2:
        // SecurityAccess sendKey
        if (solvedKey > 0) {  //Process once we have gotten the solved challenge
          ATTO_3_7E7_RESET_SOC.data = {
              0x04, 0x27, 0x02, (uint8_t)((solvedKey & 0xFF00) >> 8), (uint8_t)(solvedKey & 0x00FF), 0x00, 0x00, 0x00};
          transmit_can_frame(&ATTO_3_7E7_RESET_SOC);
          stateMachineCalibrateSOC = RUNNING_STEP_3;
        } else {
          increaseTimeoutSOC++;
          if (increaseTimeoutSOC > 250) {
            increaseTimeoutSOC = 0;
            stateMachineCalibrateSOC = NOT_RUNNING;
          }
        }
        break;
      case RUNNING_STEP_3:
        // WriteDataByIdentifier dataIdentifier=1F FC (calibrate SOC), data = 10 27 98 3A
        ATTO_3_7E7_RESET_SOC.data = {0x07,
                                     0x2E,
                                     0x1F,
                                     0xFC,
                                     (uint8_t)(datalayer_bydatto->calibrationTargetSOC * 100),
                                     (uint8_t)((datalayer_bydatto->calibrationTargetSOC * 100) >> 8),
                                     (uint8_t)(datalayer_bydatto->calibrationTargetAH * 100),
                                     (uint8_t)((datalayer_bydatto->calibrationTargetAH * 100) >> 8)};
        transmit_can_frame(&ATTO_3_7E7_RESET_SOC);
        stateMachineCalibrateSOC = NOT_RUNNING;
        break;
      case NOT_RUNNING:
        break;
      default:
        break;
    }
    switch (stateMachineIsoRoutine) {
      case STARTED:
        // DiagnosticSessionControl enter extendedDiagnosticSession (10 03)
        solvedKey = 0;  // force a fresh seed before the key is sent
        increaseTimeoutIso = 0;
        ATTO_3_7E7_RESET_SOC.data = {0x02, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
        transmit_can_frame(&ATTO_3_7E7_RESET_SOC);
        stateMachineIsoRoutine = RUNNING_STEP_1;
        break;
      case RUNNING_STEP_1:
        // SecurityAccess requestSeed (27 01)
        ATTO_3_7E7_RESET_SOC.data = {0x02, 0x27, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
        transmit_can_frame(&ATTO_3_7E7_RESET_SOC);
        stateMachineIsoRoutine = RUNNING_STEP_2;
        break;
      case RUNNING_STEP_2:
        // Wait for the seed (RX sets solvedKey), then SecurityAccess sendKey (27 02)
        if (solvedKey > 0) {
          ATTO_3_7E7_RESET_SOC.data = {
              0x04, 0x27, 0x02, (uint8_t)((solvedKey & 0xFF00) >> 8), (uint8_t)(solvedKey & 0x00FF), 0x00, 0x00, 0x00};
          transmit_can_frame(&ATTO_3_7E7_RESET_SOC);
          stateMachineIsoRoutine = RUNNING_STEP_3;
        } else if (++increaseTimeoutIso > 30) {
          datalayer_bydatto->iso_command_status = 4;  // no reply
          stateMachineIsoRoutine = NOT_RUNNING;
        }
        break;
      case RUNNING_STEP_3: {
        // RoutineControl (1 disable -> 31 01, 2 enable -> 31 02)
        uint8_t subfn = (isoRoutineAction == 1) ? 0x01 : 0x02;
        ATTO_3_7E7_RESET_SOC.data = {0x04, 0x31, subfn, 0x20, 0x08, 0x00, 0x00, 0x00};
        transmit_can_frame(&ATTO_3_7E7_RESET_SOC);
        increaseTimeoutIso = 0;
        stateMachineIsoRoutine = RUNNING_STEP_4;  // RX ends the machine on 71/7F
        break;
      }
      case RUNNING_STEP_4:
        // Wait for the routine reply (RX ends the machine); time out so the UI can't stick.
        if (++increaseTimeoutIso > 20) {  // ~2 s at 100 ms/tick
          if (datalayer_bydatto->iso_command_status == 1) {
            datalayer_bydatto->iso_command_status = 4;  // no reply
          }
          stateMachineIsoRoutine = NOT_RUNNING;
        }
        break;
      case NOT_RUNNING:
        break;
      default:
        break;
    }
  }
  // Send 200ms CAN Message
  if (currentMillis - previousMillis200 >= INTERVAL_200_MS) {
    previousMillis200 = currentMillis;

    if (!diagnostics_idle()) {
      return;
    }
    if (cell_balance_time_requested.load() && !cell_balance_time_queued && !cell_balance_time_active) {
      begin_cell_balance_time_scan(currentMillis);
    }
    if (cell_balance_time_queued || cell_balance_time_active) {
      handle_cell_balance_time_poll(currentMillis);
      return;
    }

    switch (poll_state) {
      case POLL_FOR_ORIGINAL_CALIBRATION:
        ATTO_3_7E7_POLL.data.u8[2] = (uint8_t)((POLL_FOR_ORIGINAL_CALIBRATION & 0xFF00) >> 8);
        ATTO_3_7E7_POLL.data.u8[3] = (uint8_t)(POLL_FOR_ORIGINAL_CALIBRATION & 0x00FF);
        poll_state = POLL_FOR_CURRENT_CALIBRATION;
        break;
      case POLL_FOR_CURRENT_CALIBRATION:
        ATTO_3_7E7_POLL.data.u8[2] = (uint8_t)((POLL_FOR_CURRENT_CALIBRATION & 0xFF00) >> 8);
        ATTO_3_7E7_POLL.data.u8[3] = (uint8_t)(POLL_FOR_CURRENT_CALIBRATION & 0x00FF);
        poll_state = POLL_CHARGE_TIMES;
        break;
      case POLL_CHARGE_TIMES:
        ATTO_3_7E7_POLL.data.u8[2] = (uint8_t)((POLL_CHARGE_TIMES & 0xFF00) >> 8);
        ATTO_3_7E7_POLL.data.u8[3] = (uint8_t)(POLL_CHARGE_TIMES & 0x00FF);
        poll_state = POLL_TOTAL_CHARGED_AH;
        break;
      case POLL_TOTAL_CHARGED_AH:
        ATTO_3_7E7_POLL.data.u8[2] = (uint8_t)((POLL_TOTAL_CHARGED_AH & 0xFF00) >> 8);
        ATTO_3_7E7_POLL.data.u8[3] = (uint8_t)(POLL_TOTAL_CHARGED_AH & 0x00FF);
        poll_state = POLL_TOTAL_DISCHARGED_AH;
        break;
      case POLL_TOTAL_DISCHARGED_AH:
        ATTO_3_7E7_POLL.data.u8[2] = (uint8_t)((POLL_TOTAL_DISCHARGED_AH & 0xFF00) >> 8);
        ATTO_3_7E7_POLL.data.u8[3] = (uint8_t)(POLL_TOTAL_DISCHARGED_AH & 0x00FF);
        poll_state = POLL_TOTAL_CHARGED_KWH;
        break;
      case POLL_TOTAL_CHARGED_KWH:
        ATTO_3_7E7_POLL.data.u8[2] = (uint8_t)((POLL_TOTAL_CHARGED_KWH & 0xFF00) >> 8);
        ATTO_3_7E7_POLL.data.u8[3] = (uint8_t)(POLL_TOTAL_CHARGED_KWH & 0x00FF);
        poll_state = POLL_TOTAL_DISCHARGED_KWH;
        break;
      case POLL_TOTAL_DISCHARGED_KWH:
        ATTO_3_7E7_POLL.data.u8[2] = (uint8_t)((POLL_TOTAL_DISCHARGED_KWH & 0xFF00) >> 8);
        ATTO_3_7E7_POLL.data.u8[3] = (uint8_t)(POLL_TOTAL_DISCHARGED_KWH & 0x00FF);
        poll_state = POLL_TIMES_FULL_POWER;
        break;
      case POLL_TIMES_FULL_POWER:
        ATTO_3_7E7_POLL.data.u8[2] = (uint8_t)((POLL_TIMES_FULL_POWER & 0xFF00) >> 8);
        ATTO_3_7E7_POLL.data.u8[3] = (uint8_t)(POLL_TIMES_FULL_POWER & 0x00FF);
        poll_state = POLL_FOR_ORIGINAL_CALIBRATION;
        break;
      default:
        poll_state = POLL_FOR_ORIGINAL_CALIBRATION;
        break;
    }

    transmit_can_frame(&ATTO_3_7E7_POLL);
  }
}

void BydAttoBattery::setup(void) {  // Performs one time setup at startup
  datalayer_battery->info.chemistry = battery_chemistry_enum::LFP;
  datalayer_battery->info.max_design_voltage_dV = 6500;  //Startup in extremes
  datalayer_battery->info.min_design_voltage_dV = 2000;  //We later determine range based on amount of cells
  datalayer_battery->info.max_cell_voltage_mV = MAX_CELL_VOLTAGE_MV;
  datalayer_battery->info.min_cell_voltage_mV = MIN_CELL_VOLTAGE_MV;
  datalayer_battery->info.max_cell_voltage_deviation_mV = MAX_CELL_DEVIATION_MV;
}

void BydAttoBattery::write_advanced_status(AdvancedStatusWriter& out) {
  DATALAYER_INFO_BYDATTO3* byd_datalayer = datalayer_bydatto;
  const String s = (datalayer_bydatto == &datalayer_extended.bydAtto3) ? "" : "2";
  out.section("");

  const auto& dl_bat = *datalayer_battery;
  out.kv(TL("Detected cells"), String(dl_bat.info.number_of_cells));

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
  out.kv(TL("BE contactor state"), contactor_control_state);

  out.kv(TL("Main contactors"), byd_datalayer->contactor_main_closed
                                                  ? "Closed — battery connected"
                                                  : "Open — battery disconnected");
  out.kv(TL("Precharge state"), byd_datalayer->contactor_precharging ? "Active" : "Idle");
  // Bit2 (0x04) = car on/off (clear during car-off AC charging even though HV is live),
  // not literal HV-bus energisation.
  out.kv(TL("HV active"), byd_datalayer->contactor_hv_active ? "Yes" : "No");

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
  out.kv(TL("BMS pack mode"), pack_mode);

  char feedbackStr[5];
  snprintf(feedbackStr, sizeof(feedbackStr), "0x%02X", byd_datalayer->contactor_feedback);
  // 0x344 byte1 low nibble: a BMS state code whose meaning is unconfirmed (reads 1 in
  // idle/drive/discharge alike). byte0 is the real charge/drive truth, so show this raw.
  out.kv(TL("BMS raw status"), "mode " + String(feedbackStr) + ", state " + String(byd_datalayer->discharge_status));

  float soc_measured = static_cast<float>(byd_datalayer->SOC_highprec) * 0.1f;
  float BMS_maxChargePower = static_cast<float>(byd_datalayer->chargePower) * 0.1f;
  float BMS_maxDischargePower = static_cast<float>(byd_datalayer->dischargePower) * 0.1f;

  out.kv(TL("SOC measured"), String(soc_measured), "%");
  out.kv(TL("SOC OBD2"), String(byd_datalayer->SOC_polled), "%");
  if (byd_datalayer->pack_voltage_dV > 0) {
    out.kv(TL("Pack voltage"), String(byd_datalayer->pack_voltage_dV / 10.0f, 1), "V");
  } else {
    out.kv(TL("Pack voltage"), "Not received");
  }

  for (int i = 0; i < 13; i++) {
    if (byd_datalayer->battery_temperatures[i] != 215) {
      out.kv(("Temperature sensor " + String(i + 1)).c_str(),
             String(byd_datalayer->battery_temperatures[i]), "°C");
    }
  }

  out.kv(TL("Max discharge power"), String(BMS_maxDischargePower), "kW");
  out.kv(TL("Max charge (regen) power"), String(BMS_maxChargePower), "kW");
  out.kv(TL("Total charged"), String(byd_datalayer->total_charged_kwh), "kWh");
  out.kv(TL("Total discharged"), String(byd_datalayer->total_discharged_kwh), "kWh");
  out.kv(TL("Total charged (Ah)"), String(byd_datalayer->total_charged_ah), "Ah");
  out.kv(TL("Total discharged (Ah)"), String(byd_datalayer->total_discharged_ah), "Ah");
  out.kv(TL("Charge times"), String(byd_datalayer->charge_times));
  out.kv(TL("Times of full power"), String(byd_datalayer->times_full_power));
  out.kv(TL("Min cell voltage number"), String(byd_datalayer->BMS_min_cell_voltage_number));
  out.kv(TL("Max cell voltage number"), String(byd_datalayer->BMS_max_cell_voltage_number));
  out.kv(TL("Min temp module number"), String(byd_datalayer->BMS_min_temp_module_number));
  out.kv(TL("Max temp module number"), String(byd_datalayer->BMS_max_temp_module_number));
  out.kv(TL("Seed"), String(byd_datalayer->seed));
  out.kv("SolvedKey", String(byd_datalayer->solvedKey));

  if (byd_datalayer->servicemode == 0) {
    out.kv("ServiceMode", "No command ran yet");
  } else if (byd_datalayer->servicemode == 1) {
    out.kv("ServiceMode", "REJECTED");
  } else if (byd_datalayer->servicemode == 2) {
    out.kv("ServiceMode", "APPROVED!");
  }

  out.kv(TL("Capacity original"), String((byd_datalayer->BMS_capacity_original_calibration) / 100), "AH");
  out.kv(TL("Capacity current"), String((byd_datalayer->BMS_capacity_current_calibration) / 100), "AH");
  out.kv(TL("SOC original"), String(byd_datalayer->BMC_SOC_original_calibration), "%");
  out.kv(TL("SOC current"), String(byd_datalayer->BMC_SOC_current_calibration), "%");

  {
    const uint32_t dwell_sec = byd_datalayer->autocal_dwell_accumulated_ms / kMillisPerSecond;
    const float autocal_current_A = static_cast<float>(byd_datalayer->autocal_current_dA) / kDeciPerUnit;
    const char* current_direction = "idle";
    if (byd_datalayer->autocal_current_dA < 0) {
      current_direction = "discharge";
    } else if (byd_datalayer->autocal_current_dA > 0) {
      current_direction = "charge";
    }

    const bool drift_reached =
        byd_datalayer->autocal_drift_percent >= byd_datalayer->auto_calibrate_soc_drift_percent;

    out.section(TL("Auto-calibration status"));
    out.kv(TL("Contactors"), good_if(byd_datalayer->autocal_crit_contactors, "OK", "Open"));
    out.kv(TL("Full / In taper?"), good_if(byd_datalayer->autocal_crit_taper, "Yes", "No"));
    out.kv(TL("Battery current"), String(autocal_current_A, 1) + " A (" + String(current_direction) + ")");
    out.kv(TL("Current in range"), autocal_current_range());
    out.kv(TL("Dwell time"), String(dwell_sec / kSecondsPerMinute) + "m " +
                                 String(dwell_sec % kSecondsPerMinute) + "s / " +
                                 String(kAutoCalDwellRequiredMs / kMillisPerMinute) + "m");
    out.kv(TL("SOC drift"),
           AdvancedValue{String(byd_datalayer->autocal_drift_percent, 1) + "% / threshold " +
                             String(byd_datalayer->auto_calibrate_soc_drift_percent) + "%",
                         drift_reached ? AdvancedSeverity::Good : AdvancedSeverity::Normal});
    out.kv(TL("Cooldown"), good_if(byd_datalayer->autocal_crit_cooldown_ready, "Ready", "Waiting",
                                   AdvancedSeverity::Warning));
  }


  out.section(TL("Isolation resistance monitor"));

  String monitoring;
  if (!byd_datalayer->iso_status_valid) {
    monitoring = "Unknown";
  } else if (!byd_datalayer->contactor_main_closed) {
    // Monitoring status from 0x35E b0 bit0x80; only unambiguous when the pack is closed
    monitoring = "Inactive (pack open)";
  } else {
    monitoring = byd_datalayer->iso_measurement_active ? "On" : "Off";
  }
  out.kv(TL("Monitoring"), monitoring);

  if (byd_datalayer->insulation_valid) {
    float iso_kohm = static_cast<float>(byd_datalayer->insulation_ohm_per_volt) *
                     (datalayer_battery->status.voltage_dV / 10.0f) / 1000.0f;
    out.kv(TL("Insulation resistance"),
                            String(iso_kohm, 1) + " kΩ (" + String(byd_datalayer->insulation_ohm_per_volt) + " Ω/V)");
  } else {
    out.kv(TL("Insulation resistance"), "Not received");
  }

  // Command feedback, only shown while something is pending or after a failure
  if (byd_datalayer->iso_command_status == 1) {
    out.kv(TL("Last command"), "Sending…");
  } else if (byd_datalayer->iso_command_status == 3) {
    out.kv(TL("Last command"), "Rejected");
  } else if (byd_datalayer->iso_command_status == 4) {
    out.kv(TL("Last command"), "No response");
  }

  auto& dtc = datalayer_battery->dtc;
  write_dtc_section(out, *this, dtc, DtcCodeStyle::kStandard);
}

namespace {
using ST = SettingType;
using SA = SettingApplies;
using SS = SettingStorage;
using SR = SettingRam;

constexpr int32_t kDriftMin = 1;
constexpr int32_t kDriftMax = 20;

void mirror_keep_iso_disabled(uint8_t) {
  datalayer_extended.bydAtto3_2.keep_iso_disabled = datalayer_extended.bydAtto3.keep_iso_disabled;
}

const DeviceSetting kSettings[] = {
    {{"BYDAUTOCALEN", ST::Bool, kLive, SA::Live, 0, nullptr, nullptr, kNoMin, kNoMax, SS::Nvs, kSecBydCal,
      {SR::Bool, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3.auto_calibrate_soc_enabled; }}},
     "Auto-calibrate SOC (battery 1)",
     0},
    {{"BYDAUTOCALDRIFT", ST::Uint, kLive, SA::Live, 0, nullptr, nullptr, kDriftMin, kDriftMax, SS::Nvs, kSecBydCal,
      {SR::U8, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3.auto_calibrate_soc_drift_percent; }}},
     "Drift (%) (battery 1)",
     0},
    {{"BYDAUTOCALEN2", ST::Bool, kLive, SA::Live, 0, nullptr, nullptr, kNoMin, kNoMax, SS::Nvs, kSecBydCal,
      {SR::Bool, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3_2.auto_calibrate_soc_enabled; }}},
     "Auto-calibrate SOC (battery 2)",
     1},
    {{"BYDAUTOCALDRFT2", ST::Uint, kLive, SA::Live, 0, nullptr, nullptr, kDriftMin, kDriftMax, SS::Nvs, kSecBydCal,
      {SR::U8, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3_2.auto_calibrate_soc_drift_percent; }}},
     "Drift (%) (battery 2)",
     1},
    {{"BYDKEEPISOOFF", ST::Bool, kLive, SA::Live, 0, nullptr, nullptr, kNoMin, kNoMax, SS::Nvs, kSecBydCal,
      {SR::Bool, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3.keep_iso_disabled; }, 1,
       SettingScope::Global, mirror_keep_iso_disabled}},
     "Keep isolation monitoring disabled"},
    {{"cal_target_soc", ST::Uint, kLive, SA::Live, 0, nullptr, nullptr, kNoMin, kNoMax, SS::Volatile, kSecBydCal,
      {SR::U16, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3.calibrationTargetSOC; }}},
     "Calibration target SOC (battery 1)",
     0},
    {{"cal_target_ah", ST::Uint, kLive, SA::Live, 0, nullptr, nullptr, kNoMin, kNoMax, SS::Volatile, kSecBydCal,
      {SR::U16, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3.calibrationTargetAH; }}},
     "Calibration target Ah (battery 1)",
     0},
    {{"cal_target_soc2", ST::Uint, kLive, SA::Live, 0, nullptr, nullptr, kNoMin, kNoMax, SS::Volatile, kSecBydCal,
      {SR::U16, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3_2.calibrationTargetSOC; }}},
     "Calibration target SOC (battery 2)",
     1},
    {{"cal_target_ah2", ST::Uint, kLive, SA::Live, 0, nullptr, nullptr, kNoMin, kNoMax, SS::Volatile, kSecBydCal,
      {SR::U16, [](uint8_t) -> void* { return &datalayer_extended.bydAtto3_2.calibrationTargetAH; }}},
     "Calibration target Ah (battery 2)",
     1},
};
}  // namespace

DeviceSettingList BydAttoBattery::settings() {
  return device_settings(kSettings);
}
