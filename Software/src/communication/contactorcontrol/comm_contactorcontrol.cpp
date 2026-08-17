#include "comm_contactorcontrol.h"
#include "../../battery/BATTERIES.h"
#include "../../devboard/hal/GpioOutput.h"
#include "../../devboard/hal/hal.h"
#include "../../devboard/safety/safety.h"
#include "../../devboard/utils/led_handler.h"
#include "../../inverter/INVERTERS.h"
#ifndef UNIT_TEST
#include "driver/gpio.h"  // gpio_hold_en / gpio_hold_dis / gpio_deep_sleep_hold_en
#endif
#include <Arduino.h>

// TODO: Ensure valid values at run-time
// User can update all these values via Settings page
bool contactor_control_enabled = false;         //Should GPIO contactor control be performed?
bool contactor_control_inverted_logic = false;  //Should we control NC contactors? Extremely rare option
uint16_t precharge_time_ms = 100;               //Precharge time in ms. Adjust depending on capacitance in inverter
bool pwm_contactor_control = false;             //Should the contactors be economized via PWM after they are engaged?
bool contactor_control_enabled_double_battery = false;  //Should a contactor for the secondary battery be operated?
bool contactor_control_enabled_triple_battery = false;  //Should a contactor for the third battery be operated?
bool remote_bms_reset = false;                          //Is it possible to actuate BMS reset via MQTT?
bool periodic_bms_reset = false;                        //Should periodic BMS reset be performed?
uint16_t periodic_bms_reset_interval_h = 24;            //How often the periodic BMS reset runs, in hours (24 or 48)
bool periodic_bms_reset_defer_low_soc = false;          //Defer the reset while SOC is below BMS_RESET_DEFER_SOC_PPTT
bool periodic_bms_reset_skip_balancing = false;         //Skip one period if the pack is balancing

// Parameters
ContactorState contactorStatus = ContactorState::DISCONNECTED;

#define MAX_ALLOWED_FAULT_TICKS 1000  //1000 = 10 seconds
#define NEGATIVE_CONTACTOR_TIME_MS \
  500  // Time after negative contactor is turned on, to start precharge (not actual precharge time!)
#define PRECHARGE_COMPLETED_TIME_MS \
  1000  // After successful precharge, resistor is turned off after this delay (and contactors are economized if PWM enabled)
#define ESTOP_OPEN_TIMEOUT_MS \
  7000  // Equipment stop: max time to wait for the pause to reach zero current before opening contactors anyway
uint16_t pwm_frequency = 20000;
uint16_t pwm_hold_duty = 250;
static uint32_t prechargeStartTime = 0;
static uint32_t negativeStartTime = 0;
static uint32_t prechargeCompletedTime = 0;
static uint32_t timeSpentInFaultedMode = 0;
uint32_t lastPowerRemovalTime = 0;
static uint32_t bmsPowerOnTime = 0;
uint32_t currentTime = 0;
static uint32_t estop_open_wait_start_ms = 0;
bool periodicResetDeferred = false;   //True while a due periodic reset is waiting for SOC to recover
bool balancingPeriodSkipped = false;  //True once balancing has cost the reset a period
const uint32_t bmsWarmupDuration = 3000;
#define BMS_RESET_DEFER_SOC_PPTT 1500  // 15.00%, below this the low-SOC guard defers the periodic reset

/* The safety layer decrements CAN_battery_still_alive once per second and latches
   EVENT_CAN_BATTERY_MISSING when it reaches zero, so the BMS may only be silent for
   CAN_STILL_ALIVE seconds. A reset that keeps the BMS powered off for longer than that
   would always trip the event, so for those durations we refresh the liveness counters
   ourselves while the reset runs. Refreshing one second before the window closes keeps
   the counter from ever reaching zero. Durations that fit inside the window are left
   alone and keep the original, unmasked behaviour. */
#define BMS_RESET_CAN_KEEPALIVE_INTERVAL_MS ((uint32_t)(CAN_STILL_ALIVE - 1) * 1000UL)
uint32_t lastCanKeepaliveTime = 0;

