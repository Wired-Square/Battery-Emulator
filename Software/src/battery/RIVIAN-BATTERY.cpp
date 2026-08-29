#include "RIVIAN-BATTERY.h"

#include "../battery/BATTERIES.h"
#include "../communication/can/comm_can.h"
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"     //For Advanced Battery Insights webpage
#include "../devboard/utils/common_functions.h"  //For CRC table
#include "../devboard/utils/events.h"

/*
Initial support for Rivian BIG battery (135kWh)
The battery has 3x CAN channels
- Failover CAN (CAN-FD) lots of content, not required for operation. Has all cellvoltages!
- Platform CAN (500kbps) with all the control messages needed to control the battery <- This is the one we want
- Battery CAN (500kbps) lots of content, not required for operation 
*/

uint8_t RivianBattery::calculateCRC(CAN_frame rx_frame, uint8_t length, uint8_t initial_value) {
  uint8_t crc = initial_value;
  for (uint8_t j = 1; j < length; j++) {  //start at 1, since 0 is the CRC
    crc = crc8_table_SAE_J1850_ZER0[(crc ^ static_cast<uint8_t>(rx_frame.data.u8[j])) % 256];
  }
  crc ^= 0xFF;
  return crc;
}

uint16_t estimate_SOC_based_on_voltage(uint16_t voltage) {
  uint16_t result = 0;
  //Voltage ranges between 4500dV when full, and 3500dV when empty
  result = (voltage - 3500);  //Make the range
  result = result * 10;       //Add decimal
  return result;
}

void RivianBattery::update_values() {

  if (user_selected_use_estimated_SOC) {  //Crash locked packs with bypassed contactors
    datalayer_battery->status.real_soc = estimate_SOC_based_on_voltage(datalayer_battery->status.voltage_dV);
  } else {
    datalayer_battery->status.real_soc = battery_SOC_average;
  }

  //datalayer_battery->status.soh_pptt; //TODO: Find usable SOH

  datalayer_battery->status.voltage_dV = pre_contactor_voltage;
  datalayer_battery->status.current_dA = ((int16_t)battery_current / 10.0 - 3200) * 10;

  datalayer_battery->status.remaining_capacity_Wh = static_cast<uint32_t>(
      (static_cast<double>(datalayer_battery->status.real_soc) / 10000) * datalayer_battery->info.total_capacity_Wh);

  if (user_selected_use_estimated_SOC) {
    datalayer_battery->status.max_charge_power_W = 50000;
    datalayer_battery->status.max_discharge_power_W = 50000;
  } else {
    datalayer_battery->status.max_charge_power_W = ((pre_contactor_voltage / 10) * battery_charge_limit_amp);
    datalayer_battery->status.max_discharge_power_W = ((pre_contactor_voltage / 10) * battery_discharge_limit_amp);
  }

  if (cell_min_voltage_mV > 0) {
    datalayer_battery->status.cell_min_voltage_mV = cell_min_voltage_mV;
  }

  if (cell_max_voltage_mV > 0) {
    datalayer_battery->status.cell_max_voltage_mV = cell_max_voltage_mV;
  }

  datalayer_battery->status.temperature_min_dC = battery_min_temperature * 10;
  datalayer_battery->status.temperature_max_dC = battery_max_temperature * 10;

  if (battery_thermal_runaway) {
    set_event(EVENT_THERMAL_RUNAWAY, 0);  //Hope nobody will ever get this event!
  }

  //Update extended datalayer for get_advanced_status()
  datalayer_extended.rivian.BMS_state = BMS_state;
  datalayer_extended.rivian.HVIL = HVIL;
  datalayer_extended.rivian.error_flags_from_BMS = error_flags_from_BMS;
  datalayer_extended.rivian.contactor_state = contactor_state;
  datalayer_extended.rivian.error_relay_open = error_relay_open;
  datalayer_extended.rivian.puncture_fault = puncture_fault;
  datalayer_extended.rivian.liquid_fault = liquid_fault;
  datalayer_extended.rivian.IsolationMeasurementOngoing = IsolationMeasurementOngoing;
  datalayer_extended.rivian.isolation_fault_status = isolation_fault_status;
  datalayer_extended.rivian.contactor_DCFC_welded = contactor_DCFC_welded;
  datalayer_extended.rivian.system_safe_state = system_safe_state;
  datalayer_extended.rivian.pre_contactor_voltage = pre_contactor_voltage;
  datalayer_extended.rivian.main_contactor_voltage = main_contactor_voltage;
  datalayer_extended.rivian.voltage_reference = voltage_reference;
  datalayer_extended.rivian.DCFC_contactor_voltage = DCFC_contactor_voltage;
  datalayer_extended.rivian.slewrate_potential_violation = slewrate_potential_violation;
  datalayer_extended.rivian.minimum_power_potential_violation = minimum_power_potential_violation;
  datalayer_extended.rivian.operation_limit_violation_warning = operation_limit_violation_warning;
}

