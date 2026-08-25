#include "safety.h"
#include <algorithm>
#include "../../battery/BATTERIES.h"
#include "../../charger/CHARGERS.h"
#include "../../datalayer/datalayer.h"
#include "../../devboard/utils/logging.h"
#include "../../inverter/INVERTERS.h"
#include "../utils/events.h"

static uint8_t charge_limit_failures = 0;
static uint8_t discharge_limit_failures = 0;
static bool battery_full_event_fired = false;
static bool battery_empty_event_fired = false;
static bool charge_blocked = false;
static bool discharge_blocked = false;
static uint8_t hysteresis_heap_seconds = 0;
static uint8_t inverter_slow_start_counter = 0;

#define MAX_SOH_DEVIATION_PPTT 2500
// Some inverters take a while to boot and start sending CAN. Suppress the
// inverter-missing error during this startup window (measured from power-on).
#define INVERTER_STARTUP_GRACE_MS 300000  // 300 s
#define CELL_CRITICAL_MV 100              // If cells go this much outside design voltage, shut battery down!
#define LOWEST_ALLOWED_CELLVOLTAGE_RECOVERY_CHARGE_MV 2000  //If cells are below this, recovery charge not allowed
#define MAX_CHARGEPOWER_RECOVERY_CHARGE_DA 50
#define HYSTERESIS_OFFSET_DV 20
#define CELL_HYSTERESIS_MV 20  // Re-allow charge only once max cell drops this far below limit (avoids chatter at knee)

//battery pause status begin
bool emulator_pause_request_ON = false;
bool emulator_pause_CAN_send_ON = false;
bool allowed_to_send_CAN = true;
static uint32_t emulator_restart_request_millis = 0;

//component detection
bool battery_detected_slots[kMaxBatterySlots] = {};
bool charger_detected = false;
bool inverter_detected = false;

bool battery_slot_detected(uint8_t slot) {
  return slot < kMaxBatterySlots && battery_detected_slots[slot];
}

battery_pause_status emulator_pause_status = NORMAL;
//battery pause status end

static bool cell_ov_charge_blocked[kMaxBatterySlots] = {};

// Test hook: the detection flags and OV latches are otherwise write-once for
// the life of the process (firmware reboots on reconfiguration).
void reset_battery_pack_safety_state() {
  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    battery_detected_slots[slot] = false;
    cell_ov_charge_blocked[slot] = false;
  }
  charge_limit_failures = 0;
  discharge_limit_failures = 0;
  battery_full_event_fired = false;
  battery_empty_event_fired = false;
  charge_blocked = false;
  discharge_blocked = false;
  hysteresis_heap_seconds = 0;
  inverter_slow_start_counter = 0;
}

// Shared CAN-aliveness handling for battery slots and the charger: latch the
// detected-event on the first counter refresh, raise the missing-event when the
// counter runs out, decrement and clear it otherwise. The inverter has its own
// logic (long-timeout option, startup grace) and is handled separately.
static void check_can_component_alive(uint8_t& still_alive_counter, bool& detected, EVENTS_ENUM_TYPE detected_event,
                                      EVENTS_ENUM_TYPE missing_event, uint8_t missing_event_data) {
  if (!detected) {
    if (still_alive_counter >= CAN_STILL_ALIVE) {
      detected = true;
      set_event(detected_event, 1);
    }
  }
  if (!still_alive_counter) {
    set_event(missing_event, missing_event_data);
  } else {
    --still_alive_counter;
    clear_event(missing_event);
  }
}