bool bms_power_is_active() {
  return periodic_bms_reset || remote_bms_reset || esp32hal->always_enable_bms_power();
}

// Initialization functions

const char* contactors = "Contactors";

// Missing role row = feature not wired on this board.
static SwitchedOutput* claim_output(OutputRole role, const char* owner) {
  SwitchedOutput* output = esp32hal->switched_output(role);
  if (output == nullptr) {
    set_event(EVENT_GPIO_NOT_DEFINED, (uint8_t)GPIO_NUM_NC);
    return nullptr;
  }
  return output->init(owner) ? output : nullptr;
}

bool init_contactors() {
  if (contactor_control_enabled) {
    SwitchedOutput* positive = claim_output(OutputRole::PositiveContactor, contactors);
    SwitchedOutput* negative = claim_output(OutputRole::NegativeContactor, contactors);
    SwitchedOutput* precharge = claim_output(OutputRole::Precharge, contactors);
    if (positive == nullptr || negative == nullptr || precharge == nullptr) {
      DEBUG_PRINTF("GPIO controlled contactor setup failed\n");
      return false;
    }
    positive->set(false);
    negative->set(false);
    precharge->set(false);
    set_indicator_led(IndicatorLed::CONTACTOR_POS, false);
    set_indicator_led(IndicatorLed::CONTACTOR_NEG, false);
    set_indicator_led(IndicatorLed::PRECHARGE, false);
  }

  if (contactor_control_enabled_double_battery) {
    SwitchedOutput* second = claim_output(OutputRole::SecondBatteryContactors, contactors);
    if (second == nullptr) {
      DEBUG_PRINTF("Secondary battery contactor control setup failed\n");
      return false;
    }
    second->set(false);
  }

  if (contactor_control_enabled_triple_battery) {
    SwitchedOutput* third = claim_output(OutputRole::ThirdBatteryContactors, contactors);
    if (third == nullptr) {
      DEBUG_PRINTF("Triple battery contactor control setup failed\n");
      return false;
    }
    third->set(false);
  }

  // Init BMS contactor
  if (bms_power_is_active()) {
    SwitchedOutput* bms_power = claim_output(OutputRole::BmsPower, "BMS power");
    if (bms_power == nullptr) {
      DEBUG_PRINTF("BMS power setup failed\n");
      return false;
    }
    bms_power->set_raw(true);
    set_indicator_led(IndicatorLed::BMS_POWER, true);
  }

  return true;
}

static void dbg_contactors(const char* state) {
  logging.print("[");
  logging.print(millis());
  logging.print(" ms] contactors control: ");
  logging.println(state);
}

