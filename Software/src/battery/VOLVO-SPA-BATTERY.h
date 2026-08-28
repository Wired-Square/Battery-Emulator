#ifndef VOLVO_SPA_BATTERY_H
#define VOLVO_SPA_BATTERY_H

#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "../devboard/webserver/advanced_api.h"
#include "BatterySlotContext.h"
#include "CanBattery.h"

class VolvoSpaBattery : public CanBattery {
 public:
  VolvoSpaBattery(const BatterySlotContext& ctx) : CanBattery(ctx.can_interface) { datalayer_battery = ctx.datalayer; }
  virtual void setup(void);
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);

  const std::vector<BatteryCommand>& get_commands() override { return commands_; }

  const char* get_dtc_json_filename() override { return "volvo_SPA_dtc.json"; }

  void write_advanced_status(AdvancedStatusWriter& out) override {
    out.section("");

    out.kv(TL("BECM supply voltage"), String(datalayer_extended.VolvoPolestar.BECMsupplyVoltage), "mV");

    out.kv(TL("Dynamic max voltage"), String(datalayer_extended.VolvoPolestar.BECMUDynMaxLim), "V");
    out.kv(TL("Dynamic min voltage"), String(datalayer_extended.VolvoPolestar.BECMUDynMinLim), "V");

    out.kv(TL("Discharge power limit 1"), String(datalayer_extended.VolvoPolestar.HvBattPwrLimDcha1), "kW");
    out.kv(TL("Discharge soft power limit"), String(datalayer_extended.VolvoPolestar.HvBattPwrLimDchaSoft), "kW");
    out.kv(TL("Discharge power limit slow aging"),
                          String(datalayer_extended.VolvoPolestar.HvBattPwrLimDchaSlowAgi), "kW");
    out.kv(TL("Charge power limit slow aging"), String(datalayer_extended.VolvoPolestar.HvBattPwrLimChrgSlowAgi), "kW");

    String hvil_a;
    switch (datalayer_extended.VolvoPolestar.HVILstatusBits & 0x01) {
      case 0x01:
        hvil_a = "Open";
        break;
      default:
        hvil_a = "Not valid";
    }
    out.kv(TL("HVIL Circuit A status"), hvil_a);

    String hvil_b;
    switch (datalayer_extended.VolvoPolestar.HVILstatusBits & 0x02) {
      case 0x02:
        hvil_b = "Open";
        break;
      default:
        hvil_b = "Closed";
    }
    out.kv(TL("HVIL Circuit B status"), hvil_b);

    String hvil_c;
    switch (datalayer_extended.VolvoPolestar.HVILstatusBits & 0x04) {
      case 0x04:
        hvil_c = "Open";
        break;
      default:
        hvil_c = "Closed";
    }
    out.kv(TL("HVIL Circuit C status"), hvil_c);

    String precharge_contactor;
    switch (datalayer_extended.VolvoPolestar.HVILstatusBits & 0x08) {
      case 0x08:
        precharge_contactor = "Open";
        break;
      default:
        precharge_contactor = "Closed";
    }
    out.kv(TL("Precharge contactor status"), precharge_contactor);

    String positive_contactor;
    switch (datalayer_extended.VolvoPolestar.HVILstatusBits & 0x10) {
      case 0x10:
        positive_contactor = "Open";
        break;
      default:
        positive_contactor = "Closed";
    }
    out.kv(TL("Positive Contactor status"), positive_contactor);

    String negative_contactor;
    switch (datalayer_extended.VolvoPolestar.HVILstatusBits & 0x20) {
      case 0x20:
        negative_contactor = "Open";
        break;
      default:
        negative_contactor = "Closed";
    }
    out.kv(TL("Negative Contactor status"), negative_contactor);

    String hv_sys_relay_status;
    switch (datalayer_extended.VolvoPolestar.HVSysRlySts) {
      case 0:
        hv_sys_relay_status = "Open";
        break;
      case 1:
        hv_sys_relay_status = "Closed";
        break;
      case 2:
        hv_sys_relay_status = "KeepStatus";
        break;
      case 3:
        hv_sys_relay_status = "OpenAndRequestActiveDischarge";
        break;
      default:
        hv_sys_relay_status = "Not valid";
    }
    out.kv(TL("HV system relay status"), hv_sys_relay_status);

    String hv_sys_dc_relay_status1;
    switch (datalayer_extended.VolvoPolestar.HVSysDCRlySts1) {
      case 0:
        hv_sys_dc_relay_status1 = "Open";
        break;
      case 1:
        hv_sys_dc_relay_status1 = "Closed";
        break;
      case 2:
        hv_sys_dc_relay_status1 = "KeepStatus";
        break;
      case 3:
        hv_sys_dc_relay_status1 = "Fault";
        break;
      default:
        hv_sys_dc_relay_status1 = "Not valid";
    }
    out.kv(TL("HV system relay status 1"), hv_sys_dc_relay_status1);

    String hv_sys_dc_relay_status2;
    switch (datalayer_extended.VolvoPolestar.HVSysDCRlySts2) {
      case 0:
        hv_sys_dc_relay_status2 = "Open";
        break;
      case 1:
        hv_sys_dc_relay_status2 = "Closed";
        break;
      case 2:
        hv_sys_dc_relay_status2 = "KeepStatus";
        break;
      case 3:
        hv_sys_dc_relay_status2 = "Fault";
        break;
      default:
        hv_sys_dc_relay_status2 = "Not valid";
    }
    out.kv(TL("HV system relay status 2"), hv_sys_dc_relay_status2);

    String hv_sys_iso_monitor_status;
    switch (datalayer_extended.VolvoPolestar.HVSysIsoRMonrSts) {
      case 0:
        hv_sys_iso_monitor_status = "Not valid 1";
        break;
      case 1:
        hv_sys_iso_monitor_status = "False";
        break;
      case 2:
        hv_sys_iso_monitor_status = "True";
        break;
      case 3:
        hv_sys_iso_monitor_status = "Not valid 2";
        break;
      default:
        hv_sys_iso_monitor_status = "Not valid";
    }
    out.kv(TL("HV system isolation resistance monitoring status"), hv_sys_iso_monitor_status);

    write_dtc_section(out, *this, datalayer_battery->dtc, DtcCodeStyle::kShortFailureType);
  }

 private:
  DATALAYER_BATTERY_TYPE* datalayer_battery;
  void reset_DTC() { UserRequestDTCreset = true; }
  void read_DTC() { UserRequestDTCreadout = true; }
  void reset_BECM() { UserRequestBECMecuReset = true; }

  std::vector<BatteryCommand> commands_ = {
      command(CMD_RESET_DTC, [this] { reset_DTC(); }),
      command(CMD_READ_DTC, [this] { read_DTC(); }),
      command(CMD_RESET_BECM, [this] { reset_BECM(); }),
  };

  bool UserRequestDTCreset = false;
  bool UserRequestDTCreadout = false;
  bool UserRequestBECMecuReset = false;

  void readCellVoltages();
  // Parses a fully reassembled UDS ReadDTCInformation reply out of dtc_buffer into datalayer_battery->dtc.
  void parseDTCResponseVolvo();
  // DTC readout reassembly state. The reply shares the 0x635 response ID with the periodic
  // group polling, so it is intercepted separately while a readout is in flight.
  static const uint16_t DTC_BUFFER_SIZE = 3 + 4 * DATALAYER_BATTERY_DTC_TYPE::MAX_DTC_COUNT;
  static const unsigned long DTC_TIMEOUT_MS = 2000;
  uint8_t dtc_buffer[DTC_BUFFER_SIZE];
  uint16_t dtc_rx_expected = 0;  // Total payload length announced by the ISO-TP first frame
  uint16_t dtc_rx_len = 0;       // Bytes reassembled so far
  bool dtc_rx_active = false;    // A multi-frame reply is currently being reassembled
  bool dtc_read_in_progress = false;
  bool cell_voltage_read_in_progress = false;
  unsigned long dtc_request_millis = 0;
  bool dtc_clear_in_progress = false;
  unsigned long dtc_clear_millis = 0;

  static const int MAX_PACK_VOLTAGE_108S_DV = 4540;
  static const int MIN_PACK_VOLTAGE_108S_DV = 2938;
  static const int MAX_PACK_VOLTAGE_96S_DV = 4080;
  static const int MIN_PACK_VOLTAGE_96S_DV = 2620;
  static const int MAX_CELL_DEVIATION_MV = 250;
  static const int MAX_CELL_VOLTAGE_MV = 4260;  // Charging is halted if one cell goes above this
  static const int MIN_CELL_VOLTAGE_MV = 2700;  // Charging is halted if one cell goes below this

  static const int PID_POLL_SOH = 0x496D;
  static const int PID_POLL_CELL_VOLTAGES = 0x4B00;
  static const int PID_POLL_BECM_SUPPLY_VOLTAGE = 0xF442;
  static const int PID_POLL_HVIL = 0x491A;

  unsigned long previousMillis100 = 0;  // will store last time a 100ms CAN Message was send
  unsigned long previousMillis500 = 0;  // will store last time a 500ms CAN Message was send
  unsigned long previousMillis1s = 0;   // will store last time a 1s CAN Message was send
  unsigned long previousMillis60s = 0;  // will store last time a 60s CAN Message was send

  int32_t CHARGE_ENERGY = 0;             //0x1A1
  uint16_t BATT_U = 0;                   //0x3A
  uint16_t MAX_U = 0;                    //0x3A
  uint16_t MIN_U = 0;                    //0x3A
  int16_t BATT_I = 0;                    //0x3A
  uint8_t BATT_ERR_INDICATION = 0;       //0x413
  int16_t BATT_T_MAX = 0;                //0x413
  int16_t BATT_T_MIN = 0;                //0x413
  int16_t BATT_T_AVG = 0;                //0x413
  uint16_t SOC_BMS = 0;                  //0X37D
  uint16_t CELL_U_MAX = 370;             //0x37D
  uint16_t CELL_U_MIN = 370;             //0x37D
  uint8_t CELL_ID_U_MAX = 0;             //0x37D
  uint16_t BECMsupplyVoltage = 12000;    //Polled
  uint16_t HvBattPwrLimDchaSoft = 0;     //0x369
  uint16_t HvBattPwrLimDcha1 = 0;        //0x175
  uint16_t HvBattPwrLimDchaSlowAgi = 0;  //0x177
  uint16_t HvBattPwrLimChrgSlowAgi = 0;  //0x177
  uint8_t batteryModuleNumber = 0x10;    // First battery module
  uint8_t battery_request_idx = 0;
  bool rxConsecutiveFrames = false;
  uint16_t min_max_voltage[2];  //contains cell min[0] and max[1] values in mV
  uint8_t cellcounter = 0;
  uint16_t cell_voltages[108];  //array with all the cellvoltages
  bool startedUp = false;
  uint8_t DTC_reset_counter = 0;

  uint16_t incoming_poll = 0;
  uint16_t currentpoll = PID_POLL_SOH;
  uint8_t poll_index = 0;
  const uint16_t poll_commands[3] = {PID_POLL_SOH, PID_POLL_BECM_SUPPLY_VOLTAGE, PID_POLL_HVIL};

  CAN_frame VOLVO_536 = {.FD = false,
                         .ext_ID = false,
                         .DLC = 8,
                         .ID = 0x536,
                         .data = {0x00, 0x40, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00}};  //Network manage frame
  CAN_frame VOLVO_140_CLOSE = {.FD = false,
                               .ext_ID = false,
                               .DLC = 8,
                               .ID = 0x140,
                               .data = {0x00, 0x02, 0x00, 0xB7, 0xFF, 0x03, 0xFF, 0x82}};  //Close contactors message
  CAN_frame VOLVO_140_OPEN = {.FD = false,
                              .ext_ID = false,
                              .DLC = 8,
                              .ID = 0x140,
                              .data = {0x00, 0x02, 0x00, 0x9E, 0xFF, 0x03, 0xFF, 0x82}};  //Open contactor message
  CAN_frame VOLVO_372 = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x372,
      .data = {0x00, 0xA6, 0x07, 0x14, 0x04, 0x00, 0x80, 0x00}};  //Ambient Temp -->>VERIFY this data content!!!<<--
  CAN_frame VOLVO_CELL_U_Req = {.FD = false,
                                .ext_ID = false,
                                .DLC = 8,
                                .ID = 0x735,
                                .data = {0x03, 0x22, 0x4B, 0x00, 0x00, 0x00, 0x00, 0x00}};  //Cell voltage request frame
  CAN_frame VOLVO_FlowControl = {.FD = false,
                                 .ext_ID = false,
                                 .DLC = 8,
                                 .ID = 0x735,
                                 .data = {0x30, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00}};  //Flowcontrol
  CAN_frame VOLVO_Poll_frame = {.FD = false,
                                .ext_ID = false,
                                .DLC = 8,
                                .ID = 0x735,
                                .data = {0x03, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  CAN_frame VOLVO_BECM_ECUreset = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x735,
      .data = {0x02, 0x11, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00}};  //BECM ECU reset command (reboot/powercycle BECM)
  CAN_frame VOLVO_DTC_Erase = {.FD = false,
                               .ext_ID = false,
                               .DLC = 8,
                               .ID = 0x7FF,
                               .data = {0x04, 0x14, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00}};  //Global DTC erase
  CAN_frame VOLVO_DTCreadout = {.FD = false,
                                .ext_ID = false,
                                .DLC = 8,
                                .ID = 0x7FF,
                                .data = {0x02, 0x19, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}};  //Global DTC readout
};

#endif
