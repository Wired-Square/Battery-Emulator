#include "battery_combined.h"

#include <algorithm>

#include "datalayer.h"
#include "../devboard/utils/value_mapping.h"

static DATALAYER_BATTERY_TYPE& pack(uint8_t slot) {
  return datalayer.battery_slot(slot);
}

// A pack participates in the system fold only when it is populated and its
// contactors may close onto the shared bus.
static bool slot_joined(const bool (&populated)[kMaxBatterySlots], uint8_t slot) {
  return populated[slot] && datalayer.system.status.slot_allows_contactor_closing(slot);
}

void update_reported_values(const bool (&populated)[kMaxBatterySlots]) {
  auto& agg = datalayer.battery.combined;
  auto& sys_settings = datalayer.battery.settings;

  /* Per-pack instantaneous power, from each pack's own voltage and current */
  pack(0).status.active_power_W = (pack(0).status.current_dA * (pack(0).status.voltage_dV / 100));
  for (uint8_t slot = 1; slot < kMaxBatterySlots; slot++) {
    if (populated[slot]) {
      auto& slot_status = pack(slot).status;
      slot_status.active_power_W = (slot_status.current_dA * (slot_status.voltage_dV / 100));
    }
  }

  /* Snapshot the primary as the base of the view; summable fields and the
     shaped inverter limits are overwritten below. The previous cycle's derived
     currents are retained across the snapshot so an invalid conversion voltage
     keeps them instead of reverting to raw pack values. */
  uint16_t previous_charge_dA = agg.status.max_charge_current_dA;
  uint16_t previous_discharge_dA = agg.status.max_discharge_current_dA;
  agg = pack(0);

  /* Fold BMS power limits, instantaneous power and system extremes across
     joined packs. Parallel packs share current by impedance, not evenly, so
     the safe system limit is bounded by the weakest joined pack:
     min(sum of limits, joined_count * weakest limit). */
  uint32_t charge_limit_sum_W = pack(0).status.max_charge_power_W;
  uint32_t charge_limit_min_W = pack(0).status.max_charge_power_W;
  uint32_t discharge_limit_sum_W = pack(0).status.max_discharge_power_W;
  uint32_t discharge_limit_min_W = pack(0).status.max_discharge_power_W;
  int32_t active_power_sum_W = pack(0).status.active_power_W;
  uint64_t soc_weight_sum = (uint64_t)pack(0).status.real_soc * pack(0).info.total_capacity_Wh;
  uint64_t capacity_weight_Wh = pack(0).info.total_capacity_Wh;
  uint16_t soh_min_pptt = pack(0).status.soh_pptt;
  uint16_t cell_count = std::min<uint16_t>(pack(0).info.number_of_cells, MAX_AMOUNT_CELLS);
  uint32_t joined_count = 1;
  for (uint8_t slot = 1; slot < kMaxBatterySlots; slot++) {
    if (!slot_joined(populated, slot)) {
      continue;
    }
    auto& slot_status = pack(slot).status;
    joined_count++;
    charge_limit_sum_W += slot_status.max_charge_power_W;
    charge_limit_min_W = std::min(charge_limit_min_W, slot_status.max_charge_power_W);
    discharge_limit_sum_W += slot_status.max_discharge_power_W;
    discharge_limit_min_W = std::min(discharge_limit_min_W, slot_status.max_discharge_power_W);
    active_power_sum_W += slot_status.active_power_W;
    soc_weight_sum += (uint64_t)slot_status.real_soc * pack(slot).info.total_capacity_Wh;
    capacity_weight_Wh += pack(slot).info.total_capacity_Wh;
    soh_min_pptt = std::min(soh_min_pptt, slot_status.soh_pptt);
    // Cell extremes only from packs with cell data; temperature extremes only
    // from packs that report a bus voltage (0 dC is a valid temperature, so
    // liveness has to gate the fold, not the value itself).
    if (slot_status.cell_max_voltage_mV != 0) {
      agg.status.cell_max_voltage_mV = std::max(agg.status.cell_max_voltage_mV, slot_status.cell_max_voltage_mV);
      agg.status.cell_min_voltage_mV = std::min(agg.status.cell_min_voltage_mV, slot_status.cell_min_voltage_mV);
      // Inverters that transmit per-cell topology see every joined pack's
      // cells in slot order, truncated at array capacity.
      uint16_t copy_count = std::min<uint16_t>(pack(slot).info.number_of_cells, MAX_AMOUNT_CELLS - cell_count);
      for (uint16_t cell = 0; cell < copy_count; cell++) {
        agg.status.cell_voltages_mV[cell_count + cell] = slot_status.cell_voltages_mV[cell];
        agg.status.cell_balancing_status[cell_count + cell] = slot_status.cell_balancing_status[cell];
      }
      cell_count += copy_count;
    }
    if (slot_status.voltage_dV > 0) {
      agg.status.temperature_max_dC = std::max(agg.status.temperature_max_dC, slot_status.temperature_max_dC);
      agg.status.temperature_min_dC = std::min(agg.status.temperature_min_dC, slot_status.temperature_min_dC);
    }
  }
  agg.status.max_charge_power_W = std::min(charge_limit_sum_W, joined_count * charge_limit_min_W);
  agg.status.max_discharge_power_W = std::min(discharge_limit_sum_W, joined_count * discharge_limit_min_W);
  agg.status.active_power_W = active_power_sum_W;
  agg.info.number_of_cells = cell_count;
  agg.status.soh_pptt = soh_min_pptt;
  // Capacity-weighted system SOC: a large full pack and a small empty one are
  // nowhere near 50%. Packs with no capacity data keep the primary's mirror.
  if (capacity_weight_Wh > 0) {
    agg.status.real_soc = soc_weight_sum / capacity_weight_Wh;
  }

  /* Calculate allowed charge/discharge currents. Prefer live pack voltage for the conversion.
     If unavailable (some drivers report 0 before battery comms are up), fall back to the design
     max voltage - conservative, since it under-estimates current for a given power. If that is
     also 0 (generic BMS drivers with no user-configured pack voltage, or BMS-reported cutoff
     values before first frame), keep the previous values to avoid div0. */
  uint16_t conversion_voltage_dV = agg.status.voltage_dV;
  if (conversion_voltage_dV <= 10) {
    conversion_voltage_dV = agg.info.max_design_voltage_dV;
  }
  if (conversion_voltage_dV > 10) {
    agg.status.max_charge_current_dA = ((agg.status.max_charge_power_W * 100) / conversion_voltage_dV);
    agg.status.max_discharge_current_dA = ((agg.status.max_discharge_power_W * 100) / conversion_voltage_dV);
  } else {
    agg.status.max_charge_current_dA = previous_charge_dA;
    agg.status.max_discharge_current_dA = previous_discharge_dA;
  }

  if (sys_settings.remote_settings_limit_charge) {
    if (agg.status.max_charge_current_dA > sys_settings.max_remote_set_charge_dA) {
      agg.status.max_charge_current_dA = sys_settings.max_remote_set_charge_dA;
    }
  } else {
    if (agg.status.max_charge_current_dA > sys_settings.max_user_set_charge_dA) {
      agg.status.max_charge_current_dA = sys_settings.max_user_set_charge_dA;
      sys_settings.user_settings_limit_charge = true;
    } else {
      sys_settings.user_settings_limit_charge = false;
    }
  }

  if (sys_settings.remote_settings_limit_discharge) {
    if (agg.status.max_discharge_current_dA > sys_settings.max_remote_set_discharge_dA) {
      agg.status.max_discharge_current_dA = sys_settings.max_remote_set_discharge_dA;
    }
  } else {
    if (agg.status.max_discharge_current_dA > sys_settings.max_user_set_discharge_dA) {
      agg.status.max_discharge_current_dA = sys_settings.max_user_set_discharge_dA;
      sys_settings.user_settings_limit_discharge = true;
    } else {
      sys_settings.user_settings_limit_discharge = false;
    }
  }

  /* Calculate sum of all currents from all batteries. 0 if they are not used*/
  int32_t current_sum_dA = pack(0).status.current_dA;
  for (uint8_t slot = 1; slot < kMaxBatterySlots; slot++) {
    current_sum_dA += pack(slot).status.current_dA;
  }
  agg.status.reported_current_dA = current_sum_dA;

  /* Calculate if battery or inverter is limiting factor, from the system
     current: the limits above are system limits, so one pack's current alone
     would misclassify whenever another pack carries the load. */
  if (current_sum_dA == 0) {  //Battery idle
    if (agg.status.max_discharge_current_dA > 0) {
      //We allow discharge, but inverter does nothing. Inverter is limiting
      sys_settings.inverter_limits_discharge = true;
    } else {
      sys_settings.inverter_limits_discharge = false;
    }
    if (agg.status.max_charge_current_dA > 0) {
      //We allow charge, but inverter does nothing. Inverter is limiting
      sys_settings.inverter_limits_charge = true;
    } else {
      sys_settings.inverter_limits_charge = false;
    }
  } else if (current_sum_dA < 0) {  //Battery discharging
    if (-current_sum_dA < agg.status.max_discharge_current_dA) {
      sys_settings.inverter_limits_discharge = true;
    } else {
      sys_settings.inverter_limits_discharge = false;
    }
  } else {  // > 0 Battery charging
    //If actual current is smaller than max we allow, inverter is limiting factor
    if (current_sum_dA < agg.status.max_charge_current_dA) {
      sys_settings.inverter_limits_charge = true;
    } else {
      sys_settings.inverter_limits_charge = false;
    }
  }

  if (sys_settings.soc_scaling_active) {
    /** SOC Scaling
   * A static version of a stochastic oscillator. The scaled SoC is calculated as:
   *
   *     10000 * (real_soc - min_percentage)
   * ---------------------------------------
   *     (max_percentage - min_percentage)
   *
   * And scaled capacity is:
   *
   *     reported_total_capacity_Wh = total_capacity_Wh * (max - min) / 10000
   *     reported_remaining_capacity_Wh = reported_total_capacity_Wh * scaled_soc / 10000
   */
    int32_t delta_pct = sys_settings.max_percentage - sys_settings.min_percentage;
    int32_t clamped_soc =
        CONSTRAIN(pack(0).status.real_soc, sys_settings.min_percentage, sys_settings.max_percentage);
    int32_t scaled_soc = 0;
    int32_t scaled_total_capacity = 0;
    if (delta_pct != 0) {  //Safeguard against division by 0
      scaled_soc = 10000 * (clamped_soc - sys_settings.min_percentage) / delta_pct;
    }

    pack(0).status.reported_soc = scaled_soc;
    agg.status.reported_soc = scaled_soc;

    if (pack(0).info.total_capacity_Wh > 0 && pack(0).status.real_soc > 0) {
      scaled_total_capacity = (pack(0).info.total_capacity_Wh * delta_pct) / 10000;
      pack(0).info.reported_total_capacity_Wh = scaled_total_capacity;

      pack(0).status.reported_remaining_capacity_Wh = (scaled_total_capacity * scaled_soc) / 10000;

    } else {
      pack(0).info.reported_total_capacity_Wh = pack(0).info.total_capacity_Wh;
      pack(0).status.reported_remaining_capacity_Wh = pack(0).status.remaining_capacity_Wh;
    }

    agg.info.reported_total_capacity_Wh = pack(0).info.reported_total_capacity_Wh;
    agg.status.reported_remaining_capacity_Wh = pack(0).status.reported_remaining_capacity_Wh;

    // Every populated slot folds into the combined view's reported totals, so
    // the inverter sees the packs as one large battery. Each slot scales its
    // OWN capacity and SOC against the system window — packs may differ in
    // capacity and state of charge.
    for (uint8_t slot = 1; slot < kMaxBatterySlots; slot++) {
      if (!populated[slot]) {
        continue;
      }
      auto& slot_data = pack(slot);
      if (slot_data.info.total_capacity_Wh > 0 && slot_data.status.real_soc > 0 && delta_pct != 0) {
        int32_t slot_clamped_soc =
            CONSTRAIN(slot_data.status.real_soc, sys_settings.min_percentage, sys_settings.max_percentage);
        int32_t slot_scaled_soc = 10000 * (slot_clamped_soc - sys_settings.min_percentage) / delta_pct;
        int32_t slot_scaled_total = (slot_data.info.total_capacity_Wh * delta_pct) / 10000;
        slot_data.info.reported_total_capacity_Wh = slot_scaled_total;
        slot_data.status.reported_remaining_capacity_Wh = (slot_scaled_total * slot_scaled_soc) / 10000;
      } else {
        slot_data.info.reported_total_capacity_Wh = slot_data.info.total_capacity_Wh;
        slot_data.status.reported_remaining_capacity_Wh = slot_data.status.remaining_capacity_Wh;
      }

      agg.info.reported_total_capacity_Wh += slot_data.info.reported_total_capacity_Wh;
      agg.status.reported_remaining_capacity_Wh += slot_data.status.reported_remaining_capacity_Wh;
    }

  } else {  // soc_scaling_active == false. No SOC window wanted. Set scaled SOC & capacity to same as real.
    pack(0).status.reported_soc = pack(0).status.real_soc;
    pack(0).status.reported_remaining_capacity_Wh = pack(0).status.remaining_capacity_Wh;
    pack(0).info.reported_total_capacity_Wh = pack(0).info.total_capacity_Wh;

    agg.status.reported_soc = pack(0).status.real_soc;

    // Enabled-but-empty slots are excluded: the capacity setting seeds every
    // slot's live capacity, so their datalayer values are non-zero.
    uint32_t remaining_sum_Wh = pack(0).status.remaining_capacity_Wh;
    uint32_t total_sum_Wh = pack(0).info.total_capacity_Wh;
    for (uint8_t slot = 1; slot < kMaxBatterySlots; slot++) {
      if (!populated[slot]) {
        continue;
      }
      remaining_sum_Wh += pack(slot).status.remaining_capacity_Wh;
      total_sum_Wh += pack(slot).info.total_capacity_Wh;
    }
    agg.status.reported_remaining_capacity_Wh = remaining_sum_Wh;
    agg.info.reported_total_capacity_Wh = total_sum_Wh;
  }

  for (uint8_t slot = 1; slot < kMaxBatterySlots; slot++) {
    //For screen to display correct SOC of each extra battery
    pack(slot).status.reported_soc = pack(slot).status.real_soc;
  }

  //Check each extra battery, and if they are at the extremes, report the SOC from these batteries instead.
  //Extremes are judged in the same scaled domain the reported SOC uses: a pack at the bottom of the
  //scaling window is empty for reporting purposes even when its raw SOC is far from 1%.
  int32_t window_min = sys_settings.soc_scaling_active ? sys_settings.min_percentage : 0;
  int32_t window_max = sys_settings.soc_scaling_active ? sys_settings.max_percentage : 10000;
  int32_t window_delta = window_max - window_min;
  for (uint8_t slot = 1; slot < kMaxBatterySlots; slot++) {
    if (!slot_joined(populated, slot) || window_delta == 0) {
      continue;  //This slot is not in the mix
    }
    int32_t slot_clamped_soc = CONSTRAIN(pack(slot).status.real_soc, window_min, window_max);
    int32_t slot_scaled_soc = 10000 * (slot_clamped_soc - window_min) / window_delta;
    if ((slot_scaled_soc < 100) || (slot_scaled_soc > 9900)) {
      agg.status.reported_soc = slot_scaled_soc;
    }
  }
}