// Main functions of the handle_contactors include checking if inverter allows for closing, checking battery 2, checking BMS power output, and actual contactor closing/precharge via GPIO
void handle_contactors() {
  if (inverter && inverter->controls_contactor()) {
    datalayer.system.status.inverter_allows_contactor_closing = inverter->allows_contactor_closing();
  }

  if (esp32hal->switched_output(OutputRole::BmsPower) != nullptr) {
    handle_BMSpower();  // Some batteries need to be periodically power cycled
  }

#ifdef BOARD_HAS_LOAD_SWITCH
  // A hardware fault on any bound output (VDS/power-limit/thermal or
  // latch-off on a VN9D channel) feeds the same FAULT latch as any other
  // ERROR event; GPIO outputs never report one.
  SwitchedOutputList outputs = esp32hal->switched_outputs();
  for (size_t i = 0; i < outputs.count; i++) {
    if (outputs.data[i].output->fault()) {
      set_event(EVENT_LOAD_SWITCH_FAULT, (uint8_t)i);
    }
  }
#endif

  if (contactor_control_enabled_double_battery) {
    handle_contactors_battery2();
  }

  if (contactor_control_enabled_triple_battery) {
    handle_contactors_battery3();
  }

  if (contactor_control_enabled) {
    SwitchedOutput* positive = esp32hal->switched_output(OutputRole::PositiveContactor);
    SwitchedOutput* negative = esp32hal->switched_output(OutputRole::NegativeContactor);
    SwitchedOutput* precharge = esp32hal->switched_output(OutputRole::Precharge);
    if (positive == nullptr || negative == nullptr || precharge == nullptr) {
      return;
    }
    // DC is only live to the inverter once precharge is fully complete (COMPLETED). Re-evaluated every
    // call, so any transition (open, fault-latch, precharge) is reflected within one 10 ms tick.
    datalayer.system.status.dc_bus_live = (contactorStatus == ContactorState::COMPLETED);

    // First check if we have any active errors, incase we do, turn off the battery
    if (datalayer.system.status.system_status == FAULT) {
      timeSpentInFaultedMode++;
    } else {
      timeSpentInFaultedMode = 0;
    }

    //handle contactor control SHUTDOWN_REQUESTED
    if (timeSpentInFaultedMode > MAX_ALLOWED_FAULT_TICKS) {
      contactorStatus = ContactorState::SHUTDOWN_REQUESTED;
    }

    if (contactorStatus == ContactorState::SHUTDOWN_REQUESTED) {
      precharge->set(false);
      negative->set(false);
      positive->set(false);
      set_indicator_led(IndicatorLed::PRECHARGE, false);
      set_indicator_led(IndicatorLed::CONTACTOR_NEG, false);
      set_indicator_led(IndicatorLed::CONTACTOR_POS, false);
      set_event(EVENT_ERROR_OPEN_CONTACTOR, 0);
      datalayer.system.status.contactors_engaged = 2;
      return;  // A fault scenario latches the contactor control. It is not possible to recover without a powercycle (and investigation why fault occured)
    }

    // After that, check if we are OK to start turning on the battery
    if (contactorStatus == ContactorState::DISCONNECTED) {
      precharge->set(false);
      negative->set(false);
      positive->set(false);
      set_indicator_led(IndicatorLed::PRECHARGE, false);
      set_indicator_led(IndicatorLed::CONTACTOR_NEG, false);
      set_indicator_led(IndicatorLed::CONTACTOR_POS, false);
      datalayer.system.status.contactors_engaged = 0;

      if (datalayer.system.status.system_status == ACTIVE &&
          datalayer.system.status.inverter_allows_contactor_closing && !datalayer.system.info.equipment_stop_active) {
        contactorStatus = ContactorState::START_PRECHARGE;
      }
    }

    // In case the inverter or the equipment stop requests contactors to open, jump to Disconnected (recoverable)
    if (contactorStatus == ContactorState::COMPLETED) {
      if (!datalayer.system.status.inverter_allows_contactor_closing) {
        // Inverter-commanded opening stays immediate: the inverter has already
        // stopped power transfer before revoking its permission
        contactorStatus = ContactorState::DISCONNECTED;
      } else if (datalayer.system.info.equipment_stop_active) {
        // Equipment stop: every e-stop entry point also issues a battery pause,
        // so hold the contactors until the pause state machine reports PAUSED
        // (current below 1.8 A) - opening under load risks arcing/welding.
        // Bounded: after ESTOP_OPEN_TIMEOUT_MS we open anyway and raise an
        // event, mirroring the BMS-reset give-up behavior.
        uint32_t now = millis();
        if (estop_open_wait_start_ms == 0) {
          estop_open_wait_start_ms = now;
        }
        bool paused = (emulator_pause_status == PAUSED);
        bool timed_out = (now - estop_open_wait_start_ms) > ESTOP_OPEN_TIMEOUT_MS;
        if (paused || timed_out) {
          if (timed_out) {
            LOG_SET_NEXT_SEVERITY(4);  // warning
            logging.printf("Contactors: Equipment stop wait timed out, opening under load\n");
            set_event(EVENT_ERROR_OPEN_CONTACTOR, 1);
          }
          estop_open_wait_start_ms = 0;
          contactorStatus = ContactorState::DISCONNECTED;
        }
      } else {
        estop_open_wait_start_ms = 0;
      }
      // Skip running the state machine below if it has already completed
      return;
    }

    uint32_t currentTime = millis();

    if ((currentTime < INTERVAL_10_S) || !battery_slot_detected(0)) {
      // Skip running the state machine before system has started up. Conditions are 10sec post boot, and battery comms established
      // Gives the system some time to detect any faults from battery before blindly just engaging the contactors
      return;
    }

    // Handle actual state machine. This first turns on Negative, then Precharge, then Positive, and finally turns OFF precharge
    switch (contactorStatus) {
      case ContactorState::START_PRECHARGE:
        negative->set(true);
        set_indicator_led(IndicatorLed::CONTACTOR_NEG, true);
        dbg_contactors("NEGATIVE");
        prechargeStartTime = currentTime;
        contactorStatus = ContactorState::PRECHARGE;
        datalayer.system.status.contactors_engaged = 3;
        break;

      case ContactorState::PRECHARGE:
        if (currentTime - prechargeStartTime >= NEGATIVE_CONTACTOR_TIME_MS) {
          precharge->set(true);
          set_indicator_led(IndicatorLed::PRECHARGE, true);
          dbg_contactors("PRECHARGE");
          negativeStartTime = currentTime;
          contactorStatus = ContactorState::POSITIVE;
          datalayer.system.status.contactors_engaged = 3;
        }
        break;

      case ContactorState::POSITIVE:
        if (currentTime - negativeStartTime >= precharge_time_ms) {
          positive->set(true);
          set_indicator_led(IndicatorLed::CONTACTOR_POS, true);
          dbg_contactors("POSITIVE");
          prechargeCompletedTime = currentTime;
          contactorStatus = ContactorState::PRECHARGE_OFF;
          datalayer.system.status.contactors_engaged = 3;
        }
        break;

      case ContactorState::PRECHARGE_OFF:
        if (currentTime - prechargeCompletedTime >= PRECHARGE_COMPLETED_TIME_MS) {
          precharge->set(false);
          negative->set_hold();
          positive->set_hold();
          set_indicator_led(IndicatorLed::PRECHARGE, false);
          dbg_contactors("PRECHARGE_OFF");
          contactorStatus = ContactorState::COMPLETED;
          datalayer.system.status.contactors_engaged = 1;
        }
        break;
      default:
        break;
    }
  }
}