void RivianBattery::handle_incoming_can_frame(CAN_frame rx_frame) {
  switch (rx_frame.ID) {
    case 0x00A:  //DCDC status [Platform CAN]+ 20ms
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x0A0:  //Cellvoltage min/max (Not available on all packs)
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      cell_min_voltage_mV = (((rx_frame.data.u8[5] & 0x0F) << 8) | rx_frame.data.u8[4]);
      cell_max_voltage_mV = ((rx_frame.data.u8[6] << 4) | (rx_frame.data.u8[5] >> 4));
      break;
    case 0x06E:  //Status flags [Platform CAN]+ 10ms
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      error_flags_from_BMS = rx_frame.data.u8[5];
      error_relay_open = (rx_frame.data.u8[6] & 0x01);
      IsolationMeasurementOngoing = (rx_frame.data.u8[6] & 0x02) >> 1;
      contactor_state = (((rx_frame.data.u8[7] & 0x01) << 3) | (rx_frame.data.u8[6] >> 5));
      break;
    case 0x100:  //Discharge/Charge speed [Platform CAN]+ 10ms
      battery_charge_limit_amp =
          (((rx_frame.data.u8[3] & 0x0F) << 8) | (rx_frame.data.u8[2] << 4) | (rx_frame.data.u8[1] >> 4)) / 20;
      battery_discharge_limit_amp =
          (((rx_frame.data.u8[5] & 0x0F) << 8) | (rx_frame.data.u8[4] << 4) | (rx_frame.data.u8[3] >> 4)) / 20;
      slewrate_potential_violation = (rx_frame.data.u8[5] & 0x10) >> 4;
      minimum_power_potential_violation = (rx_frame.data.u8[5] & 0x20) >> 5;
      operation_limit_violation_warning = (rx_frame.data.u8[5] & 0x40) >> 6;
      break;
    case 0x1E3:  //HMI [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      if (rx_frame.data.u8[0] != calculateCRC(rx_frame, 3, 0xEC)) {
        datalayer_battery->status.CAN_error_counter++;
        break;
      }
      HMI_part1 = rx_frame.data.u8[1];
      HMI_part2 = rx_frame.data.u8[2];
      break;
    case 0x120:  //Voltages [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      DCFC_contactor_voltage = (((rx_frame.data.u8[1] & 0x1F) << 8) | rx_frame.data.u8[0]);
      voltage_reference = (((rx_frame.data.u8[3] & 0x1F) << 8) | rx_frame.data.u8[2]);
      main_contactor_voltage = (((rx_frame.data.u8[5] & 0x1F) << 8) | rx_frame.data.u8[4]);
      pre_contactor_voltage = (((rx_frame.data.u8[7] & 0x1F) << 8) | rx_frame.data.u8[6]);
      break;
    case 0x145:  //NACS charger status [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      NACS_charger_detected = true;
      break;
    case 0x151:  //Celltemps (requires other CAN channel)
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      break;
    case 0x153:  //Temperatures [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      battery_max_temperature = (rx_frame.data.u8[5] / 2) - 40;
      battery_min_temperature = (rx_frame.data.u8[6] / 2) - 40;
      break;
    case 0x154:  //Status flags [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      if (rx_frame.data.u8[0] != calculateCRC(rx_frame, 8, 0xFD)) {
        datalayer_battery->status.CAN_error_counter++;
        break;
      }
      puncture_fault = ((rx_frame.data.u8[1] & 0x40) >> 6);
      liquid_fault = ((rx_frame.data.u8[4] & 0x02) >> 1);
      battery_thermal_runaway = ((rx_frame.data.u8[4] & 0x04) >> 2);
      system_safe_state = ((rx_frame.data.u8[4] & 0x78) >> 3);
      isolation_fault_status = ((rx_frame.data.u8[5] << 1) | ((rx_frame.data.u8[4] & 0x80) >> 7));
      break;
    case 0x156:  //States [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      contactor_DCFC_welded = (rx_frame.data.u8[5] & 0x01);
      break;
    case 0x160:  //Current [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      battery_current = ((rx_frame.data.u8[1] << 8) | rx_frame.data.u8[0]);
      break;
    case 0x162:  //Departuretime [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      //frame0-1 minutes to departure time
      break;
    case 0x164:  //End of drive message [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      //bit0-1 , 0 = normal, 1 = derate, 2 = stop , 3 = modereq100
      break;
    case 0x25A:  //SOC and kWh [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      //battery_SOC = (((rx_frame.data.u8[1] & 0x03) << 8) | rx_frame.data.u8[0]);
      kWh_available_max =
          (((rx_frame.data.u8[3] & 0x03) << 14) | (rx_frame.data.u8[2] << 6) | (rx_frame.data.u8[1] >> 2)) / 200;
      kWh_available_total =
          (((rx_frame.data.u8[5] & 0x03) << 14) | (rx_frame.data.u8[4] << 6) | (rx_frame.data.u8[3] >> 2)) / 200;
      break;
    case 0x299:  //Status flags [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      HVIL = (rx_frame.data.u8[2] & 0x07);
      break;
    case 0x405:  //State [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      BMS_state = (rx_frame.data.u8[0] & 0x03);
      break;
    case 0x55B:  //SOC [Platform CAN]+
      datalayer_battery->status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      battery_SOC_average = (rx_frame.data.u8[4] << 8) | rx_frame.data.u8[5];
      break;
    default:
      break;
  }
}