// Per-pack checks, run for every present battery slot. The events shared by
// all packs (cell deviation, SOH difference) are aggregated after the loop:
// a healthy pack must never clear a fault another pack raised.
static void check_battery_packs() {
  bool any_present = false;
  uint16_t deviation_worst_mV = 0;
  bool deviation_offending = false;
  bool soh_pair_checked = false;
  bool soh_offending = false;
  int16_t overheat_worst_dC = 0;
  bool overheat_offending = false;
  int16_t frozen_worst_dC = 0;
  bool frozen_offending = false;
  int32_t temp_spread_worst_dC = 0;
  bool temp_spread_offending = false;
  uint16_t overvolt_worst_dV = 0;
  bool overvolt_offending = false;
  uint16_t undervolt_worst_dV = 0;
  bool undervolt_offending = false;
  uint16_t soh_low_worst_pptt = 0;
  bool soh_low_offending = false;

  // Pause and FAULT are system conditions. The combined view was composed before
  // this pass, so pack zeroing alone never reaches the inverter — the view
  // must be zeroed here as well.
  if (emulator_pause_request_ON || (datalayer.system.status.system_status == FAULT)) {
    datalayer.battery.combined.status.max_discharge_power_W = 0;
    datalayer.battery.combined.status.max_charge_power_W = 0;
  }

  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    if (batteries[slot] == nullptr) {
      continue;
    }
    any_present = true;
    auto& status = datalayer.battery_slot(slot).status;
    auto& info = datalayer.battery_slot(slot).info;

    // Pause function is on OR we have a critical fault event active
    if (emulator_pause_request_ON || (datalayer.system.status.system_status == FAULT)) {
      status.max_discharge_power_W = 0;
      status.max_charge_power_W = 0;
    }

    // Cell overvoltage, further charging not possible. Battery might be imbalanced.
    if (status.cell_max_voltage_mV >= info.max_cell_voltage_mV) {
      set_event(EVENT_CELL_OVER_VOLTAGE, 0);
      cell_ov_charge_blocked[slot] = true;  // Latch at the ceiling
    } else if (status.cell_max_voltage_mV < (info.max_cell_voltage_mV - CELL_HYSTERESIS_MV)) {
      cell_ov_charge_blocked[slot] = false;  // Release only once well below the ceiling
    }
    if (cell_ov_charge_blocked[slot]) {
      status.max_charge_power_W = 0;
      // A blocked cell in any joined pack blocks system charging.
      datalayer.battery.combined.status.max_charge_power_W = 0;
    }
    // Cell CRITICAL overvoltage, critical latching error without automatic reset. Requires user action to inspect battery.
    if (status.cell_max_voltage_mV >= (info.max_cell_voltage_mV + CELL_CRITICAL_MV)) {
      set_event(EVENT_CELL_CRITICAL_OVER_VOLTAGE, 0);
    }

    // Cell undervoltage. Further discharge not possible. Battery might be imbalanced.
    if (status.cell_min_voltage_mV <= info.min_cell_voltage_mV) {
      set_event(EVENT_CELL_UNDER_VOLTAGE, 0);
      status.max_discharge_power_W = 0;
      // An empty cell in any joined pack blocks system discharge.
      datalayer.battery.combined.status.max_discharge_power_W = 0;
    }
    //Cell CRITICAL undervoltage. critical latching error without automatic reset. Requires user action to inspect battery.
    if (status.cell_min_voltage_mV <= (info.min_cell_voltage_mV - CELL_CRITICAL_MV)) {
      set_event(EVENT_CELL_CRITICAL_UNDER_VOLTAGE, 0);
    }

    // Check diff between highest and lowest cell
    uint16_t deviation_mV = std::abs(status.cell_max_voltage_mV - status.cell_min_voltage_mV);
    if (deviation_mV > info.max_cell_voltage_deviation_mV && deviation_mV >= deviation_worst_mV) {
      deviation_offending = true;
      deviation_worst_mV = deviation_mV;
    }

    // Check if SOH% between this pack and the primary is too large
    if (slot > 0 && datalayer.battery.pack[0].status.soh_pptt != 9900 && status.soh_pptt != 9900) {
      soh_pair_checked = true;
      uint16_t soh_diff_pptt = datalayer.battery.pack[0].status.soh_pptt > status.soh_pptt
                                   ? datalayer.battery.pack[0].status.soh_pptt - status.soh_pptt
                                   : status.soh_pptt - datalayer.battery.pack[0].status.soh_pptt;
      if (soh_diff_pptt > MAX_SOH_DEVIATION_PPTT) {
        soh_offending = true;
      }
    }
    if (status.temperature_max_dC > BATTERY_MAXTEMPERATURE &&
        (!overheat_offending || status.temperature_max_dC > overheat_worst_dC)) {
      overheat_offending = true;
      overheat_worst_dC = status.temperature_max_dC;
    }
    if (status.temperature_min_dC < BATTERY_MINTEMPERATURE &&
        (!frozen_offending || status.temperature_min_dC < frozen_worst_dC)) {
      frozen_offending = true;
      frozen_worst_dC = status.temperature_min_dC;
    }

    int32_t temp_spread_dC = labs(status.temperature_max_dC - status.temperature_min_dC);
    if (temp_spread_dC > BATTERY_MAX_TEMPERATURE_DEVIATION && temp_spread_dC > temp_spread_worst_dC) {
      temp_spread_offending = true;
      temp_spread_worst_dC = temp_spread_dC;
    }

    // Pack voltage outside its own design limits: block the offending
    // direction on this pack AND on the composed view (same-cycle actuation).
    if (status.voltage_dV > info.max_design_voltage_dV) {
      if (!overvolt_offending || status.voltage_dV > overvolt_worst_dV) {
        overvolt_offending = true;
        overvolt_worst_dV = status.voltage_dV;
      }
      status.max_charge_power_W = 0;
      datalayer.battery.combined.status.max_charge_power_W = 0;
    }
    if (status.voltage_dV < info.min_design_voltage_dV) {
      if (!undervolt_offending || status.voltage_dV < undervolt_worst_dV) {
        undervolt_offending = true;
        undervolt_worst_dV = status.voltage_dV;
      }
      status.max_discharge_power_W = 0;
      datalayer.battery.combined.status.max_discharge_power_W = 0;
    }
    if (status.soh_pptt < 2500 && (!soh_low_offending || status.soh_pptt < soh_low_worst_pptt)) {
      soh_low_offending = true;
      soh_low_worst_pptt = status.soh_pptt;
    }

    if (!batteries[slot]->soc_plausible()) {
      set_event(EVENT_SOC_PLAUSIBILITY_ERROR, status.real_soc);
    }
  }

  if (any_present) {
    if (deviation_offending) {
      set_event(EVENT_CELL_DEVIATION_HIGH, std::min<uint16_t>(deviation_worst_mV / 20, UINT8_MAX));
    } else {
      clear_event(EVENT_CELL_DEVIATION_HIGH);
    }

    if (overheat_offending) {
      set_event(EVENT_BATTERY_OVERHEAT, overheat_worst_dC);
    } else {
      clear_event(EVENT_BATTERY_OVERHEAT);
    }

    if (frozen_offending) {
      set_event(EVENT_BATTERY_FROZEN, frozen_worst_dC);
    } else {
      clear_event(EVENT_BATTERY_FROZEN);
    }

    if (temp_spread_offending) {
      set_event_latched(EVENT_BATTERY_TEMP_DEVIATION_HIGH, temp_spread_worst_dC);
    } else {
      clear_event(EVENT_BATTERY_TEMP_DEVIATION_HIGH);
    }

    if (overvolt_offending) {
      set_event(EVENT_BATTERY_OVERVOLTAGE, overvolt_worst_dV);
    } else {
      clear_event(EVENT_BATTERY_OVERVOLTAGE);
    }

    if (undervolt_offending) {
      set_event(EVENT_BATTERY_UNDERVOLTAGE, undervolt_worst_dV);
    } else {
      clear_event(EVENT_BATTERY_UNDERVOLTAGE);
    }

    if (soh_low_offending) {
      set_event(EVENT_SOH_LOW, soh_low_worst_pptt);
    } else {
      clear_event(EVENT_SOH_LOW);
    }
  }

  if (soh_offending) {
    set_event(EVENT_SOH_DIFFERENCE, (uint8_t)(MAX_SOH_DEVIATION_PPTT / 100));
  } else if (soh_pair_checked) {
    clear_event(EVENT_SOH_DIFFERENCE);
  }
}