void handle_contactors_battery2() {
  SwitchedOutput* second = esp32hal->switched_output(OutputRole::SecondBatteryContactors);
  if (second == nullptr) {
    return;
  }

  if ((contactorStatus == ContactorState::COMPLETED) && datalayer.system.status.battery2_allowed_contactor_closing) {
    second->set(true);
    datalayer.system.status.contactors_battery2_engaged = true;
  } else {  // Closing contactors on secondary battery not allowed
    second->set(false);
    datalayer.system.status.contactors_battery2_engaged = false;
  }
}

void handle_contactors_battery3() {
  SwitchedOutput* third = esp32hal->switched_output(OutputRole::ThirdBatteryContactors);
  if (third == nullptr) {
    return;
  }

  if ((contactorStatus == ContactorState::COMPLETED) && datalayer.system.status.battery3_allowed_contactor_closing) {
    third->set(true);
    datalayer.system.status.contactors_battery3_engaged = true;
  } else {  // Closing contactors on secondary battery not allowed
    third->set(false);
    datalayer.system.status.contactors_battery3_engaged = false;
  }
}

/* PERIODIC_BMS_RESET - Once every configured interval (24h or 48h) we remove power from the BMS_power pin for 30 seconds.
The user can optionally defer the reset while SOC is low, and skip a single period while balancing.
REMOTE_BMS_RESET - Allows the user to remotely powercycle the BMS by sending a command to the emulator via MQTT.

This makes the BMS recalculate all SOC% and avoid memory leaks
During that time we also set the emulator state to paused in order to not try and send CAN messages towards the battery
Feature is only used if user has enabled PERIODIC_BMS_RESET */

void bms_power_off() {
  if (SwitchedOutput* bms_power = esp32hal->switched_output(OutputRole::BmsPower)) {
    bms_power->set_raw(false);
  }
  set_indicator_led(IndicatorLed::BMS_POWER, false);
}

void bms_power_on() {
  if (SwitchedOutput* bms_power = esp32hal->switched_output(OutputRole::BmsPower)) {
    bms_power->set_raw(true);
  }
  set_indicator_led(IndicatorLed::BMS_POWER, true);
}