void RivianBattery::transmit_can(unsigned long currentMillis) {
  // Send 500ms CAN Message, too fast and the pack can't change states (pre-charge is built in, seems we can't change state during pre-charge)
  // 100ms seems to draw too much current for a 5A supply during contactor pull in
  if (currentMillis - previousMillis10 >= (INTERVAL_200_MS)) {
    previousMillis10 = currentMillis;

    //If we want to close contactors, and system allows for it
    if ((datalayer.system.status.inverter_allows_contactor_closing) &&
        (datalayer.system.status.system_status != FAULT)) {
      //Standby -> Ready Mode
      if (BMS_state == STANDBY || BMS_state == SLEEP) {
        RIVIAN_150.data.u8[0] = 0x03;
        RIVIAN_150.data.u8[2] = 0x01;
        RIVIAN_420.data.u8[0] = 0x02;
        RIVIAN_200.data.u8[0] = 0x08;

        transmit_can_frame(&RIVIAN_150);
        transmit_can_frame(&RIVIAN_420);
        transmit_can_frame(&RIVIAN_41F);
        transmit_can_frame(&RIVIAN_207);
        transmit_can_frame(&RIVIAN_200);
      }
      //Ready mode -> Go Mode

      if (BMS_state == READY) {
        RIVIAN_150.data.u8[0] = 0x3E;
        RIVIAN_150.data.u8[2] = 0x03;
        RIVIAN_420.data.u8[0] = 0x03;

        transmit_can_frame(&RIVIAN_150);
        transmit_can_frame(&RIVIAN_420);
      }

    } else {  //If we want to open contactors, transition the other way
              //Go mode -> Ready Mode

      if (BMS_state == GO) {
        RIVIAN_150.data.u8[0] = 0x03;
        RIVIAN_150.data.u8[2] = 0x01;

        transmit_can_frame(&RIVIAN_150);
      }

      if (BMS_state == READY) {
        RIVIAN_150.data.u8[0] = 0x03;
        RIVIAN_150.data.u8[2] = 0x01;
        RIVIAN_420.data.u8[0] = 0x01;
        RIVIAN_200.data.u8[0] = 0x10;

        transmit_can_frame(&RIVIAN_245);
        transmit_can_frame(&RIVIAN_150);
        transmit_can_frame(&RIVIAN_420);
        transmit_can_frame(&RIVIAN_41F);
        transmit_can_frame(&RIVIAN_200);
      }
    }
    //disabled this because the battery didn't like it so fast (slowed to 100ms) and because the battery couldn't change states fast enough
    //as much as I don't like the "free-running" aspect of it, checking the BMS_state and acting on it should fix issues caused by that.
    //transmit_can_frame(&RIVIAN_150);
    //transmit_can_frame(&RIVIAN_420);
    //transmit_can_frame(&RIVIAN_41F);
    //transmit_can_frame(&RIVIAN_207);
    //transmit_can_frame(&RIVIAN_200);
  }
}

