#ifndef KIA_HYUNDAI_64_BATTERY_H
#define KIA_HYUNDAI_64_BATTERY_H
#include <cstring>
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "BatterySlotContext.h"
#include "CanBattery.h"

class KiaHyundai64Battery : public CanBattery {
 public:
  KiaHyundai64Battery(const BatterySlotContext& ctx) : CanBattery(ctx.can_interface) {
    datalayer_battery = ctx.datalayer;
    allows_contactor_closing = ctx.is_primary() ? ctx.contactor_flag : nullptr;
    contactor_closing_allowed = ctx.is_primary() ? nullptr : ctx.contactor_flag;
    datalayer_battery_extended = &extended_;
  }

  virtual void setup(void);
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);

  const std::vector<BatteryCommand>& get_commands() override { return commands_; }

  bool supports_insulation_resistance() override { return true; }

  BatteryAdvancedStatus get_advanced_status() override {
    BatteryAdvancedStatus status;
    DATALAYER_INFO_KIAHYUNDAI64* kia_datalayer = &extended_;
    AdvancedSection s;
    auto build_fields = [&s](DATALAYER_INFO_KIAHYUNDAI64& data) {
      char readableSerialNumber[17];  // One extra space for null terminator
      memcpy(readableSerialNumber, data.ecu_serial_number, sizeof(data.ecu_serial_number));
      readableSerialNumber[16] = '\0';  // Null terminate the string
      char readableVersionNumber[17];   // One extra space for null terminator
      memcpy(readableVersionNumber, data.ecu_version_number, sizeof(data.ecu_version_number));
      readableVersionNumber[16] = '\0';  // Null terminate the string

      s.fields.push_back(kv("BMS serial number", String(readableSerialNumber)));
      s.fields.push_back(kv("BMS software version", String(readableVersionNumber)));
      s.fields.push_back(kv("Cells", String(data.total_cell_count), "S"));
      s.fields.push_back(kv("12V voltage", String(data.battery_12V / 10.0f, 1), "V"));

      String waterleakage;
      if (data.waterleakageSensor == 0) {
        waterleakage = "LEAK DETECTED";
      } else if (data.waterleakageSensor == 164) {
        waterleakage = "No leakage";
      } else {
        waterleakage = String(data.waterleakageSensor);
      }
      s.fields.push_back(kv("Waterleakage", waterleakage));

      s.fields.push_back(kv("Temperature, water inlet", String(data.temperature_water_inlet), "°C"));
      s.fields.push_back(kv("Temperature, power relay", String(data.powerRelayTemperature), "°C"));
      s.fields.push_back(kv("Batterymanagement mode", String(data.batteryManagementMode)));
      s.fields.push_back(kv("BMS ignition", String(data.BMS_ign)));
      s.fields.push_back(kv("Battery relay", String(data.batteryRelay)));
      s.fields.push_back(kv("Inverter voltage", String(data.inverterVoltage), "V"));
      s.fields.push_back(kv("Isolation resistance", String(data.isolation_resistance_kOhm), "kOhm"));
      s.fields.push_back(kv("Power on total time", String(data.powered_on_total_time), "s"));
      s.fields.push_back(kv("Fastcharging sessions", String(data.number_of_fastcharging_sessions), "x"));
      s.fields.push_back(kv("Slowcharging sessions", String(data.number_of_standard_charging_sessions), "x"));
      s.fields.push_back(
          kv("Normal charged energy amount", String(data.accumulated_normal_charging_energy_kWh), "kWh"));
      s.fields.push_back(kv("Fastcharged energy amount", String(data.accumulated_fastcharging_energy_kWh), "kWh"));
      s.fields.push_back(
          kv("Total amount charged energy", String(data.cumulative_energy_charged_kWh / 10.0), "kWh"));
      s.fields.push_back(
          kv("Total amount discharged energy", String(data.cumulative_energy_discharged_kWh / 10.0), "kWh"));
      s.fields.push_back(kv("Cumulative charge current", String(data.cumulative_charge_current_ah / 10.0), "Ah"));
      s.fields.push_back(
          kv("Cumulative discharge current", String(data.cumulative_discharge_current_ah / 10.0), "Ah"));
    };

    build_fields(*kia_datalayer);

    status.sections.push_back(s);
    return status;
  }

 private:
  void reset_DTC() { UserRequestDTCreset = true; }

  std::vector<BatteryCommand> commands_ = {
      command(CMD_RESET_DTC, [this] { reset_DTC(); }),
  };

  DATALAYER_INFO_KIAHYUNDAI64 extended_;

  DATALAYER_BATTERY_TYPE* datalayer_battery;
  DATALAYER_INFO_KIAHYUNDAI64* datalayer_battery_extended;

  bool UserRequestDTCreset = false;

  // If not null, this battery decides when the contactor can be closed and writes the value here.
  bool* allows_contactor_closing;

  // If not null, this battery listens to this boolean to determine whether contactor closing is allowed
  bool* contactor_closing_allowed;

  void update_number_of_cells();

  static const int MAX_PACK_VOLTAGE_98S_DV = 4110;  //5000 = 500.0V
  static const int MIN_PACK_VOLTAGE_98S_DV = 2800;
  static const int MAX_PACK_VOLTAGE_90S_DV = 3870;
  static const int MIN_PACK_VOLTAGE_90S_DV = 2250;
  static const int MAX_CELL_DEVIATION_MV = 150;
  static const int MAX_CELL_VOLTAGE_MV = 4250;  //Battery is put into emergency stop if one cell goes over this value
  static const int MIN_CELL_VOLTAGE_MV = 2950;  //Battery is put into emergency stop if one cell goes below this value

  unsigned long previousMillis100 = 0;  // will store last time a 100ms CAN Message was send
  unsigned long previousMillis10 = 0;   // will store last time a 10s CAN Message was send

  uint16_t soc_calculated = 0;
  uint16_t SOC_BMS = 0;
  uint16_t SOC_Display = 0;
  uint16_t batterySOH = 1000;
  uint16_t CellVoltMax_mV = 3700;
  uint16_t CellVoltMin_mV = 3700;
  uint16_t allowedDischargePower = 0;
  uint16_t allowedChargePower = 0;
  uint16_t batteryVoltage = 3700;
  uint16_t inverterVoltageFrameHigh = 0;
  uint16_t inverterVoltage = 0;
  uint16_t cellvoltages_mv[98];
  uint16_t leadAcidBatteryVoltage = 120;
  int16_t batteryAmps = 0;
  int16_t temperatureMax = 0;
  int16_t temperatureMin = 0;
  uint8_t poll_data_pid = 0;
  uint8_t open_state = 0;
  uint16_t pid_reply = 0;
  bool holdPidCounter = false;
  uint8_t CellVmaxNo = 0;
  uint8_t CellVminNo = 0;
  uint8_t batteryManagementMode = 0;
  uint8_t BMS_ign = 0;
  uint8_t batteryRelay = 0;
  uint8_t waterleakageSensor = 164;
  uint8_t counter_200 = 0;
  int8_t temperature_water_inlet = 0;
  int8_t heatertemp = 0;
  int8_t powerRelayTemperature = 0;
  bool startedUp = false;
  uint8_t ecu_serial_number[16] = {0};
  uint8_t ecu_version_number[16] = {0};
  uint32_t cumulative_charge_current_ah = 0;
  uint32_t cumulative_discharge_current_ah = 0;
  uint32_t cumulative_energy_charged_kWh = 0;
  uint16_t cumulative_energy_discharged_HIGH_BYTE = 0;
  uint32_t cumulative_energy_discharged_kWh = 0;
  uint32_t powered_on_total_time = 0;
  uint16_t isolation_resistance_kOhm = 0;
  uint16_t number_of_standard_charging_sessions = 0;
  uint16_t number_of_fastcharging_sessions = 0;
  uint16_t accumulated_normal_charging_energy_kWh = 0;
  uint16_t accumulated_fastcharging_energy_kWh = 0;

  CAN_frame KIA_HYUNDAI_200 = {.FD = false,
                               .ext_ID = false,
                               .DLC = 8,
                               .ID = 0x200,
                               .data = {0x00, 0x80, 0xD8, 0x04, 0x00, 0x17, 0xD0, 0x00}};
  CAN_frame KIA_HYUNDAI_523 = {.FD = false,
                               .ext_ID = false,
                               .DLC = 8,
                               .ID = 0x523,
                               .data = {0x08, 0x38, 0x36, 0x36, 0x33, 0x34, 0x00, 0x01}};
  CAN_frame KIA_HYUNDAI_524 = {.FD = false,
                               .ext_ID = false,
                               .DLC = 8,
                               .ID = 0x524,
                               .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  //553 Needed frame 200ms
  CAN_frame KIA64_553 = {.FD = false,
                         .ext_ID = false,
                         .DLC = 8,
                         .ID = 0x553,
                         .data = {0x04, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00}};
  //57F Needed frame 100ms
  CAN_frame KIA64_57F = {.FD = false,
                         .ext_ID = false,
                         .DLC = 8,
                         .ID = 0x57F,
                         .data = {0x80, 0x0A, 0x72, 0x00, 0x00, 0x00, 0x00, 0x72}};
  //Needed frame 100ms
  CAN_frame KIA64_2A1 = {.FD = false,
                         .ext_ID = false,
                         .DLC = 8,
                         .ID = 0x2A1,
                         .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame KIA64_7E4_OPEN_CONTACTOR_SEQUENCE = {.FD = false,
                                                 .ext_ID = false,
                                                 .DLC = 8,
                                                 .ID = 0x7E4,
                                                 .data = {0x02, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame KIA64_7E4_poll = {.FD = false,
                              .ext_ID = false,
                              .DLC = 8,
                              .ID = 0x7E4,
                              .data = {0x03, 0x22, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame KIA64_7E4_ack = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x7E4,
      .data = {0x30, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00}};  //Ack frame, correct PID is returned
  CAN_frame KIA64_CLEAR_DTC = {.FD = false,
                               .ext_ID = false,
                               .DLC = 8,
                               .ID = 0x7E4,
                               .data = {0x04, 0x14, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00}};
  static const int POLL_GROUP_1 = 0x0101;
  static const int POLL_GROUP_2 = 0x0102;
  static const int POLL_GROUP_3 = 0x0103;
  static const int POLL_GROUP_4 = 0x0104;
  static const int POLL_GROUP_5 = 0x0105;
  static const int POLL_GROUP_6 = 0x0106;
  static const int POLL_GROUP_11 = 0x0111;
  static const int POLL_ECU_SERIAL = 0xF18C;
  static const int POLL_ECU_VERSION = 0xF191;
};

#endif