// Configured period between two automatic resets. Guarded to 24h in case the stored
// value is missing or nonsensical, see load_settings().
static uint32_t bms_reset_interval_ms() {
  uint32_t hours = periodic_bms_reset_interval_h;
  if (hours == 0) {
    hours = 24;
  }
  return (uint32_t)hours * 60UL * 60UL * 1000UL;
}

/* The two guards behave differently on purpose, so the decision is three-way rather than
   a plain yes/no. Only the periodic reset is affected, a remote reset requested by the user
   via MQTT is always carried out. */
enum class PeriodicResetVerdict {
  Run,        // Nothing in the way, reset now
  Defer,      // Stay due and reset as soon as the condition clears
  SkipPeriod  // Give up this occurrence, try again one full period later
};

/* Decides what should happen to a periodic reset whose interval has elapsed. On a non-Run
   verdict the reason is written to the out parameter for logging, and always points at a
   string literal. Note that an unknown SOC reads as 0 and therefore also defers the reset
   while the low SOC guard is enabled, which is the safe direction. */
static PeriodicResetVerdict periodic_bms_reset_verdict(const char** reason) {
  // Low SOC defers: there is little point recalibrating against a nearly empty pack, and we
  // want the reset to happen as soon as it is worthwhile rather than a whole period later.
  if (periodic_bms_reset_defer_low_soc) {
    uint16_t min_real_soc = UINT16_MAX;
    uint16_t min_reported_soc = UINT16_MAX;
    for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
      if (batteries[slot] == nullptr) {
        continue;
      }
      auto& status = datalayer.battery_slot(slot).status;
      if (status.real_soc < min_real_soc) {
        min_real_soc = status.real_soc;
      }
      if (status.reported_soc < min_reported_soc) {
        min_reported_soc = status.reported_soc;
      }
    }
    if (min_real_soc < BMS_RESET_DEFER_SOC_PPTT) {
      *reason = "real SOC below 15 percent";
      return PeriodicResetVerdict::Defer;
    }
    if (min_reported_soc < BMS_RESET_DEFER_SOC_PPTT) {
      *reason = "scaled SOC below 15 percent";
      return PeriodicResetVerdict::Defer;
    }
  }

  /* Balancing costs the reset a single period. If balancing is still active when the next
     period comes around the reset goes ahead anyway, so a pack that reports balancing more
     or less permanently cannot suppress the reset indefinitely.
     Batteries that are not present report BALANCING_STATUS_UNKNOWN, so they never skip. */
  if (periodic_bms_reset_skip_balancing && !balancingPeriodSkipped) {
    for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
      if (batteries[slot] == nullptr) {
        continue;
      }
      if (datalayer.battery_slot(slot).status.balancing_status == BALANCING_STATUS_ACTIVE) {
        *reason = "balancing active on battery";
        return PeriodicResetVerdict::SkipPeriod;
      }
    }
  }

  return PeriodicResetVerdict::Run;
}

/* True when the configured off time outlasts the CAN liveness window and the reset therefore
   needs the counters held up. Only the off time matters here: the surrounding pause and warmup
   phases are short and the BMS is on the bus for part of them. To test the statement in Leaf 
   "GEN4_e_Battery_control_spec_ver1.0.pdf" page 4, "IGN to be OFF for more than 6 min 30 seconds every day. "*/
static bool bms_reset_needs_can_keepalive() {
  return datalayer.battery.settings.user_set_bms_reset_duration_ms > BMS_RESET_CAN_KEEPALIVE_INTERVAL_MS;
}

/* Pretends the batteries were just heard from, and restarts the keepalive interval.
   Batteries that are not configured are skipped, since the safety layer does not look at
   their counters either. */
static void bms_reset_refresh_can_alive(uint32_t now) {
  lastCanKeepaliveTime = now;
  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    if (batteries[slot] != nullptr) {
      datalayer.battery_slot(slot).status.CAN_battery_still_alive = CAN_STILL_ALIVE;
    }
  }
}