void RivianBattery::setup(void) {  // Performs one time setup at startup
  datalayer_battery->info.number_of_cells = 108;
  datalayer_battery->info.total_capacity_Wh = 135000;
  datalayer_battery->info.chemistry = NMC;
  datalayer_battery->info.max_design_voltage_dV = MAX_PACK_VOLTAGE_DV;
  datalayer_battery->info.min_design_voltage_dV = MIN_PACK_VOLTAGE_DV;
  datalayer_battery->info.max_cell_voltage_mV = MAX_CELL_VOLTAGE_MV;
  datalayer_battery->info.min_cell_voltage_mV = MIN_CELL_VOLTAGE_MV;
  datalayer.system.status.battery_allows_contactor_closing = true;
}

void RivianBattery::write_advanced_status(AdvancedStatusWriter& out) {
  out.section("");

  out.kv(TL("Voltage, pre contactors"), String(datalayer_extended.rivian.pre_contactor_voltage), "dV");
  out.kv(TL("Voltage, main contactors"), String(datalayer_extended.rivian.main_contactor_voltage), "dV");
  out.kv(TL("Voltage, reference"), String(datalayer_extended.rivian.voltage_reference), "dV");
  out.kv("Voltage, DCFC contactors", String(datalayer_extended.rivian.DCFC_contactor_voltage), "dV");

  out.kv(TL("SOC, max"), String(datalayer_extended.rivian.battery_SOC_max), "pptt");
  out.kv(TL("SOC, min"), String(datalayer_extended.rivian.battery_SOC_min), "pptt");

  if (datalayer_extended.rivian.NACS_charger_detected) {
    out.kv(TL("NACS charger detected"), "yes");
  }

  out.kv(TL("Isolation measurement ongoing"), datalayer_extended.rivian.IsolationMeasurementOngoing ? "Yes" : "No");

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
  out.kv(TL("Isolation Status"), isolation_status);

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
  out.kv(TL("Interlock status"), interlock_status);

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
  out.kv(TL("BMS State"), bms_state);

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
  out.kv(TL("Contactor State"), contactor_state);


  out.section(TL("Active errors and faults"));

  if (datalayer_extended.rivian.error_relay_open) {
    out.kv(TL("Error Relay Open"), "yes");
  }
  if (datalayer_extended.rivian.error_flags_from_BMS & 0x01) {
    out.kv(TL("Error Isolation Single"), "yes");
  }
  if ((datalayer_extended.rivian.error_flags_from_BMS & 0x02) >> 1) {
    out.kv(TL("Error Isolation Double"), "yes");
  }
  if ((datalayer_extended.rivian.error_flags_from_BMS & 0x04) >> 2) {
    out.kv(TL("Error Emergency Off CRASH"), "yes");
  }
  if ((datalayer_extended.rivian.error_flags_from_BMS & 0x08) >> 3) {
    out.kv(TL("Error Emergency Off Pilot"), "yes");
  }
  if ((datalayer_extended.rivian.error_flags_from_BMS & 0x10) >> 4) {
    out.kv(TL("Error Emergency Off Request"), "yes");
  }
  if ((datalayer_extended.rivian.error_flags_from_BMS & 0x20) >> 5) {
    out.kv(TL("Error Emergency Off"), "yes");
  }
  if ((datalayer_extended.rivian.error_flags_from_BMS & 0x40) >> 6) {
    out.kv(TL("Error Contactors Welded"), "yes");
  }
  if ((datalayer_extended.rivian.error_flags_from_BMS & 0x80) >> 7) {
    out.kv(TL("Error Limited Power"), "yes");
  }

  //HMI errors / status codes, bundle them also under errors

  if ((datalayer_extended.rivian.HMI_part1 & 0x10) >> 4) {
    out.kv(TL("Vehicle Battery Issue"), "yes");
  }
  if ((datalayer_extended.rivian.HMI_part1 & 0x20) >> 5) {
    out.kv(TL("Critical Battery Issue"), "yes");
  }
  if ((datalayer_extended.rivian.HMI_part1 & 0x40) >> 6) {
    out.kv(TL("AC performance limited"), "yes");
  }
  if ((datalayer_extended.rivian.HMI_part1 & 0x80) >> 7) {
    out.kv(TL("DC performance limited"), "yes");
  }
  if (datalayer_extended.rivian.HMI_part2 & 0x01) {
    out.kv(TL("DC charging disabled"), "yes");
  }
  if ((datalayer_extended.rivian.HMI_part2 & 0x02) >> 1) {
    out.kv(TL("Electric hazard"), "yes");
  }
  if ((datalayer_extended.rivian.HMI_part2 & 0x04) >> 2) {
    out.kv(TL("Fire risk"), "yes");
  }
  if ((datalayer_extended.rivian.HMI_part2 & 0x08) >> 3) {
    out.kv(TL("Vehicle system fault"), "yes");
  }
  if ((datalayer_extended.rivian.HMI_part2 & 0x10) >> 4) {
    out.kv(TL("Battery electric malfunction"), "yes");
  }

  //Misc
  if (datalayer_extended.rivian.system_safe_state > 1) {
    out.kv(TL("System safe state A active"), "yes");
  }
  if (datalayer_extended.rivian.puncture_fault) {
    out.kv(TL("Puncture fault detected"), "yes");
  }
  if (datalayer_extended.rivian.liquid_fault) {
    out.kv(TL("Liquid fault detected"), "yes");
  }
  if (datalayer_extended.rivian.contactor_DCFC_welded) {
    out.kv("DCFC contactor welded", "yes");
  }

  if (datalayer_extended.rivian.slewrate_potential_violation) {
    out.kv(TL("Slewrate potential violation"), "yes");
  }
  if (datalayer_extended.rivian.minimum_power_potential_violation) {
    out.kv(TL("Min power potential violation"), "yes");
  }
  if (datalayer_extended.rivian.operation_limit_violation_warning) {
    out.kv(TL("Operation limit violation warning"), "yes");
  }
}
