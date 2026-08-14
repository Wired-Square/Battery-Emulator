#ifndef MEB_BATTERY_H
#define MEB_BATTERY_H
#include "BatterySlotContext.h"
#include "CanBattery.h"
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "../devboard/webserver/advanced_api.h"

// VW Group platform battery types.
enum class VAGPlatform : uint8_t {
  MEB = 1,
  MQB_Evo = 2,
};

class MebBattery : public CanBattery, public IsoTp {
 public:
  // Use the default constructor to create the first or single battery.
  MebBattery(const BatterySlotContext& ctx) : CanBattery(ctx.can_interface) {
    datalayer_battery = ctx.datalayer;
    datalayer_meb = &datalayer_extended.meb;
  }

  virtual void setup(void);
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);
  bool supports_charged_energy() { return true; }

  const std::vector<BatteryCommand>& get_commands() override { return commands_; }

  bool supports_insulation_resistance() override { return true; }

  const char* get_dtc_json_filename() override { return "vag_meb_dtc.json"; }

  BatteryAdvancedStatus get_advanced_status() override {
    BatteryAdvancedStatus status;
    AdvancedSection s;

    s.fields.push_back(kv("Service disconnect switch", datalayer_extended.meb.SDSW ? "Missing!" : "OK"));
    s.fields.push_back(kv("Pilotline", datalayer_extended.meb.pilotline ? "Open!" : "OK"));
    s.fields.push_back(kv("Transportmode", datalayer_extended.meb.transportmode ? "Locked!" : "OK"));
    s.fields.push_back(kv("Shutdown", datalayer_extended.meb.shutdown_active ? "Active!" : "No"));
    s.fields.push_back(kv("Component protection", datalayer_extended.meb.componentprotection ? "Active!" : "No"));

    String hvil;
    switch (datalayer_extended.meb.HVIL) {
      case 0:
        hvil = "Init";
        break;
      case 1:
        hvil = "Closed";
        break;
      case 2:
        hvil = "Open!";
        break;
      case 3:
        hvil = "Fault";
        break;
      default:
        hvil = "?";
    }
    s.fields.push_back(kv("HVIL status", hvil));

    String kl30c;
    switch (datalayer_extended.meb.BMS_Kl30c_Status) {
      case 0:
        kl30c = "Init";
        break;
      case 1:
        kl30c = "Closed";
        break;
      case 2:
        kl30c = "Open!";
        break;
      case 3:
        kl30c = "Fault";
        break;
      default:
        kl30c = "?";
    }
    s.fields.push_back(kv("KL30C status", kl30c));

    String bms_status;
    switch (datalayer_battery->status.real_bms_status) {
      case BMS_ACTIVE:
        bms_status = "OK";
        break;
      case BMS_STANDBY:
        bms_status = "Standby";
        break;
      case BMS_FAULT:
        bms_status = "Fault";
        break;
      case BMS_DISCONNECTED:
        bms_status = "Disconnected";
        break;
      default:
        bms_status = "?";
    }
    s.fields.push_back(kv("BMS status", bms_status));

    String bms_mode;
    switch (datalayer_extended.meb.BMS_mode) {
      case 0:
        bms_mode = "HV inactive";
        break;
      case 1:
        bms_mode = "HV active";
        break;
      case 2:
        bms_mode = "Balancing";
        break;
      case 3:
        bms_mode = "Extern charging";
        break;
      case 4:
        bms_mode = "AC charging";
        break;
      case 5:
        bms_mode = "Battery error";
        break;
      case 6:
        bms_mode = "DC charging";
        break;
      case 7:
        bms_mode = "Init";
        break;
      default:
        bms_mode = "?";
    }
    s.fields.push_back(kv("BMS mode", bms_mode));

    s.fields.push_back(kv("Charging", datalayer_extended.meb.charging_active ? "active" : "not active"));

    String balancing;
    switch (datalayer_extended.meb.balancing_active) {
      case 0:
        balancing = "init";
        break;
      case 1:
        balancing = "active";
        break;
      case 2:
        balancing = "inactive";
        break;
      default:
        balancing = "?";
    }
    s.fields.push_back(kv("Balancing", balancing));

    s.fields.push_back(
        kv("Slow charging", datalayer_extended.meb.balancing_request ? "requested" : "not requested"));

    String diagnostic;
    switch (datalayer_extended.meb.battery_diagnostic) {
      case 0:
        diagnostic = "Init";
        break;
      case 1:
        diagnostic = "Battery display";
        break;
      case 4:
        diagnostic = "Battery display OK";
        break;
      case 6:
        diagnostic = "Battery display check";
        break;
      case 7:
        diagnostic = "Fault";
        break;
      default:
        diagnostic = "?";
    }
    s.fields.push_back(kv("Diagnostic", diagnostic));

    String hv_line;
    switch (datalayer_extended.meb.status_HV_PTC_line) {
      case 0:
        hv_line = "Init";
        break;
      case 1:
        hv_line = "No open HV line detected";
        break;
      case 2:
        hv_line = "Open HV line";
        break;
      case 3:
        hv_line = "Fault";
        break;
      default:
        hv_line = "? " + String(datalayer_extended.meb.status_HV_PTC_line);
    }
    s.fields.push_back(kv("HV line status", hv_line));

    s.fields.push_back(
        kv("BMS fault performance", datalayer_extended.meb.BMS_fault_performance ? "Active!" : "Off"));
    s.fields.push_back(kv("BMS fault emergency shutdown crash",
                          datalayer_extended.meb.BMS_fault_emergency_shutdown_crash ? "Active!" : "Off"));
    s.fields.push_back(kv("BMS error shutdown request",
                          datalayer_extended.meb.BMS_error_shutdown_request ? "Active!" : "Inactive"));
    s.fields.push_back(kv("BMS error shutdown", datalayer_extended.meb.BMS_error_shutdown ? "Active!" : "Off"));

    String welded;
    switch (datalayer_extended.meb.BMS_welded_contactors_status) {
      case 0:
        welded = "Init";
        break;
      case 1:
        welded = "No contactor welded";
        break;
      case 2:
        welded = "At least 1 contactor welded";
        break;
      case 3:
        welded = "Protection status detection error";
        break;
      default:
        welded = "?";
    }
    s.fields.push_back(kv("Welded contactors", welded));

    String warning_support;
    switch (datalayer_extended.meb.warning_support) {
      case 0:
        warning_support = "OK";
        break;
      case 1:
        warning_support = "Not OK";
        break;
      case 6:
        warning_support = "Init";
        break;
      case 7:
        warning_support = "Fault";
        break;
      default:
        warning_support = "?";
    }
    s.fields.push_back(kv("Warning support", warning_support));

    String intermediate_voltage_status;
    switch (datalayer_extended.meb.BMS_status_voltage_free) {
      case 0:
        intermediate_voltage_status = "Init";
        break;
      case 1:
        intermediate_voltage_status = "BMS interm circuit voltage free (U<20V)";
        break;
      case 2:
        intermediate_voltage_status = "BMS interm circuit not voltage free (U >= 25V)";
        break;
      case 3:
        intermediate_voltage_status = "Error";
        break;
      default:
        intermediate_voltage_status = "?";
    }
    s.fields.push_back(
        kv("Intermediate voltage", String(datalayer_extended.meb.BMS_voltage_intermediate_dV / 10.0f, 1), "V"));
    s.fields.push_back(kv("Intermediate voltage status", intermediate_voltage_status));

    String bms_error_status;
    switch (datalayer_extended.meb.BMS_error_status) {
      case 0:
        bms_error_status = "Component IO";
        break;
      case 1:
        bms_error_status = "Iso Error 1";
        break;
      case 2:
        bms_error_status = "Iso Error 2";
        break;
      case 3:
        bms_error_status = "Interlock";
        break;
      case 4:
        bms_error_status = "SD";
        break;
      case 5:
        bms_error_status = "Performance red";
        break;
      case 6:
        bms_error_status = "No component function";
        break;
      case 7:
        bms_error_status = "Init";
        break;
      default:
        bms_error_status = "?";
    }
    s.fields.push_back(kv("BMS error status", bms_error_status));

    s.fields.push_back(kv("BMS voltage", String(datalayer_extended.meb.BMS_voltage_dV / 10.0f, 1)));
    s.fields.push_back(kv("OBD MIL", datalayer_extended.meb.BMS_OBD_MIL ? "ON!" : "Off"));
    s.fields.push_back(kv("Red error lamp", datalayer_extended.meb.BMS_error_lamp_req ? "ON!" : "Off"));
    s.fields.push_back(kv("Yellow warning lamp", datalayer_extended.meb.BMS_warning_lamp_req ? "ON!" : "Off"));
    s.fields.push_back(kv("Isolation resistance", String(datalayer_extended.meb.isolation_resistance), "kOhm"));
    s.fields.push_back(kv("Battery heating", datalayer_extended.meb.battery_heating ? "Active!" : "Off"));

    const char* rt_enum[] = {"No", "Error level 1", "Error level 2", "Error level 3"};
    s.fields.push_back(kv("Overcurrent", rt_enum[datalayer_extended.meb.rt_overcurrent & 0x03]));
    s.fields.push_back(kv("CAN fault", rt_enum[datalayer_extended.meb.rt_CAN_fault & 0x03]));
    s.fields.push_back(kv("Overcharged", rt_enum[datalayer_extended.meb.rt_overcharge & 0x03]));
    s.fields.push_back(kv("SOC too high", rt_enum[datalayer_extended.meb.rt_SOC_high & 0x03]));
    s.fields.push_back(kv("SOC too low", rt_enum[datalayer_extended.meb.rt_SOC_low & 0x03]));
    s.fields.push_back(kv("SOC jumping", rt_enum[datalayer_extended.meb.rt_SOC_jumping & 0x03]));
    s.fields.push_back(kv("Temp difference", rt_enum[datalayer_extended.meb.rt_temp_difference & 0x03]));
    s.fields.push_back(kv("Cell overtemp", rt_enum[datalayer_extended.meb.rt_cell_overtemp & 0x03]));
    s.fields.push_back(kv("Cell undertemp", rt_enum[datalayer_extended.meb.rt_cell_undertemp & 0x03]));
    s.fields.push_back(kv("Battery overvoltage", rt_enum[datalayer_extended.meb.rt_battery_overvolt & 0x03]));
    s.fields.push_back(kv("Battery undervoltage", rt_enum[datalayer_extended.meb.rt_battery_undervol & 0x03]));
    s.fields.push_back(kv("Cell overvoltage", rt_enum[datalayer_extended.meb.rt_cell_overvolt & 0x03]));
    s.fields.push_back(kv("Cell undervoltage", rt_enum[datalayer_extended.meb.rt_cell_undervol & 0x03]));
    s.fields.push_back(kv("Cell imbalance", rt_enum[datalayer_extended.meb.rt_cell_imbalance & 0x03]));
    s.fields.push_back(kv("Battery unathorized", rt_enum[datalayer_extended.meb.rt_battery_unathorized & 0x03]));

    if (datalayer_extended.meb.battery_temperature_dC == 875) {  //Raw value 255
      s.fields.push_back(kv("Battery temperature", "ERROR"));
    } else if (datalayer_extended.meb.battery_temperature_dC == 870) {  //Raw value 254
      s.fields.push_back(kv("Battery temperature", "INIT"));
    } else {
      s.fields.push_back(
          kv("Battery temperature", String(datalayer_extended.meb.battery_temperature_dC / 10.f, 1), "°C"));
    }

    for (int i = 0; i < 3; i++) {
      String points;
      for (int j = 0; j < 6; j++) {
        if (j > 0) {
          points += " ";
        }
        points += String(datalayer_extended.meb.temp_points[i * 6 + j], 1);
      }
      s.fields.push_back(kv("Temperature points " + String(i * 6 + 1) + "-" + String(i * 6 + 6), points, "°C"));
    }

    bool temps_done = false;
    for (int i = 0; i < 7 && !temps_done; i++) {
      String cell_temps;
      for (int j = 0; j < 8; j++) {
        if (datalayer_extended.meb.celltemperature_dC[i * 8 + j] == 865) {
          temps_done = true;
          break;
        } else {
          if (j > 0) {
            cell_temps += " ";
          }
          cell_temps += String(datalayer_extended.meb.celltemperature_dC[i * 8 + j] / 10.f, 1);
        }
      }
      s.fields.push_back(kv("Cell temperatures " + String(i * 8 + 1) + "-" + String(i * 8 + 8), cell_temps, "°C"));
    }

    s.fields.push_back(
        kv("Total charged", String(datalayer_battery->status.total_charged_battery_Wh / 1000.0, 1), "kWh"));
    s.fields.push_back(
        kv("Total discharged", String(datalayer_battery->status.total_discharged_battery_Wh / 1000.0, 1), "kWh"));

    status.sections.push_back(s);
    status.sections.push_back(dtc_advanced_section(*this, datalayer_battery->dtc, DtcCodeStyle::kRawHex));
    return status;
  }

 protected:
  void reset_DTC() { datalayer_extended.meb.UserRequestDTCreset = true; }
  void read_DTC() { datalayer_extended.meb.UserRequestDTCreadout = true; }
  void reset_crash() { datalayer_extended.meb.UserRequestCrashReset = true; }

  std::vector<BatteryCommand> commands_ = {
      command(CMD_RESET_DTC, [this] { reset_DTC(); }),
      command(CMD_READ_DTC, [this] { read_DTC(); }),
      command(CMD_RESET_CRASH, [this] { reset_crash(); }),
  };

  /* validate crc for some CAN frames */
  uint8_t vw_crc_calc(const uint8_t* inputBytes, uint8_t length, uint32_t address);
  /* send a UDS ReadDataByIdentifier request for did via ISO-TP */
  void uds_read_data_by_id(uint16_t did, unsigned long currentMillis);
  /* handle a UDS response assembled by the ISO-TP layer */
  void uds_response_handler(const uint8_t* data, int len, enum isotp_tatype type);
  /* drive basic settings state machine — called every transmit_can() tick */
  void handle_basic_settings(unsigned long currentMillis);
  /* drive the BMS reset state machine — called every transmit_can() tick */
  void handle_bms_reset(unsigned long currentMillis);
  /* IsoTp override: send a raw CAN frame */
  void on_isotp_can_tx(uint32_t can_id, const uint8_t* can_data, uint8_t can_dlc) override;
  /* IsoTp override: process an assembled ISO-TP message */
  void on_isotp_rx_complete(const uint8_t* data, int len, isotp_tatype tatype) override;

  DATALAYER_BATTERY_TYPE* datalayer_battery;
  DATALAYER_INFO_MEB* datalayer_meb;

  VAGPlatform platform = VAGPlatform::MEB;

  static const int MAX_PACK_VOLTAGE_84S_DV = 3528;  //5000 = 500.0V
  static const int MIN_PACK_VOLTAGE_84S_DV = 2520;
  static const int MAX_PACK_VOLTAGE_96S_DV = 4032;
  static const int MIN_PACK_VOLTAGE_96S_DV = 2880;
  static const int MAX_PACK_VOLTAGE_108S_DV = 4536;
  static const int MIN_PACK_VOLTAGE_108S_DV = 3240;
  static const int MAX_CELL_DEVIATION_MV = 150;
  static const int MAX_CELL_VOLTAGE_MV = 4250;  //Battery is put into emergency stop if one cell goes over this value
  static const int MIN_CELL_VOLTAGE_MV = 2700;  //Battery is put into emergency stop if one cell goes below this value
  static const int PID_SOC = 0x028C;
  static const int PID_SOH = 0x50CE;  // This isn't available on older firmware versions, 1141 and higher only.
  static const int PID_VOLTAGE = 0x1E3B;
  static const int PID_CURRENT = 0x1E3D;
  static const int PID_MAX_TEMP = 0x1E0E;
  static const int PID_MIN_TEMP = 0x1E0F;
  static const int PID_MAX_CHARGE_VOLTAGE = 0x5171;
  static const int PID_MIN_DISCHARGE_VOLTAGE = 0x5170;
  static const int PID_ENERGY_COUNTERS = 0x1E32;
  static const int PID_ALLOWED_CHARGE_POWER = 0x1E1B;
  static const int PID_ALLOWED_DISCHARGE_POWER = 0x1E1C;
  static const int PID_CELLVOLTAGE_CELL_1 = 0x1E40;
  static const int PID_CELLVOLTAGE_CELL_2 = 0x1E41;
  static const int PID_CELLVOLTAGE_CELL_3 = 0x1E42;
  static const int PID_CELLVOLTAGE_CELL_4 = 0x1E43;
  static const int PID_CELLVOLTAGE_CELL_5 = 0x1E44;
  static const int PID_CELLVOLTAGE_CELL_6 = 0x1E45;
  static const int PID_CELLVOLTAGE_CELL_7 = 0x1E46;
  static const int PID_CELLVOLTAGE_CELL_8 = 0x1E47;
  static const int PID_CELLVOLTAGE_CELL_9 = 0x1E48;
  static const int PID_CELLVOLTAGE_CELL_10 = 0x1E49;
  static const int PID_CELLVOLTAGE_CELL_11 = 0x1E4A;
  static const int PID_CELLVOLTAGE_CELL_12 = 0x1E4B;
  static const int PID_CELLVOLTAGE_CELL_13 = 0x1E4C;
  static const int PID_CELLVOLTAGE_CELL_14 = 0x1E4D;
  static const int PID_CELLVOLTAGE_CELL_15 = 0x1E4E;
  static const int PID_CELLVOLTAGE_CELL_16 = 0x1E4F;
  static const int PID_CELLVOLTAGE_CELL_17 = 0x1E50;
  static const int PID_CELLVOLTAGE_CELL_18 = 0x1E51;
  static const int PID_CELLVOLTAGE_CELL_19 = 0x1E52;
  static const int PID_CELLVOLTAGE_CELL_20 = 0x1E53;
  static const int PID_CELLVOLTAGE_CELL_21 = 0x1E54;
  static const int PID_CELLVOLTAGE_CELL_22 = 0x1E55;
  static const int PID_CELLVOLTAGE_CELL_23 = 0x1E56;
  static const int PID_CELLVOLTAGE_CELL_24 = 0x1E57;
  static const int PID_CELLVOLTAGE_CELL_25 = 0x1E58;
  static const int PID_CELLVOLTAGE_CELL_26 = 0x1E59;
  static const int PID_CELLVOLTAGE_CELL_27 = 0x1E5A;
  static const int PID_CELLVOLTAGE_CELL_28 = 0x1E5B;
  static const int PID_CELLVOLTAGE_CELL_29 = 0x1E5C;
  static const int PID_CELLVOLTAGE_CELL_30 = 0x1E5D;
  static const int PID_CELLVOLTAGE_CELL_31 = 0x1E5E;
  static const int PID_CELLVOLTAGE_CELL_32 = 0x1E5F;
  static const int PID_CELLVOLTAGE_CELL_33 = 0x1E60;
  static const int PID_CELLVOLTAGE_CELL_34 = 0x1E61;
  static const int PID_CELLVOLTAGE_CELL_35 = 0x1E62;
  static const int PID_CELLVOLTAGE_CELL_36 = 0x1E63;
  static const int PID_CELLVOLTAGE_CELL_37 = 0x1E64;
  static const int PID_CELLVOLTAGE_CELL_38 = 0x1E65;
  static const int PID_CELLVOLTAGE_CELL_39 = 0x1E66;
  static const int PID_CELLVOLTAGE_CELL_40 = 0x1E67;
  static const int PID_CELLVOLTAGE_CELL_41 = 0x1E68;
  static const int PID_CELLVOLTAGE_CELL_42 = 0x1E69;
  static const int PID_CELLVOLTAGE_CELL_43 = 0x1E6A;
  static const int PID_CELLVOLTAGE_CELL_44 = 0x1E6B;
  static const int PID_CELLVOLTAGE_CELL_45 = 0x1E6C;
  static const int PID_CELLVOLTAGE_CELL_46 = 0x1E6D;
  static const int PID_CELLVOLTAGE_CELL_47 = 0x1E6E;
  static const int PID_CELLVOLTAGE_CELL_48 = 0x1E6F;
  static const int PID_CELLVOLTAGE_CELL_49 = 0x1E70;
  static const int PID_CELLVOLTAGE_CELL_50 = 0x1E71;
  static const int PID_CELLVOLTAGE_CELL_51 = 0x1E72;
  static const int PID_CELLVOLTAGE_CELL_52 = 0x1E73;
  static const int PID_CELLVOLTAGE_CELL_53 = 0x1E74;
  static const int PID_CELLVOLTAGE_CELL_54 = 0x1E75;
  static const int PID_CELLVOLTAGE_CELL_55 = 0x1E76;
  static const int PID_CELLVOLTAGE_CELL_56 = 0x1E77;
  static const int PID_CELLVOLTAGE_CELL_57 = 0x1E78;
  static const int PID_CELLVOLTAGE_CELL_58 = 0x1E79;
  static const int PID_CELLVOLTAGE_CELL_59 = 0x1E7A;
  static const int PID_CELLVOLTAGE_CELL_60 = 0x1E7B;
  static const int PID_CELLVOLTAGE_CELL_61 = 0x1E7C;
  static const int PID_CELLVOLTAGE_CELL_62 = 0x1E7D;
  static const int PID_CELLVOLTAGE_CELL_63 = 0x1E7E;
  static const int PID_CELLVOLTAGE_CELL_64 = 0x1E7F;
  static const int PID_CELLVOLTAGE_CELL_65 = 0x1E80;
  static const int PID_CELLVOLTAGE_CELL_66 = 0x1E81;
  static const int PID_CELLVOLTAGE_CELL_67 = 0x1E82;
  static const int PID_CELLVOLTAGE_CELL_68 = 0x1E83;
  static const int PID_CELLVOLTAGE_CELL_69 = 0x1E84;
  static const int PID_CELLVOLTAGE_CELL_70 = 0x1E85;
  static const int PID_CELLVOLTAGE_CELL_71 = 0x1E86;
  static const int PID_CELLVOLTAGE_CELL_72 = 0x1E87;
  static const int PID_CELLVOLTAGE_CELL_73 = 0x1E88;
  static const int PID_CELLVOLTAGE_CELL_74 = 0x1E89;
  static const int PID_CELLVOLTAGE_CELL_75 = 0x1E8A;
  static const int PID_CELLVOLTAGE_CELL_76 = 0x1E8B;
  static const int PID_CELLVOLTAGE_CELL_77 = 0x1E8C;
  static const int PID_CELLVOLTAGE_CELL_78 = 0x1E8D;
  static const int PID_CELLVOLTAGE_CELL_79 = 0x1E8E;
  static const int PID_CELLVOLTAGE_CELL_80 = 0x1E8F;
  static const int PID_CELLVOLTAGE_CELL_81 = 0x1E90;
  static const int PID_CELLVOLTAGE_CELL_82 = 0x1E91;
  static const int PID_CELLVOLTAGE_CELL_83 = 0x1E92;
  static const int PID_CELLVOLTAGE_CELL_84 = 0x1E93;
  static const int PID_CELLVOLTAGE_CELL_85 = 0x1E94;
  static const int PID_CELLVOLTAGE_CELL_86 = 0x1E95;
  static const int PID_CELLVOLTAGE_CELL_87 = 0x1E96;
  static const int PID_CELLVOLTAGE_CELL_88 = 0x1E97;
  static const int PID_CELLVOLTAGE_CELL_89 = 0x1E98;
  static const int PID_CELLVOLTAGE_CELL_90 = 0x1E99;
  static const int PID_CELLVOLTAGE_CELL_91 = 0x1E9A;
  static const int PID_CELLVOLTAGE_CELL_92 = 0x1E9B;
  static const int PID_CELLVOLTAGE_CELL_93 = 0x1E9C;
  static const int PID_CELLVOLTAGE_CELL_94 = 0x1E9D;
  static const int PID_CELLVOLTAGE_CELL_95 = 0x1E9E;
  static const int PID_CELLVOLTAGE_CELL_96 = 0x1E9F;
  static const int PID_CELLVOLTAGE_CELL_97 = 0x1EA0;
  static const int PID_CELLVOLTAGE_CELL_98 = 0x1EA1;
  static const int PID_CELLVOLTAGE_CELL_99 = 0x1EA2;
  static const int PID_CELLVOLTAGE_CELL_100 = 0x1EA3;
  static const int PID_CELLVOLTAGE_CELL_101 = 0x1EA4;
  static const int PID_CELLVOLTAGE_CELL_102 = 0x1EA5;
  static const int PID_CELLVOLTAGE_CELL_103 = 0x1EA6;
  static const int PID_CELLVOLTAGE_CELL_104 = 0x1EA7;
  static const int PID_CELLVOLTAGE_CELL_105 = 0x1EA8;
  static const int PID_CELLVOLTAGE_CELL_106 = 0x1EA9;
  static const int PID_CELLVOLTAGE_CELL_107 = 0x1EAA;
  static const int PID_CELLVOLTAGE_CELL_108 = 0x1EAB;
  static const int PID_TEMP_POINT_1 = 0x1EAE;
  static const int PID_TEMP_POINT_2 = 0x1EAF;
  static const int PID_TEMP_POINT_3 = 0x1EB0;
  static const int PID_TEMP_POINT_4 = 0x1EB1;
  static const int PID_TEMP_POINT_5 = 0x1EB2;
  static const int PID_TEMP_POINT_6 = 0x1EB3;
  static const int PID_TEMP_POINT_7 = 0x1EB4;
  static const int PID_TEMP_POINT_8 = 0x1EB5;
  static const int PID_TEMP_POINT_9 = 0x1EB6;
  static const int PID_TEMP_POINT_10 = 0x1EB7;
  static const int PID_TEMP_POINT_11 = 0x1EB8;
  static const int PID_TEMP_POINT_12 = 0x1EB9;
  static const int PID_TEMP_POINT_13 = 0x1EBA;
  static const int PID_TEMP_POINT_14 = 0x1EBB;
  static const int PID_TEMP_POINT_15 = 0x1EBC;
  static const int PID_TEMP_POINT_16 = 0x1EBD;
  static const int PID_TEMP_POINT_17 = 0x1EBE;
  static const int PID_TEMP_POINT_18 = 0x1EBF;

  /* Define CAN ID messages */
  static const int OBD_Hybrid_01_Req = 0x18DA05F1;
  static const int OBD_Hybrid_01_Resp = 0x18DAF105;
  static const int ISO_Hybrid_01_Req = 0x17FC007B;
  static const int ISO_Hybrid_01_Resp = 0x17FE007B;
  static const int ISO_Hybrid_01_Req_FD = 0x1C40007B;
  static const int ISO_Hybrid_01_Resp_FD = 0x1C42007B;
  static const int ISO_Functional_Req_FD = 0x1C410000;
  static const int BMS_04 = 0x5A2;
  static const int BMS_05 = 0x2AF;
  static const int BMS_07 = 0x5CA;
  static const int BMS_11 = 0x16A954A6;
  static const int BMS_20 = 0xCF;
  static const int BMS_21 = 0x12DD54D0;
  static const int BMS_22 = 0x12DD54D1;
  static const int BMS_23 = 0x12DD54D2;
  static const int BMS_24 = 0x1A555550;
  static const int BMS_25 = 0x1A555551;
  static const int BMS_26 = 0x1A5555B0;
  static const int BMS_28 = 0x1A5555B1;
  static const int BMS_29 = 0x16A954F8;
  static const int BMS_31 = 0x1A5555B2;
  static const int BMS_34 = 0x0AF95450;
  static const int BMS_35 = 0x1A555683;
  static const int BMS_CMC_04 = 0x16A954E8;
  static const int BMS_DC_01 = 0x578;
  static const int BMS_HYB_02 = 0x97;
  static const int BMS_HYB_04 = 0x124;
  static const int BMS_HYB_06 = 0x6A3;
  static const int EM_HYB_05 = 0x6A4;
  static const int MSG_HYB_01 = 0x3A6;
  static const int DC_HYB_02 = 0x3AF;
  static const int DCDC_04 = 0xF7;
  static const int Motor_EV_01 = 0x187;
  static const int Airbag_01 = 0x40;
  static const int EM1_01 = 0xC0;
  static const int ESC_51_Auth = 0xFC;
  static const int ESP_21 = 0xFD;
  static const int Diagnose_01 = 0x6B2;
  static const int Temperaturen_01 = 0x1A5555A6;
  static const int Systeminfo_01 = 0x585;
  static const int Reichweite_01 = 0x5F5;
  static const int Motor_Code_01 = 0x641;
  static const int Klemmen_Status_01 = 0x3C0;
  static const int Standklima_01 = 0x16A954FB;
  static const int ORU_01 = 0x1A555548;
  static const int Klima_EV_06 = 0x1A55552B;
  static const int Klima_EV_07 = 0x12DD5513;
  static const int HVEM_04 = 0x569;
  static const int eTM_01 = 0x16A954B4;
  static const int NMH_Gateway = 0x1B000010;
  static const int NMH_Klima = 0x1B000046;
  static const int NMH_Hybrid_01 = 0x1B00007B;
  static const int NMH_DCDC_NV = 0x1B0000B9;
  static const int MSG_HYB_30 = 0x153;
  static const int Klima_Sensor_02 = 0x5E1;
  static const int Motor_14 = 0x3BE;
  static const int Motor_54 = 0x14C;
  static const int HVLM_13 = 0x271;
  static const int HVLM_14 = 0x272;
  static const int HVK_01 = 0x503;
  static const int KN_Hybrid_01 = 0x17F0007B;
  static const int Kombi_02 = 0x6B7;

  unsigned long previousMillis10ms = 0;   // will store last time a 10ms CAN Message was send
  unsigned long previousMillis20ms = 0;   // will store last time a 20ms CAN Message was send
  unsigned long previousMillis40ms = 0;   // will store last time a 40ms CAN Message was send
  unsigned long previousMillis50ms = 0;   // will store last time a 50ms CAN Message was send
  unsigned long previousMillis100ms = 0;  // will store last time a 100ms CAN Message was send
  unsigned long previousMillis200ms = 0;  // will store last time a 200ms CAN Message was send
  unsigned long previousMillis500ms = 0;  // will store last time a 500ms CAN Message was send
  unsigned long previousMillis1s = 0;     // will store last time a 1s CAN Message was send

  bool toggle = false;
  uint8_t counter_1000ms = 0;
  uint8_t counter_200ms = 0;
  uint8_t counter_100ms = 0;
  uint8_t counter_50ms = 0;
  uint8_t counter_40ms = 0;
  uint8_t counter_20ms = 0;
  uint8_t counter_10ms = 0;
  uint8_t counter_040 = 0;
  uint8_t counter_0F7 = 0;
  uint8_t counter_3b5 = 0;
  uint8_t BMS_04_counter = 0;
  uint8_t BMS_07_counter = 0;
  uint8_t BMS_20_counter = 0;
  uint8_t EM1_01_counter = 0;
  uint8_t BMS_DC_01_counter = 0;
  uint8_t BMS_11_counter = 0;
  uint8_t BMS_11_CRC = 0;

  uint32_t poll_pid = PID_CELLVOLTAGE_CELL_85;  // We start here to quickly determine the cell size of the pack.
  bool nof_cells_determined = false;
  uint32_t pid_reply = 0;

  // ISO-TP / UDS request serialization. Only one UDS transaction (PID poll, DTC read) is
  // outstanding at a time; the next request waits until a response arrives or the timeout expires.
  static const int ROUTINE_ID_DTC_DELETE_TRIGGER = 0x0302;
  static constexpr unsigned long BASIC_SETTINGS_ROUTINE_STOP_DELAY_MS = 3000;
  static const int MAX_DTC_COUNT = 32;  // matches dtc_codes[] size in DATALAYER_INFO_MEB
  static constexpr unsigned long UDS_REQUEST_TIMEOUT_MS = 1000;
  bool uds_request_pending = false;
  unsigned long uds_request_timestamp = 0;

  // Generic basic settings state machine.
  enum class BasicSettingsState : uint8_t {
    IDLE = 0,
    SEND_EXT_SESSION,
    WAIT_EXT_SESSION,
    SEND_SEED_REQ,
    WAIT_SEED,
    WAIT_KEY_RESP,
    SEND_ROUTINE_START,
    SEND_ROUTINE_STOP,
    WAIT_ROUTINE_RESULT,
  };
  BasicSettingsState basic_settings_state = BasicSettingsState::IDLE;
  uint16_t basic_settings_routine_id = 0;     // 2-byte routine identifier sent in 31 01 <hi> <lo>
  uint16_t basic_settings_routine_param = 0;  // 2-byte routine parameter sent after the routine ID
  uint32_t security_access_seed = 0;
  uint32_t security_login_key = 20103;       // MEB only, MQB Evo is set in setup() after determining the model.
  unsigned long basic_settings_wait_ms = 0;  // start timestamp between routine steps

  // BMS reset state machine. Pause the battery and wait for the current to drop,
  // request HV_OFF + KL15 off, go silent until the BMS sleeps, wait, then restart.
  enum class BmsResetState : uint8_t { IDLE, WAIT_FOR_PAUSE, REQUEST_HV_OFF, SILENCE, SLEEP_WAIT };
  BmsResetState bms_reset_state = BmsResetState::IDLE;
  unsigned long bms_reset_ms = 0;        // phase start timestamp
  bool bms_reset_active = false;         // forces KL15 OFF + HV_OFF in periodic TX
  bool bms_reset_tx_suppressed = false;  // when true, skip all periodic CAN transmits
  bool startup_bms_checked = false;      // true once we've inspected the first BMS_20 after boot

  static constexpr unsigned long BMS_RESET_PAUSE_TIMEOUT_MS = 10000;    // max wait for current to drop
  static constexpr unsigned long BMS_RESET_HV_OFF_TIMEOUT_MS = 3000;    // max wait for HV_OFF ack
  static constexpr unsigned long BMS_RESET_SILENCE_TIMEOUT_MS = 15000;  // max wait for BMS to sleep
  static constexpr unsigned long BMS_RESET_BMS_SILENT_MS = 1000;        // RX gap that means "asleep"
  static constexpr unsigned long BMS_RESET_SLEEP_MS = 5000;             // bus-quiet wait before restart
  static constexpr uint32_t BMS_CAN_ERR_IGNORE_MS = 2000;               // ignore CAN errors while BMS wakes after reset

  uint16_t battery_soc_polled = 0;
  uint16_t battery_soh_polled = 0;
  uint16_t battery_voltage_polled = 1480;
  int16_t battery_current_polled = 0;
  int16_t battery_max_temp = 600;
  int16_t battery_min_temp = 600;
  uint16_t battery_max_charge_voltage = 0;
  uint16_t battery_min_discharge_voltage = 0;
  uint16_t battery_allowed_charge_power = 0;
  uint16_t battery_allowed_discharge_power = 0;
  uint16_t cellvoltages_polled[108];
  uint16_t tempval = 0;
  bool BMS_ext_limits_active =
      false;  //The current current limits of the HV battery are expanded to start the combustion engine / confirmation of the request
  uint8_t BMS_mode =
      0x07;  //0: standby; Gates open; Communication active 1: Main contactor closed / HV network activated / normal driving operation
  //2: assigned depending on the project (e.g. balancing, extended DC fast charging) //3: external charging
  uint8_t BMS_HVIL_status = 0;         //0 init, 1 seated, 2 open, 3 fault
  bool BMS_fault_performance = false;  //Error: Battery performance is limited (e.g. due to sensor or fan failure)
  uint16_t BMS_current = 16300;
  bool BMS_fault_emergency_shutdown_crash =
      false;  //Error: Safety-critical error (crash detection) Battery contactors are already opened / will be opened immediately Signal is read directly by the EMS and initiates an AKS of the PWR and an active discharge of the DC link
  uint32_t BMS_voltage_intermediate = 2000;
  uint32_t BMS_voltage = 1480;
  int32_t BMS_usable_batt_energy_Wh = 0;
  int32_t BMS_usable_batt_energy_t_Wh = 0;
  int32_t BMS_max_usable_batt_energy_Wh = 0;
  uint16_t BMS_nominal_voltage_dV = 0;
  uint8_t BMS_status_voltage_free =
      0;  //0=Init, 1=BMS intermediate circuit voltage-free (U_Zwkr < 20V), 2=BMS intermediate circuit not voltage-free (U_Zwkr >/= 25V, hysteresis), 3=Error
  bool BMS_OBD_MIL = false;
  uint8_t BMS_error_status =
      0x7;  //0 Component_IO, 1 Restricted_CompFkt_Isoerror_I, 2 Restricted_CompFkt_Isoerror_II, 3 Restricted_CompFkt_Interlock, 4 Restricted_CompFkt_SD, 5 Restricted_CompFkt_Performance red, 6 = No component function, 7 = Init
  uint16_t BMS_capacity_ah = 0;
  bool BMS_error_lamp_req = false;
  bool BMS_warning_lamp_req = false;
  uint8_t BMS_Kl30c_Status = 0;  // 0 init, 1 closed, 2 open, 3 fault
  bool service_disconnect_switch_missing = false;
  bool pilotline_open = false;
  bool balancing_request = false;
  uint8_t battery_diagnostic = 0;
  uint16_t battery_Wh_left = 0;
  uint16_t battery_Wh_max = 1000;
  uint8_t battery_potential_status = 0;
  uint8_t battery_temperature_warning = 0;
  uint16_t max_discharge_power_watt = 0;
  uint16_t max_discharge_current_amp = 0;
  uint16_t max_charge_power_watt = 0;
  uint16_t max_charge_current_amp = 0;
  uint16_t battery_SOC = 1;
  uint16_t usable_energy_amount_Wh = 0;
  uint8_t status_HV_PTC_line = 0;  //0 init, 1 No open HV line, 2 open HV line detected, 3 fault (Heater HV connector)
  uint8_t warning_support = 0;
  bool battery_heating_active = false;
  uint16_t power_discharge_percentage = 0;
  uint16_t power_charge_percentage = 0;
  uint16_t actual_battery_voltage = 0;
  uint16_t regen_battery = 0;
  uint16_t energy_extracted_from_battery = 0;
  uint16_t max_fastcharging_current_amp = 0;  //maximum DC charging current allowed
  uint8_t BMS_Status_DCLS =
      0;  //Status of the voltage monitoring on the DC charging interface. 0 inactive, 1 I_O , 2 N_I_O , 3 active
  uint16_t DC_voltage_DCLS = 0;  //Factor 1, DC voltage of the charging station. Measurement between the DC HV lines.
  uint16_t DC_voltage_chargeport =
      0;  //Factor 0.5,  Current voltage at the HV battery DC charging terminals; Outlet to the DC charging plug.
  uint8_t BMS_welded_contactors_status =
      0;  //0: Init no diagnostic result, 1: no contactor welded, 2: at least 1 contactor welded, 3: Protection status detection error
  bool BMS_error_shutdown_request =
      false;  // Fault: Fault condition, requires battery contactors to be opened internal battery error; Advance notification of an impending opening of the battery contactors by the BMS
  bool BMS_error_shutdown =
      false;  // Fault: Fault condition, requires battery contactors to be opened Internal battery error, battery contactors opened without notice by the BMS
  uint16_t power_battery_heating_watt = 0;
  uint16_t power_battery_heating_req_watt = 0;
  uint8_t cooling_request =
      0;  //0 = No cooling, 1 = Light cooling, cabin prio, 2= higher cooling, 3 = immediate cooling, 4 = emergency cooling
  uint8_t heating_request = 0;       //0 = init, 1= maintain temp, 2=higher demand, 3 = immediate heat demand
  uint8_t balancing_active = false;  //0 = init, 1 active, 2 not active
  bool charging_active = false;
  uint16_t max_energy_Wh = 0;
  uint16_t max_charge_percent = 0;
  uint16_t min_charge_percent = 0;
  uint16_t isolation_resistance_kOhm = 0;
  bool battery_heating_installed = false;
  bool error_NT_circuit = false;
  uint8_t pump_1_control = 0;             //0x0D not installed, 0x0E init, 0x0F fault
  uint8_t pump_2_control = 0;             //0x0D not installed, 0x0E init, 0x0F fault
  uint8_t target_flow_temperature_C = 0;  //*0,5 -40
  uint8_t return_temperature_C = 0;       //*0,5 -40
  uint8_t status_valve_1 = 0;             //0 not active, 1 active, 5 not installed, 6 init, 7 fault
  uint8_t status_valve_2 = 0;             //0 not active, 1 active, 5 not installed, 6 init, 7 fault
  uint8_t temperature_request =
      0;  //0 high cooling, 1 medium cooling, 2 low cooling, 3 no temp requirement init, 4 low heating , 5 medium heating, 6 high heating, 7 circulation
  uint16_t performance_index_discharge_peak_temperature_percentage = 0;
  uint16_t performance_index_charge_peak_temperature_percentage = 0;
  uint8_t temperature_status_discharge =
      0;                                  //0 init, 1 temp under optimal, 2 temp optimal, 3 temp over optimal, 7 fault
  uint8_t temperature_status_charge = 0;  //0 init, 1 temp under optimal, 2 temp optimal, 3 temp over optimal, 7 fault
  uint8_t isolation_fault =
      0;  //0 init, 1 no iso fault, 2 iso fault threshold1, 3 iso fault threshold2, 4 IWU defective
  uint8_t isolation_status =
      0;  // 0 init, 1 = larger threshold1, 2 = smaller threshold1 total, 3 = smaller threshold1 intern, 4 = smaller threshold2 total, 5 = smaller threshold2 intern, 6 = no measurement, 7 = measurement active
  uint8_t actual_temperature_highest_C = 0;    //*0,5 -40
  uint8_t actual_temperature_lowest_C = 0;     //*0,5 -40
  uint16_t actual_cellvoltage_highest_mV = 0;  //bias 1000
  uint16_t actual_cellvoltage_lowest_mV = 0;   //bias 1000
  uint16_t predicted_power_dyn_standard_watt = 0;
  uint8_t predicted_time_dyn_standard_minutes = 0;
  uint8_t mux = 0;
  //uint16_t cellvoltages[160] = {0};
  uint16_t duration_discharge_power_watt = 0;
  uint16_t duration_charge_power_watt = 0;
  uint16_t maximum_voltage = 0;
  uint16_t minimum_voltage = 0;
  uint8_t battery_serialnumber[26];
  uint8_t realtime_overcurrent_monitor = 0;
  uint8_t realtime_CAN_communication_fault = 0;
  uint8_t realtime_overcharge_warning = 0;
  uint8_t realtime_SOC_too_high = 0;
  uint8_t realtime_SOC_too_low = 0;
  uint8_t realtime_SOC_jumping_warning = 0;
  uint8_t realtime_temperature_difference_warning = 0;
  uint8_t realtime_cell_overtemperature_warning = 0;
  uint8_t realtime_cell_undertemperature_warning = 0;
  uint8_t realtime_battery_overvoltage_warning = 0;
  uint8_t realtime_battery_undervoltage_warning = 0;
  uint8_t realtime_cell_overvoltage_warning = 0;
  uint8_t realtime_cell_undervoltage_warning = 0;
  uint8_t realtime_cell_imbalance_warning = 0;
  uint8_t realtime_warning_battery_unathorized = 0;
  bool component_protection_active = false;
  bool shutdown_active = false;
  bool transportation_mode_active = false;
  uint8_t KL15_mode = 0;
  uint8_t bus_knockout_timer = 0;
  uint8_t hybrid_wakeup_reason = 0;
  uint8_t wakeup_type = 0;
  bool instrument_cluster_request = false;
  uint8_t seconds = 0;
  unsigned long first_can_msg_timestamp = 0;
  unsigned long last_can_msg_timestamp = 0;
  bool hv_requested = false;
  int32_t kwh_charge = 0;
  int32_t kwh_discharge = 0;