// Called on every pass through the powered-off and powering-on states of a long reset.
static void bms_reset_can_keepalive_tick(uint32_t now) {
  if (!bms_reset_needs_can_keepalive()) {
    return;
  }
  if (now - lastCanKeepaliveTime < BMS_RESET_CAN_KEEPALIVE_INTERVAL_MS) {
    return;
  }
  bms_reset_refresh_can_alive(now);
}

void handle_BMSpower() {
  //Skip running the BMS reset state machine if equipment stop is active, as we don't want to powercycle the BMS during that time
  if (datalayer.system.info.equipment_stop_active) {
    return;
  }

  if (periodic_bms_reset || remote_bms_reset) {
    uint32_t currentTime = millis();

    if (datalayer.system.status.bms_reset_status == BMS_RESET_IDLE) {
      // Idle state, no reset ongoing

      // Check if it's time to perform a periodic BMS reset
      if (periodic_bms_reset && currentTime - lastPowerRemovalTime >= bms_reset_interval_ms()) {
        const char* reason = nullptr;
        switch (periodic_bms_reset_verdict(&reason)) {
          case PeriodicResetVerdict::Defer:
            /* Leave lastPowerRemovalTime alone so the reset stays due. start_bms_reset()
               re-anchors it when the reset finally runs, which is what moves the following
               24h/48h periods to start from the deferred point. Logged once per episode,
               since this branch is evaluated on every loop while the reset is due. */
            if (!periodicResetDeferred) {
              periodicResetDeferred = true;
              logging.printf("BMS reset: Periodic reset deferred, %s.\n", reason);
            }
            break;

          case PeriodicResetVerdict::SkipPeriod:
            // Restarting the interval here is what turns this into a one period skip.
            lastPowerRemovalTime = currentTime;
            balancingPeriodSkipped = true;
            logging.printf("BMS reset: Periodic reset skipped for one period, %s.\n", reason);
            break;

          case PeriodicResetVerdict::Run:
            if (periodicResetDeferred) {
              periodicResetDeferred = false;
              logging.printf("BMS reset: Running previously deferred periodic reset.\n");
            }
            balancingPeriodSkipped = false;  // Each reset restores the one period allowance
            start_bms_reset();
            break;
        }
      }
    } else if (datalayer.system.status.bms_reset_status == BMS_RESET_WAITING_FOR_PAUSE) {
      // We've already issued a pause, now we're waiting for that to take effect.

      int16_t battery_current_dA = datalayer.battery.pack[0].status.current_dA;
      int16_t battery2_current_dA = datalayer.battery.pack[1].status.current_dA;  // Should be 0 if no battery2
      int16_t battery3_current_dA = datalayer.battery.pack[2].status.current_dA;  // Should be 0 if no battery3

      if (
          // No current, safe to cut power
          (battery_current_dA == 0 && battery2_current_dA == 0 && battery3_current_dA == 0)
          // or reasonably low current and 5 seconds has passed
          || (abs(battery_current_dA) < 10 && abs(battery2_current_dA) < 10 && abs(battery3_current_dA) < 10 &&
              currentTime - lastPowerRemovalTime >= 5000)) {

        bms_power_off();
        lastPowerRemovalTime = currentTime;
        datalayer.system.status.bms_reset_status = BMS_RESET_POWERED_OFF;
      } else if (currentTime - lastPowerRemovalTime >= 10000) {
        // There's still current, and we don't want to weld the contactors, so give up.

        datalayer.system.status.bms_reset_status = BMS_RESET_IDLE;
        set_event(EVENT_PERIODIC_BMS_RESET_FAILURE, 0);  // also printing a log entry
        clear_event(EVENT_PERIODIC_BMS_RESET_FAILURE);
      }
    } else if (datalayer.system.status.bms_reset_status == BMS_RESET_POWERED_OFF) {
      bms_reset_can_keepalive_tick(currentTime);

      // Check if the user configured duration has passed
      if (currentTime - lastPowerRemovalTime >= datalayer.battery.settings.user_set_bms_reset_duration_ms) {
        bms_power_on();
        bmsPowerOnTime = currentTime;
        /* The last periodic refresh can have been up to a full interval ago, which would leave
           the BMS only a sliver of the window to get back on the bus. Refreshing here gives it
           the whole window from power-on, measured from the same moment for every off time. */
        if (bms_reset_needs_can_keepalive()) {
          bms_reset_refresh_can_alive(currentTime);
        }
        datalayer.system.status.bms_reset_status = BMS_RESET_POWERING_ON;
      }
    } else if (datalayer.system.status.bms_reset_status == BMS_RESET_POWERING_ON) {
      bms_reset_can_keepalive_tick(currentTime);

      // Wait for BMS to start up before unpausing
      if (currentTime - bmsPowerOnTime >= bmsWarmupDuration) {
        // Unpause the battery
        setBatteryPause(false, false, EquipmentStop::UNCHANGED, false);

        // Reset is complete

        datalayer.system.status.bms_reset_status = BMS_RESET_IDLE;
        set_event(EVENT_PERIODIC_BMS_RESET, 0);
        clear_event(EVENT_PERIODIC_BMS_RESET);
      }
    }
  }
}