// Liveness checks for every present battery slot. Runs after the primary-only
// checks: the missing event is ERROR level and latches system FAULT the moment
// it is set, which must not pre-empt the ACTIVE-gated checks (battery empty)
// on the tick a battery first goes silent. Corrupted CAN aggregates across
// slots for the same reason as the pack checks above.
static void check_battery_liveness() {
  static constexpr struct {
    EVENTS_ENUM_TYPE detected;
    EVENTS_ENUM_TYPE missing;
  } kSlotEvents[kMaxBatterySlots] = {
      {EVENT_CAN_BATTERY_DETECTED, EVENT_CAN_BATTERY_MISSING},
      {EVENT_CAN_BATTERY2_DETECTED, EVENT_CAN_BATTERY2_MISSING},
      {EVENT_CAN_BATTERY3_DETECTED, EVENT_CAN_BATTERY3_MISSING},
  };

  bool any_present = false;
  bool corrupted_offending = false;
  const InterfaceDescriptor* corrupted_worst_interface = nullptr;
  uint16_t corrupted_worst_count = 0;

  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    if (batteries[slot] == nullptr) {
      continue;
    }
    any_present = true;
    auto& status = datalayer.battery_slot(slot).status;

    // Check that the BMS has been seen and is still sending CAN messages.
    // If we go 60s without messages we raise an error
    check_can_component_alive(status.CAN_battery_still_alive, battery_detected_slots[slot], kSlotEvents[slot].detected,
                              kSlotEvents[slot].missing, can_event_interface_id(can_config.battery[slot]));

    // Too many malformed CAN messages received!
    if (status.CAN_error_counter > MAX_CAN_FAILURES && status.CAN_error_counter >= corrupted_worst_count) {
      corrupted_offending = true;
      corrupted_worst_count = status.CAN_error_counter;
      corrupted_worst_interface = can_config.battery[slot];
    }
  }

  if (any_present) {
    if (corrupted_offending) {
      set_event(EVENT_CAN_CORRUPTED_WARNING, can_event_interface_id(corrupted_worst_interface));
    } else {
      clear_event(EVENT_CAN_CORRUPTED_WARNING);
    }
  }
}