#define TIME_YEAR 2024
#define TIME_MONTH 8
#define TIME_DAY 20
#define TIME_HOUR 10
#define TIME_MINUTE 5
#define TIME_SECOND 0

#define BMS_TARGET_HV_OFF 0
#define BMS_TARGET_HV_ON 1
#define BMS_TARGET_AC_CHARGING_EXT 3  //(HS + AC_2 contactors closed)
#define BMS_TARGET_AC_CHARGING 4      //(HS + AC contactors closed)
#define BMS_TARGET_DC_CHARGING 6      //(HS + DC contactors closed)
#define BMS_TARGET_INIT 7

#define DC_FASTCHARGE_NO_START_REQUEST 0x00
#define DC_FASTCHARGE_VEHICLE 0x40
#define DC_FASTCHARGE_LS1 0x80
#define DC_FASTCHARGE_LS2 0xC0

  // PID polling and multi-frame reassembly are handled by the inherited IsoTp layer
  // (see uds_read_data_by_id() / on_isotp_rx_complete()).
  static constexpr CAN_frame OBD_CLEAR_DTC = {.FD = true,
                                              .ext_ID = true,
                                              .DLC = 8,
                                              .ID = ISO_Functional_Req_FD,
                                              .data = {0x01, 0x04, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55}};
  // TesterPresent (3E 80) sent to the BMS physical address to keep the extended session alive.
  // Bit7 of subFunction = 1 suppresses the positive response, so no UDS reply is expected.
  static constexpr CAN_frame Tester_present_frame = {.FD = true,
                                                     .ext_ID = true,
                                                     .DLC = 8,
                                                     .ID = ISO_Hybrid_01_Req_FD,
                                                     .data = {0x02, 0x3E, 0x80, 0x55, 0x55, 0x55, 0x55, 0x55}};
  //Messages needed for contactor closing
  CAN_frame Airbag_01_frame = {.FD = true,  // Airbag
                               .ext_ID = false,
                               .DLC = 8,
                               .ID = Airbag_01,  //Frame5 has HV deactivate request. Needs to be 0x00
                               .data = {0x7E, 0x83, 0x00, 0x01, 0x00, 0x00, 0x15, 0x00}};
  CAN_frame EM1_01_frame = {
      .FD = true,  // EM1 message
      .ext_ID = false,
      .DLC = 32,
      .ID = EM1_01,  //Frame 5-6 and maybe 7-8 important (external voltage at inverter)
      .data = {0x77, 0x0A, 0xFE, 0xE7, 0x7F, 0x10, 0x27, 0x00, 0xE0, 0x7F, 0xFF, 0xF3, 0x3F, 0xFF, 0xF3, 0x3F,
               0xFC, 0x0F, 0x00, 0x00, 0xC0, 0xFF, 0xFE, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame ESC_51_Auth_frame = {
      .FD = true,  //
      .ext_ID = false,
      .DLC = 48,
      .ID = ESC_51_Auth,  //This message contains emergency regen request?(byte19), battery needs to see this message
      .data = {0x07, 0x08, 0x00, 0x00, 0x7E, 0x00, 0x40, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0xFE, 0xFE, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x00, 0xF4, 0x01, 0x40, 0xFF, 0xEB, 0x7F, 0x0A, 0x88, 0xE3, 0x81, 0xAF, 0x42}};
  CAN_frame Diagnose_01_frame = {.FD = true,  // Diagnostics
                                 .ext_ID = false,
                                 .DLC = 8,
                                 .ID = Diagnose_01,
                                 .data = {0x6A, 0xA7, 0x37, 0x80, 0xC9, 0xBD, 0xF6, 0xC2}};
  CAN_frame Temperaturen_01_frame = {.FD = true,
                                     .ext_ID = true,
                                     .DLC = 8,
                                     .ID = Temperaturen_01,
                                     .data = {0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00}};
  static constexpr CAN_frame Systeminfo_01_frame = {
      .FD = true,
      .ext_ID = false,
      .DLC = 8,
      .ID = Systeminfo_01,
      .data = {0xCF, 0x38, 0xAF, 0x5B, 0x25, 0x00, 0x00, 0x00}};  // CF 38 AF 5B 25 00 00 00 in start4.log
  //                     .data = {0xCF, 0x38, 0x20, 0x02, 0x25, 0xF7, 0x30, 0x00}}; // CF 38 AF 5B 25 00 00 00 in start4.log
  static constexpr CAN_frame Reichweite_01_frame = {.FD = true,
                                                    .ext_ID = false,
                                                    .DLC = 8,
                                                    .ID = Reichweite_01,
                                                    .data = {0x23, 0x02, 0x39, 0xC0, 0x1B, 0x8B, 0xC8, 0x1B}};
  CAN_frame Motor_Code_01_frame = {.FD = true,
                                   .ext_ID = false,
                                   .DLC = 8,
                                   .ID = Motor_Code_01,
                                   .data = {0x37, 0x18, 0x00, 0x00, 0xF0, 0x00, 0xAA, 0x70}};
  CAN_frame Klemmen_Status_01_frame = {.FD = true,
                                       .ext_ID = false,
                                       .DLC = 4,
                                       .ID = Klemmen_Status_01,
                                       .data = {0x66, 0x00, 0x00, 0x00}};  // Klemmen_status_01
  CAN_frame ESP_21_frame = {.FD = true,
                            .ext_ID = false,
                            .DLC = 8,
                            .ID = ESP_21,  //CRC and counter, otherwise static
                            .data = {0x5F, 0xD0, 0x1F, 0x81, 0x00, 0x00, 0x00, 0x00}};
  static constexpr CAN_frame Standklima_01_frame = {.FD = true,
                                                    .ext_ID = true,
                                                    .DLC = 8,
                                                    .ID = Standklima_01,
                                                    .data = {0x00, 0xC0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00}};
  static constexpr CAN_frame ORU_01_frame = {.FD = true,
                                             .ext_ID = true,
                                             .DLC = 8,
                                             .ID = ORU_01,
                                             .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  static constexpr CAN_frame Klima_EV_06_frame = {.FD = true,
                                                  .ext_ID = true,
                                                  .DLC = 8,
                                                  .ID = Klima_EV_06,
                                                  .data = {0x00, 0x00, 0x00, 0xA0, 0x02, 0x04, 0x00, 0x30}};
  static constexpr CAN_frame Klima_EV_07_frame = {.FD = true,
                                                  .ext_ID = true,
                                                  .DLC = 8,
                                                  .ID = Klima_EV_07,
                                                  .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  static constexpr CAN_frame HVEM_04_frame = {.FD = true,
                                              .ext_ID = false,
                                              .DLC = 8,
                                              .ID = HVEM_04,  //HVEM
                                              .data = {0x00, 0x00, 0x01, 0x3A, 0x00, 0x00, 0x00, 0x00}};
  static constexpr CAN_frame eTM_01_frame = {.FD = true,
                                             .ext_ID = true,
                                             .DLC = 8,
                                             .ID = eTM_01,  //eTM
                                             .data = {0xFE, 0xB6, 0x0D, 0x00, 0x00, 0xD5, 0x48, 0xFD}};
  static constexpr CAN_frame NMH_Klima_frame = {.FD = false,  // Not FD
                                                .ext_ID = true,
                                                .DLC = 8,
                                                .ID = NMH_Klima,  // Klima
                                                .data = {0x00, 0x40, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame NMH_Gateway_frame = {.FD = false,  // Not FD
                                 .ext_ID = true,
                                 .DLC = 8,
                                 .ID = NMH_Gateway,  // Gateway
                                 .data = {0x00, 0x50, 0x04, 0x51, 0x19, 0xF7, 0xB0, 0x00}};
  static constexpr CAN_frame NMH_DCDC_NV_frame = {.FD = false,  // Not FD
                                                  .ext_ID = true,
                                                  .DLC = 8,
                                                  .ID = NMH_DCDC_NV,  //DC/DC converter
                                                  .data = {0x00, 0x40, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00}};
  static constexpr CAN_frame MSG_HYB_30_frame = {.FD = true,
                                                 .ext_ID = false,
                                                 .DLC = 8,
                                                 .ID = MSG_HYB_30,  // content
                                                 .data = {0x00, 0x00, 0x00, 0xFF, 0xEF, 0xFE, 0xFF, 0xFF}};
  static constexpr CAN_frame Klima_Sensor_02_frame = {.FD = true,
                                                      .ext_ID = false,
                                                      .DLC = 8,
                                                      .ID = Klima_Sensor_02,  // content
                                                      .data = {0x7F, 0x2A, 0x00, 0x60, 0xFE, 0x00, 0x00, 0x00}};
  CAN_frame Motor_14_frame = {.FD = true,
                              .ext_ID = false,
                              .DLC = 8,
                              .ID = Motor_14,  // CRC, otherwise content
                              .data = {0x57, 0x0D, 0x00, 0x00, 0x00, 0x02, 0x04, 0x40}};
  CAN_frame HVLM_13_frame = {.FD = true,  //HVLM_13
                             .ext_ID = false,
                             .DLC = 8,
                             .ID = HVLM_13,  // content still TODO
                             .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame HVLM_14_frame = {.FD = true,  //HVLM_14
                             .ext_ID = false,
                             .DLC = 8,
                             .ID = HVLM_14,  // content
                             .data = {0x00, 0x00, 0x00, 0x00, 0x48, 0x08, 0x00, 0x94}};
  CAN_frame HVK_01_frame = {.FD = true,  //HVK_01
                            .ext_ID = false,
                            .DLC = 8,
                            .ID = HVK_01,  // Content varies. Frame1 & 3 has HV req
                            .data = {0x5D, 0x61, 0x00, 0xFF, 0x7F, 0x80, 0xE3, 0x03}};
  CAN_frame Motor_54_frame = {
      .FD = true,  //Motor message
      .ext_ID = false,
      .DLC = 32,
      .ID = Motor_54,  //CRC needed, content otherwise
      .data = {0x38, 0x0A, 0xFF, 0x01, 0x01, 0xFF, 0x01, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE,
               0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x25, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE}};
  static constexpr CAN_frame Kombi_02_frame = {.FD = false,
                                               .ext_ID = false,
                                               .DLC = 8,
                                               .ID = Kombi_02,  // content
                                               .data = {0xFE, 0xFF, 0xEF, 0xFF, 0x3F, 0x1C, 0x02, 0x94}};

  CAN_frame Motor_EV_01_frame = {.FD = true,
                                 .ext_ID = false,
                                 .DLC = 8,
                                 .ID = Motor_EV_01,  // content
                                 .data = {0x00, 0x80, 0x12, 0x00, 0x00, 0x00, 0x30, 0x96}};
  uint32_t can_msg_received = 0;
};

// MQB Evo shares all of the MEB CAN handling; only the one-time setup differs.
class MqbEvoBattery : public MebBattery {
 public:
  MqbEvoBattery(const BatterySlotContext& ctx) : MebBattery(ctx) {}
  static constexpr const char* Name = "VW Group MQB Evo 2024+ via CAN-FD";
  void setup(void) override;
  const char* get_dtc_json_filename() override { return "vag_mqb_dtc.json"; }
};

#endif