void start_bms_reset() {
  if (periodic_bms_reset || remote_bms_reset) {
    if (datalayer.system.status.bms_reset_status == BMS_RESET_IDLE) {
      // Record when we started the BMS reset process
      lastPowerRemovalTime = millis();
      // Anchor the keepalive here so the first refresh lands one interval into the reset,
      // rather than immediately or after a gap left over from a previous reset.
      lastCanKeepaliveTime = lastPowerRemovalTime;

      // Issue a pause, which should stop charge/discharge whilst the reset is ongoing
      setBatteryPause(true, false, EquipmentStop::UNCHANGED, false);

      if (contactor_control_enabled) {
        // We power the contactors directly, so we can avoid closing/opening them
        // during reset.

        // Thus we can cut the BMS power now
        bms_power_off();

        // and jump straight to powered off state, no need to wait.
        datalayer.system.status.bms_reset_status = BMS_RESET_POWERED_OFF;
      } else {
        // The BMS powers the contactors, so we need to wait for the pause to
        // take effect before cutting power to it, or the contactors might drop
        // out under load and be damaged.

        datalayer.system.status.bms_reset_status = BMS_RESET_WAITING_FOR_PAUSE;
      }
    }
  }
}

// Decide whether a specific candidate pin should be latched. Only latch pins the firmware
// is actively driving to a defined level — otherwise there's nothing meaningful to preserve,
// and latching a floating/undriven pin could freeze it in an unwanted state.
static bool should_hold_pin(gpio_num_t pin) {
  if (pin == GPIO_NUM_NC) {
    return false;
  }
  auto* bms_power = esp32hal->switched_output(OutputRole::BmsPower);
  if (bms_power != nullptr && pin == static_cast<GpioOutput*>(bms_power)->pin()) {
    return bms_power_is_active();
  }
  return false;  // unknown pins are not held until explicitly supported
}

void hold_pins_across_reset() {
  const auto pins = esp32hal->reset_hold_pins();
  if (pins.empty()) {
    return;
  }
#ifndef UNIT_TEST
  bool any_held = false;
  for (auto pin : pins) {
    if (should_hold_pin(pin)) {
      gpio_hold_en(pin);  // freeze the pad at its current (driven) level
      any_held = true;
    }
  }
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
  // Chips that cannot hold an individual IO through deep sleep need the global
  // enable to keep the per-pin holds engaged across the reset; C6-class chips
  // hold each IO on their own and do not provide this call.
  if (any_held) {
    gpio_deep_sleep_hold_en();
  }
#else
  (void)any_held;
#endif
#endif
}

void release_pins_across_reset() {
#ifndef UNIT_TEST
  // Release every candidate pin unconditionally. gpio_hold_dis() is a no-op on a pin that
  // isn't held, so this also clears a stale hold left over from a previous session in which
  // the feature was enabled but is now disabled.
  for (auto pin : esp32hal->reset_hold_pins()) {
    if (pin != GPIO_NUM_NC) {
      gpio_hold_dis(pin);
    }
  }
#endif
}