/* Expire remote-set limits. Wrap-safe subtraction: the naive
   "currentMillis > timestamp + timeout" comparison both overflows near the 49.7-day
   millis() wrap (fresh limits expire immediately) and inverts after it (stale limits
   persist up to another 49.7 days) */
void update_remote_limit_expiry(uint32_t currentMillis) {
  if ((currentMillis - datalayer.battery.settings.remote_set_timestamp) >
      datalayer.battery.settings.remote_set_timeout) {
    datalayer.battery.settings.remote_settings_limit_charge = false;
    datalayer.battery.settings.remote_settings_limit_discharge = false;
    datalayer.battery.settings.max_remote_set_charge_dA = 0;
    datalayer.battery.settings.max_remote_set_discharge_dA = 0;
  }
}

void update_machineryprotection() {
  //Check if we start to get low on memory
  if (datalayer.system.info.CPU_free_heap < 62000) {
    hysteresis_heap_seconds++;
    if (hysteresis_heap_seconds > 5) {  //Trigger after X seconds of low heap, to prevent false positives during spikes
      set_event(EVENT_LOW_HEAP_MEMORY, (datalayer.system.info.CPU_free_heap / 1000));
    }
  } else {
    clear_event(EVENT_LOW_HEAP_MEMORY);
    hysteresis_heap_seconds = 0;
  }

  // Check health status of CAN interfaces
  if (datalayer.system.info.can_native_send_fail) {
    set_event(EVENT_CAN_NATIVE_BUFFER_FULL, 0);
    datalayer.system.info.can_native_send_fail = false;
  } else {
    clear_event(EVENT_CAN_NATIVE_BUFFER_FULL);
  }
  if (datalayer.system.info.can_native_bus_error) {
    set_event(EVENT_CAN_NATIVE_BUS_ERROR, 0);
    datalayer.system.info.can_native_bus_error = false;
  } else {
    clear_event(EVENT_CAN_NATIVE_BUS_ERROR);
  }
  if (datalayer.system.info.can_2515_send_fail) {
    set_event(EVENT_CANMCP2515_BUFFER_FULL, 0);
    datalayer.system.info.can_2515_send_fail = false;
  } else {
    clear_event(EVENT_CANMCP2515_BUFFER_FULL);
  }
  if (datalayer.system.info.can_2515_bus_error) {
    set_event(EVENT_CANMCP2515_BUS_ERROR, 0);
    datalayer.system.info.can_2515_bus_error = false;
  } else {
    clear_event(EVENT_CANMCP2515_BUS_ERROR);
  }
  if (datalayer.system.info.can_2518_send_fail) {
    set_event(EVENT_CANFD_BUFFER_FULL, 0);
    datalayer.system.info.can_2518_send_fail = false;
  } else {
    clear_event(EVENT_CANFD_BUFFER_FULL);
  }
  if (datalayer.system.info.can_2518_bus_error) {
    set_event(EVENT_CANFD_BUS_ERROR, 0);
    datalayer.system.info.can_2518_bus_error = false;
  } else {
    clear_event(EVENT_CANFD_BUS_ERROR);
  }
  if (datalayer.system.info.can_2518_2_send_fail) {
    set_event(EVENT_CANFD_2_BUFFER_FULL, 0);
    datalayer.system.info.can_2518_2_send_fail = false;
  } else {
    clear_event(EVENT_CANFD_2_BUFFER_FULL);
  }
  if (datalayer.system.info.can_2518_2_bus_error) {
    set_event(EVENT_CANFD_2_BUS_ERROR, 0);
    datalayer.system.info.can_2518_2_bus_error = false;
  } else {
    clear_event(EVENT_CANFD_2_BUS_ERROR);
  }

  check_battery_packs();

  // Start checking system-level battery conditions. The per-pack checks
  // (overheat, frozen, design voltage, SOH, plausibility) run for every slot
  // in check_battery_packs() above.
  if (batteries[0]) {

    //If user is requesting charge to stop at a specific voltage
    if (datalayer.battery.settings.user_set_voltage_limits_active) {
      // --- Charge limiting with hysteresis ---
      if (datalayer.battery.pack[0].status.voltage_dV >= datalayer.battery.settings.max_user_set_charge_voltage_dV) {
        charge_blocked = true;  // Latch: block charging once target is hit
      } else if (datalayer.battery.pack[0].status.voltage_dV <
                 (datalayer.battery.settings.max_user_set_charge_voltage_dV - HYSTERESIS_OFFSET_DV)) {
        charge_blocked = false;  // Only release when voltage drops well below target
      }
      if (charge_blocked) {
        datalayer.battery.combined.status.max_charge_power_W = 0;
      }

      // --- Discharge limiting with hysteresis ---
      if (datalayer.battery.pack[0].status.voltage_dV <= datalayer.battery.settings.max_user_set_discharge_voltage_dV) {
        discharge_blocked = true;
      } else if (datalayer.battery.pack[0].status.voltage_dV >
                 (datalayer.battery.settings.max_user_set_discharge_voltage_dV + HYSTERESIS_OFFSET_DV)) {
        discharge_blocked = false;
      }
      if (discharge_blocked) {
        datalayer.battery.combined.status.max_discharge_power_W = 0;
      }
    }

    // Battery is fully charged. Dont allow any more power into it
    // Normally the BMS will send 0W allowed, but this acts as an additional layer of safety
    if (datalayer.battery.combined.status.reported_soc == 10000 ||
        datalayer.battery.combined.status.real_soc == 10000)  //Either Scaled OR Real SOC% value is 100.00%
    {
      if (!battery_full_event_fired) {
        set_event(EVENT_BATTERY_FULL, 0);
        battery_full_event_fired = true;
      }
      datalayer.battery.combined.status.max_charge_power_W = 0;
    } else {
      clear_event(EVENT_BATTERY_FULL);
      battery_full_event_fired = false;
    }

    // Battery is empty. Do not allow further discharge.
    // Normally the BMS will send 0W allowed, but this acts as an additional layer of safety
    if (datalayer.system.status.system_status == ACTIVE) {
      if (datalayer.battery.combined.status.reported_soc == 0 ||
          datalayer.battery.combined.status.real_soc == 0) {  //Either Scaled OR Real SOC% value is 0.00%, time to stop
        if (!battery_empty_event_fired) {
          set_event(EVENT_BATTERY_EMPTY, 0);
          battery_empty_event_fired = true;
        }
        datalayer.battery.combined.status.max_discharge_power_W = 0;
      } else {
        clear_event(EVENT_BATTERY_EMPTY);
        battery_empty_event_fired = false;
      }
    }

    // Inverter is charging with more power than battery wants!
    if (datalayer.battery.combined.status.active_power_W > 0) {  // Charging
      if (datalayer.battery.combined.status.active_power_W > (datalayer.battery.combined.status.max_charge_power_W + 2000)) {
        if (charge_limit_failures > MAX_CHARGE_DISCHARGE_LIMIT_FAILURES) {
          set_event(EVENT_CHARGE_LIMIT_EXCEEDED, 0);  // Alert when 2kW over requested max
        } else {
          charge_limit_failures++;
        }
      } else {
        clear_event(EVENT_CHARGE_LIMIT_EXCEEDED);
        charge_limit_failures = 0;
      }
    }

    // Inverter is pulling too much power from battery!
    if (datalayer.battery.combined.status.active_power_W < 0) {  // Discharging
      if (-datalayer.battery.combined.status.active_power_W > (datalayer.battery.combined.status.max_discharge_power_W + 2000)) {
        if (discharge_limit_failures > MAX_CHARGE_DISCHARGE_LIMIT_FAILURES) {
          set_event(EVENT_DISCHARGE_LIMIT_EXCEEDED, 0);  // Alert when 2kW over requested max
        } else {
          discharge_limit_failures++;
        }
      } else {
        clear_event(EVENT_DISCHARGE_LIMIT_EXCEEDED);
        discharge_limit_failures = 0;
      }
    }

  }

  check_battery_liveness();

  if (inverter && inverter->interface_type() == InverterInterfaceType::Can) {

    //Check if we have ever seen the inverter
    if (!inverter_detected) {
      // >= not ==: some drivers refresh the counter above CAN_STILL_ALIVE for a
      // longer timeout (SMA x3, Sofar x2), which would never match equality
      if (datalayer.system.status.CAN_inverter_still_alive >= CAN_STILL_ALIVE) {
        inverter_detected = true;
        set_event(EVENT_CAN_INVERTER_DETECTED, 1);
      }
    }

    // Check if the inverter is still sending CAN messages. If we go 60s without messages we raise a warning
    if (!datalayer.system.status.CAN_inverter_still_alive) {
      // Inverters that are slow to boot get a startup grace window before we fault.
      // millis64: with plain millis() this comparison goes false again for 5 minutes
      // after the 49.7-day wrap, re-suppressing detection of a new inverter-comms loss
      if (!inverter->needs_can_startup_grace() || millis64() > INVERTER_STARTUP_GRACE_MS) {
        set_event(EVENT_CAN_INVERTER_MISSING, can_event_interface_id(can_config.inverter));
      }
    } else {
      // Inverter frames received recently - clear any previously raised missing-event
      // regardless of which timeout mode is active
      clear_event(EVENT_CAN_INVERTER_MISSING);
      // If the inverter is a slow starter, only decrement the counter every 2 seconds to give it more time to start up before we report it as missing
      if (user_selected_inverter_long_CAN_timeout) {
        inverter_slow_start_counter++;
        if (inverter_slow_start_counter > 2) {  // Only decrement every 2 seconds
          datalayer.system.status.CAN_inverter_still_alive--;
          inverter_slow_start_counter = 0;
        }
      } else {  //Normal 60s timeout for regular inverters
        datalayer.system.status.CAN_inverter_still_alive--;
      }
    }
  }

  if (charger) {
    // Assuming chargers are all CAN here.
    // Check that the charger has been seen and is still sending CAN messages.
    // If we go 60s without messages we raise a warning
    check_can_component_alive(datalayer.charger.CAN_charger_still_alive, charger_detected, EVENT_CAN_CHARGER_DETECTED,
                              EVENT_CAN_CHARGER_MISSING, can_event_interface_id(charger->interface()));
  }

  //Safeties verified, Zero charge/discharge ampere values incase any safety wrote the W to 0
  if (datalayer.battery.combined.status.max_discharge_power_W == 0) {
    datalayer.battery.combined.status.max_discharge_current_dA = 0;
  }
  if (datalayer.battery.combined.status.max_charge_power_W == 0) {
    datalayer.battery.combined.status.max_charge_current_dA = 0;
  }
  //One exception. If user has enabled the emergency recovery charge mode, still allow small amount of charging
  if (datalayer.battery.settings.user_requests_forced_charging_recovery_mode) {

    //We allow the user set value as long as it does not exceed MAX_CHARGEPOWER_RECOVERY_CHARGE_DA
    if (datalayer.battery.settings.max_user_set_charge_dA > MAX_CHARGEPOWER_RECOVERY_CHARGE_DA) {
      datalayer.battery.combined.status.max_charge_current_dA = MAX_CHARGEPOWER_RECOVERY_CHARGE_DA;
    } else {
      datalayer.battery.combined.status.max_charge_current_dA = datalayer.battery.settings.max_user_set_charge_dA;
    }

    // If this is the start of the emergency recovery charge period, capture the current time
    if (datalayer.battery.settings.recovery_charge_start_time_ms == 0) {
      datalayer.battery.settings.recovery_charge_start_time_ms = millis();
      set_event(EVENT_RECOVERY_START, 0);
    } else {
      clear_event(EVENT_RECOVERY_START);
    }

    // Check if the elapsed time exceeds the max recovery charge time
    if (millis() - datalayer.battery.settings.recovery_charge_start_time_ms >=
        datalayer.battery.settings.recovery_charge_max_time_ms) {
      datalayer.battery.settings.user_requests_forced_charging_recovery_mode = false;
      datalayer.battery.settings.recovery_charge_start_time_ms = 0;  // Reset the start time
      set_event(EVENT_RECOVERY_END, 0);
    } else {
      clear_event(EVENT_RECOVERY_END);
    }

    //Check if cellvoltage is too low to safely start recovery. If so, abort!
    if (datalayer.battery.pack[0].status.cell_min_voltage_mV < LOWEST_ALLOWED_CELLVOLTAGE_RECOVERY_CHARGE_MV) {
      datalayer.battery.settings.user_requests_forced_charging_recovery_mode = false;
      set_event(EVENT_RECOVERY_END, 255);
    }
  }

  // Every slot, not just the enabled ones: this timer alone lowers the ceilings balancing raises.
  bool any_balancing = false;
  bool any_started = false;
  bool any_ended = false;
  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    auto& slot_settings = datalayer.battery_slot(slot).settings;
    if (!slot_settings.user_requests_balancing) {
      continue;
    }
    any_balancing = true;

    if (slot_settings.balancing_start_time_ms == 0) {
      slot_settings.balancing_start_time_ms = millis();
      any_started = true;
    }

    if (millis() - slot_settings.balancing_start_time_ms >= slot_settings.balancing_max_time_ms) {
      slot_settings.user_requests_balancing = false;
      slot_settings.balancing_start_time_ms = 0;
      any_ended = true;
    }
  }

  if (any_balancing) {
    if (any_started) {
      set_event(EVENT_BALANCING_START, 0);
    } else {
      clear_event(EVENT_BALANCING_START);
    }

    if (any_ended) {
      set_event(EVENT_BALANCING_END, 0);
    } else {
      clear_event(EVENT_BALANCING_END);
    }
  }
}

//battery pause status begin
void setBatteryPause(bool pause_battery, bool pause_CAN, EquipmentStop equipment_stop, bool store_settings) {
  DEBUG_PRINTF("Battery pause begin %d %d %d %d\n", pause_battery, pause_CAN, equipment_stop, store_settings);

  // First handle equipment stop / resume
  if (equipment_stop == STOP && !datalayer.system.info.equipment_stop_active) {
    datalayer.system.info.equipment_stop_active = true;
    if (store_settings) {
      store_settings_equipment_stop();
    }

    set_event(EVENT_EQUIPMENT_STOP, 1);
  } else if (equipment_stop == RESUME && datalayer.system.info.equipment_stop_active) {
    datalayer.system.info.equipment_stop_active = false;
    if (store_settings) {
      store_settings_equipment_stop();
    }
    clear_event(EVENT_EQUIPMENT_STOP);
  }

  emulator_pause_CAN_send_ON = pause_CAN;

  if (pause_battery) {

    set_event(EVENT_PAUSE_BEGIN, 1);
    emulator_pause_request_ON = true;
    emulator_pause_status = PAUSING;
    for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
      datalayer.battery_slot(slot).status.max_discharge_power_W = 0;
      datalayer.battery_slot(slot).status.max_charge_power_W = 0;
    }
    // The inverter view was composed before this call; zero it directly so the
    // pause takes effect this cycle, not after the next compose.
    datalayer.battery.combined.status.max_discharge_power_W = 0;
    datalayer.battery.combined.status.max_charge_power_W = 0;
    datalayer.battery.combined.status.max_discharge_current_dA = 0;
    datalayer.battery.combined.status.max_charge_current_dA = 0;

  } else {
    clear_event(EVENT_PAUSE_BEGIN);
    set_event(EVENT_PAUSE_END, 0);
    emulator_pause_request_ON = false;
    emulator_pause_CAN_send_ON = false;
    emulator_pause_status = RESUMING;
    clear_event(EVENT_PAUSE_END);
  }

  //immediate check if we can send CAN messages
  update_pause_state();
}

void graceful_restart() {
  // Pause charge/discharge, and then restart the ESP32 within 5s (as soon as the power stops).

  set_event(EVENT_RESTARTING, 0);

  // Stop charge/discharge so we don't damage the contactors
  setBatteryPause(true, false, EquipmentStop::UNCHANGED, false);

  uint32_t now = millis();
  emulator_restart_request_millis = now > 0 ? now : 1;
}

void update_restart_progress() {
  // If is a restart has been requested, check the time and restart if the
  // conditions are met.

  if (emulator_restart_request_millis > 0) {
    uint32_t now = millis();
    uint32_t elapsed = now - emulator_restart_request_millis;
    // Restart after 5s if the emulator has paused. Always restart after 10s.
    if ((elapsed > INTERVAL_5_S && emulator_pause_status == PAUSED) || elapsed > INTERVAL_10_S) {
      ESP.restart();
    }
  }
}

/// @brief handle emulator pause status and CAN sending allowed
void update_pause_state() {
  bool previous_allowed_to_send_CAN = allowed_to_send_CAN;

  if (emulator_pause_status == NORMAL) {
    allowed_to_send_CAN = true;
  }

  int16_t battery_current_dA = datalayer.battery.pack[0].status.current_dA;
  int16_t battery2_current_dA = datalayer.battery.pack[1].status.current_dA;  // Should be 0 if no battery2
  int16_t battery3_current_dA = datalayer.battery.pack[2].status.current_dA;  // Should be 0 if no battery3
  static const int16_t CURRENT_THRESHOLD_dA = 18;                      // 1.8A in deciAmps

  // in some inverters this values are not accurate, so we need to check if we are consider 1.8 amps as the limit
  if (emulator_pause_request_ON && emulator_pause_status == PAUSING && abs(battery_current_dA) < CURRENT_THRESHOLD_dA &&
      abs(battery2_current_dA) < CURRENT_THRESHOLD_dA && abs(battery3_current_dA) < CURRENT_THRESHOLD_dA) {
    emulator_pause_status = PAUSED;
  }

  if (!emulator_pause_request_ON && emulator_pause_status == RESUMING) {
    emulator_pause_status = NORMAL;
    allowed_to_send_CAN = true;
  }

  allowed_to_send_CAN = (!emulator_pause_CAN_send_ON || emulator_pause_status == NORMAL);

  if (previous_allowed_to_send_CAN && !allowed_to_send_CAN) {
    DEBUG_PRINTF("Safety: Pausing CAN sending\n");
    //completely force stop the CAN communication
    stop_can();
  } else if (!previous_allowed_to_send_CAN && allowed_to_send_CAN) {
    //resume CAN communication
    DEBUG_PRINTF("Safety: Resuming CAN sending\n");
    restart_can();
  }
}

std::string get_emulator_pause_status() {
  switch (emulator_pause_status) {
    case NORMAL:
      return "RUNNING";
    case PAUSING:
      return "PAUSING";
    case PAUSED:
      return "PAUSED";
    case RESUMING:
      return "RESUMING";
    default:
      return "UNKNOWN";
  }
}
//battery pause status
