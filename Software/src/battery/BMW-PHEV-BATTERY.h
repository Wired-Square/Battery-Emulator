#ifndef BMW_PHEV_BATTERY_H
#define BMW_PHEV_BATTERY_H
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "../devboard/webserver/advanced_api.h"
#include "BatterySlotContext.h"
#include "CanBattery.h"

class BmwPhevBattery : public CanBattery {
 public:
  bool mandatory_charge_taper() { return true; }  //TODO: Investigate if actually needed
  BmwPhevBattery(const BatterySlotContext& ctx)
      : CanBattery(ctx.can_interface, CAN_Speed::CAN_SPEED_500KBPS, CanSpeedMode::Variable) {
    datalayer_battery = ctx.datalayer;
  }
  virtual void setup(void);
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);

  const std::vector<BatteryCommand>& get_commands() override { return commands_; }

  bool supports_insulation_resistance() override { return true; }

  const char* get_dtc_json_filename() override { return "bmw_phev_dtc.json"; }

  BatteryAdvancedStatus get_advanced_status() override {
    BatteryAdvancedStatus status;
    auto& phev = datalayer_extended.bmwphev;

    // Shared across most 0/1/2/3 status fields: "not evaluated / OK / error active / invalid signal".
    auto status_ok_error = [](uint8_t v) -> String {
      switch (v) {
        case 0:
          return "Not Evaluated";
        case 1:
          return "OK";
        case 2:
          return "Error";
        case 3:
          return "Invalid Signal";
        default:
          return "Unknown";
      }
    };
    // Shared across the three "request open contactors" fields.
    auto status_active = [](uint8_t v) -> String {
      switch (v) {
        case 0:
          return "Not Evaluated";
        case 1:
          return "Not Active";
        case 2:
          return "Active";
        case 3:
          return "Invalid Signal";
        default:
          return "Unknown";
      }
    };

    AdvancedSection power;
    power.title = "Power & Voltage";
    power.fields.push_back(kv("Battery Voltage (After Contactor)", String(phev.battery_voltage_after_contactor), "dV"));
    power.fields.push_back(kv("Max Design Voltage", String(datalayer_battery->info.max_design_voltage_dV), "dV"));
    power.fields.push_back(kv("Min Design Voltage", String(datalayer_battery->info.min_design_voltage_dV), "dV"));
    power.fields.push_back(kv("Allowed Charge Power", String(datalayer_battery->status.max_charge_power_W), "W"));
    power.fields.push_back(kv("Allowed Discharge Power", String(datalayer_battery->status.max_discharge_power_W), "W"));
    power.fields.push_back(kv("BMS Allowed Charge Amps", String(phev.allowable_charge_amps), "A"));
    power.fields.push_back(kv("BMS Allowed Discharge Amps", String(phev.allowable_discharge_amps), "A"));
    status.sections.push_back(power);

    AdvancedSection contactors;
    contactors.title = "Contactor Status";

    String dcsw;
    switch (phev.ST_DCSW) {
      case 0:
        dcsw = "Contactors Open";
        break;
      case 1:
        dcsw = "Precharge Ongoing";
        break;
      case 2:
        dcsw = "Contactors Engaged";
        break;
      case 3:
        dcsw = "Invalid Signal";
        break;
      default:
        dcsw = "Unknown";
    }
    contactors.fields.push_back(kv("Contactor Status", dcsw));

    String precharge;
    switch (phev.ST_precharge) {
      case 0:
        precharge = "Not Evaluated";
        break;
      case 1:
        precharge = "Not Active, Closing Not Blocked";
        break;
      case 2:
        precharge = "Error - Precharge Blocked";
        break;
      case 3:
        precharge = "Invalid Signal";
        break;
      default:
        precharge = "Unknown";
    }
    contactors.fields.push_back(kv("Precharge Status", precharge));

    String weld;
    switch (phev.ST_WELD) {
      case 0:
        weld = "Contactors OK";
        break;
      case 1:
        weld = "One Contactor Welded!";
        break;
      case 2:
        weld = "Two Contactors Welded!";
        break;
      case 3:
        weld = "Invalid Signal";
        break;
      default:
        weld = "Unknown";
    }
    contactors.fields.push_back(kv("Contactor Weld Status", weld));

    contactors.fields.push_back(kv("Request Open Contactors", status_active(phev.battery_request_open_contactors)));
    contactors.fields.push_back(
        kv("Request Open Contactors (Fast)", status_active(phev.battery_request_open_contactors_fast)));
    contactors.fields.push_back(
        kv("Request Open Contactors (Instantly)", status_active(phev.battery_request_open_contactors_instantly)));
    status.sections.push_back(contactors);

    AdvancedSection safety;
    safety.title = "Safety Systems";
    safety.fields.push_back(kv("Interlock", status_ok_error(phev.ST_interlock)));
    safety.fields.push_back(kv("Emergency Status", status_ok_error(phev.ST_EMG)));
    status.sections.push_back(safety);

    AdvancedSection isolation;
    isolation.title = "Isolation Monitoring";
    isolation.fields.push_back(kv("Overall Isolation Status", status_ok_error(phev.ST_isolation)));
    isolation.fields.push_back(kv("Internal Isolation", status_ok_error(phev.ST_iso_int)));
    isolation.fields.push_back(kv("External Isolation", status_ok_error(phev.ST_iso_ext)));
    isolation.fields.push_back(kv("Isolation Resistance", String(phev.iso_safety_kohm), "kΩ"));
    isolation.fields.push_back(kv("Isolation Quality", String(phev.iso_safety_kohm_quality)));
    isolation.fields.push_back(kv("Internal Resistance",
                                  String(phev.iso_safety_int_kohm) + " kΩ " +
                                      (phev.iso_safety_int_plausible ? "(Plausible)" : "(Not Plausible)")));
    isolation.fields.push_back(kv("External Resistance",
                                  String(phev.iso_safety_ext_kohm) + " kΩ " +
                                      (phev.iso_safety_ext_plausible ? "(Plausible)" : "(Not Plausible)")));
    isolation.fields.push_back(kv("Trigger Resistance",
                                  String(phev.iso_safety_trg_kohm) + " kΩ " +
                                      (phev.iso_safety_trg_plausible ? "(Plausible)" : "(Not Plausible)")));
    status.sections.push_back(isolation);

    AdvancedSection thermal;
    thermal.title = "Thermal Management";
    thermal.fields.push_back(kv("Cooling Valve Status", status_ok_error(phev.ST_valve_cooling)));

    String cold_shutoff;
    switch (phev.ST_cold_shutoff_valve) {
      case 0:
        cold_shutoff = "OK";
        break;
      case 1:
        cold_shutoff = "Short Circuit to GND";
        break;
      case 2:
        cold_shutoff = "Short Circuit to 12V";
        break;
      case 3:
        cold_shutoff = "Line Break";
        break;
      case 6:
        cold_shutoff = "Driver Error";
        break;
      case 12:
      case 13:
        cold_shutoff = "Stuck";
        break;
      default:
        cold_shutoff = "Invalid Signal";
    }
    thermal.fields.push_back(kv("Cold Shutoff Valve", cold_shutoff));
    status.sections.push_back(thermal);

    AdvancedSection cells;
    cells.title = "Cell Information";
    cells.fields.push_back(kv("Detected Cell Count", String(datalayer_battery->info.number_of_cells)));
    cells.fields.push_back(kv("Max Cell Design Voltage", String(datalayer_battery->info.max_cell_voltage_mV), "mV"));
    cells.fields.push_back(kv("Min Cell Design Voltage", String(datalayer_battery->info.min_cell_voltage_mV), "mV"));
    cells.fields.push_back(kv("Min Cell Voltage Data Age", String(phev.min_cell_voltage_data_age), "ms"));
    cells.fields.push_back(kv("Max Cell Voltage Data Age", String(phev.max_cell_voltage_data_age), "ms"));
    status.sections.push_back(cells);

    AdvancedSection balancing;
    balancing.title = "Balancing Status";
    balancing.fields.push_back(
        kv("Note", "Balancing can only run while the contactors are OPEN and after the cells have settled at "
                   "rest for ~10 min (see \"Inactive - Cells Not at Rest (Wait 10 min)\" below). It is blocked "
                   "while the contactors are closed."));

    String balancing_status;
    switch (phev.balancing_status) {
      case 0:
        balancing_status = "Inactive - Not Needed";
        break;
      case 1:
        balancing_status = "Active";
        break;
      case 2:
        balancing_status = "Inactive - Cells Not at Rest (Wait 10 min)";
        break;
      case 3:
        balancing_status = "Inactive";
        break;
      default:
        balancing_status = "Unknown";
    }
    balancing.fields.push_back(kv("Balancing", balancing_status));
    balancing.fields.push_back(
        kv("Balancing Request", datalayer_battery->settings.user_requests_balancing ? "True" : "False"));
    status.sections.push_back(balancing);

    AdvancedSection diagnostics;
    diagnostics.title = "Diagnostics";
    diagnostics.fields.push_back(kv("Charging Condition Delta", String(phev.battery_charging_condition_delta)));
    status.sections.push_back(diagnostics);

    // DTC data is split across two extended structs: bmwphev owns dtc_count/dtc_read_failed,
    // while the codes/status/last-read timestamp are stored in the shared bmwix struct to save
    // space (see the field comments in datalayer_extended.h). Assemble a local
    // DATALAYER_BATTERY_DTC_TYPE so the shared dtc_advanced_section(*this, ) builder can be reused.
    DATALAYER_BATTERY_DTC_TYPE dtc{};
    dtc.dtc_count = phev.dtc_count;
    dtc.dtc_read_failed = phev.dtc_read_failed;
    dtc.dtc_last_read_millis = datalayer_extended.bmwix.dtc_last_read_millis;
    for (uint8_t i = 0; i < dtc.dtc_count && i < DATALAYER_BATTERY_DTC_TYPE::MAX_DTC_COUNT; i++) {
      dtc.dtc_codes[i] = datalayer_extended.bmwix.dtc_codes[i];
      dtc.dtc_status[i] = datalayer_extended.bmwix.dtc_status[i];
    }
    status.sections.push_back(dtc_advanced_section(*this, dtc, DtcCodeStyle::kRawHex));

    return status;
  }

 private:
  DATALAYER_BATTERY_TYPE* datalayer_battery;
  void reset_DTC() { datalayer_extended.bmwphev.UserRequestDTCreset = true; }
  void reset_BMS() { datalayer_extended.bmwphev.UserRequestBMSReset = true; }
  void request_open_contactors() { userRequestContactorOpen = true; }
  void request_close_contactors() { userRequestContactorClose = true; }
  void initiate_balancing() { datalayer_battery->settings.user_requests_balancing = true; }
  void end_balancing() { datalayer_battery->settings.user_requests_balancing = false; }
  void request_isolation_test() { datalayer_extended.bmwphev.UserRequestIsolationTest = true; }

  std::vector<BatteryCommand> commands_ = {
      command(CMD_RESET_BMS, [this] { reset_BMS(); }),
      command(CMD_RESET_DTC, [this] { reset_DTC(); }),
      // Beta CAN-based contactor close support via 0x53A (see INFO section in .cpp)
      command(CMD_CONTACTOR_CLOSE, [this] { request_close_contactors(); }),
      command(CMD_CONTACTOR_OPEN, [this] { request_open_contactors(); }),
      // Online balancing via the SME UDS routine (0xAD6B). The transmit loop reads
      // user_requests_balancing: set sends startRoutine, clear sends stopRoutine, which cancels any
      // latched balancing. Neither carries an availability predicate because the SME latches
      // independently of the flag, so Stop must stay offered even when the flag reads clear.
      //
      // IMPORTANT (SME behaviour):
      //  - Balancing is ONLY possible while the contactors are OPEN. The SME will not balance with the
      //    pack connected/closed.
      //  - While balancing is ACTIVE the SME BLOCKS contactor close (precharge is inhibited until
      //    balancing finishes / is stopped). So requesting balancing prevents the pack from going online.
      command(CMD_START_BALANCING_REQUEST, [this] { initiate_balancing(); }),
      command(CMD_STOP_BALANCING_REQUEST, [this] { end_balancing(); }),
      // Isolation test - one-shot UDS startRoutine (0xAD61). Same one-shot pattern as DTC/BMS reset.
      command(CMD_ISOLATION_TEST, [this] { request_isolation_test(); }),
  };

  static const int MAX_PACK_VOLTAGE_DV = 4650;  //4650 = 465.0V
  static const int MIN_PACK_VOLTAGE_DV = 3000;
  static const int MAX_CELL_DEVIATION_MV = 250;
  static const int MAX_CELL_VOLTAGE_MV = 4300;  //Battery is put into emergency stop if one cell goes over this value
  static const int MIN_CELL_VOLTAGE_MV = 2800;  //Battery is put into emergency stop if one cell goes below this value
  static const int MAX_DTC_COUNT = 20;          // Maximum number of DTCs to store/display
  static const int STALE_PERIOD_CONFIG =
      3600000;  //Number of milliseconds before critical values are classed as stale/stuck 1800000 = 3600 seconds / 60mins

  bool isStale(int16_t currentValue, uint16_t& lastValue, unsigned long& lastChangeTime);
  void startUDSMultiFrameReception(uint16_t totalLength, uint8_t moduleID);
  bool storeUDSPayload(const uint8_t* payload, uint8_t length);
  bool isUDSMessageComplete();
  void parseDTCResponse();
  void processCellVoltages();
  void wake_battery_via_canbus();
  uint8_t increment_alive_counter(uint8_t counter);
  const char* getUDSRequestName(CAN_frame* frame);

  // Beta CAN-based contactor close (0x53A) helpers
  void HandleIncomingUserRequest(void);
  void HandleIncomingInverterRequest(void);
  void HandleEquipmentStopRequest(void);
  void PhevCloseContactors(void);
  void PhevOpenContactors(void);

  unsigned long previousMillis20 = 0;     // will store last time a 20ms CAN Message was send
  unsigned long previousMillis100 = 0;    // will store last time a 100ms CAN Message was send
  unsigned long previousMillis200 = 0;    // will store last time a 200ms CAN Message was send
  unsigned long previousMillis500 = 0;    // will store last time a 500ms CAN Message was send
  unsigned long previousMillis640 = 0;    // will store last time a 600ms CAN Message was send
  unsigned long previousMillis1000 = 0;   // will store last time a 1000ms CAN Message was send
  unsigned long previousMillis5000 = 0;   // will store last time a 5000ms CAN Message was send
  unsigned long previousMillis10000 = 0;  // will store last time a 10000ms CAN Message was send
  unsigned long min_cell_voltage_lastchanged = 0;
  unsigned long max_cell_voltage_lastchanged = 0;

  static const unsigned long WAKEUP_DIP_DURATION_MS = 50;
  unsigned long wakeup_dip_started_ms = 0;
  bool wakeup_dip_active = false;

  void restore_can_speed_after_wakeup();

  static const int ALIVE_MAX_VALUE = 14;  // BMW CAN messages contain alive counter, goes from 0...14

  enum CmdState { SOH, CELL_VOLTAGE_MINMAX, SOC, CELL_VOLTAGE_CELLNO, CELL_VOLTAGE_CELLNO_LAST };

  CmdState cmdState = SOC;

  // A structure to keep track of the ongoing multi-frame UDS response
  typedef struct {
    bool UDS_inProgress;                // Are we currently receiving a multi-frame message?
    uint16_t UDS_expectedLength;        // Expected total payload length
    uint16_t UDS_bytesReceived;         // How many bytes have been stored so far
    uint8_t UDS_moduleID;               // The "module" indicated by the first frame
    uint8_t receivedInBatch;            // Number of CFs received in the current batch
    uint8_t UDS_buffer[256];            // Buffer for the reassembled data
    unsigned long UDS_lastFrameMillis;  // Timestamp of last frame (for timeouts, if desired)
  } UDS_RxContext;

  // A single global UDS context, since only one module can respond at a time
  UDS_RxContext gUDSContext;

  // Max time to wait for the next frame of a multi-frame UDS response before abandoning it,
  // so a lost frame can't wedge polling permanently (see UDS poll guards in transmit_can).
  static const unsigned long UDS_REASSEMBLY_TIMEOUT_MS = 500;
  // How long to silence the timed UDS polls after a one-shot command (Clear DTC, BMS Reset)
  // so the response has time to arrive before the next request is sent.
  static const unsigned long UDS_ONE_SHOT_SILENCE_MS = 1000;
  unsigned long uds_one_shot_sent_ms = 0;  // millis() when last one-shot command was sent

  //Vehicle CAN START
  CAN_frame BMW_12F = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x12F,
      .data = {0x00, 0x20, 0x86, 0x1B, 0xF1, 0x35, 0x30, 0x02}};  // CRC, counter starts at 0x20, static data
  CAN_frame BMW_10B = {.FD = false,
                       .ext_ID = false,
                       .DLC = 3,
                       .ID = 0x10B,
                       .data = {0xCD, 0x00, 0xFC}};  // Contactor closing command
  // Beta CAN-based contactor control - streamed at 200ms. data[5]/data[6] select the state:
  //   STATE A idle 0x00/0x01, STATE B closing 0x80/0x01, STATE D steady 0x84/0x00
  // See CONTACTOR CLOSE notes in the INFO section of the .cpp
  CAN_frame BMW_53A = {.FD = false,
                       .ext_ID = false,
                       .DLC = 8,
                       .ID = 0x53A,
                       .data = {0x40, 0x3A, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00}};  // STATE A - idle / contactors off

  // --- Vehicle environment / status frames the SME expects (silences CAD4xx "No message" DTCs).
  //     Sample payloads captured on the bus; see VEHICLE TX MAP in the INFO section of the .cpp.
  //     STATIC frames (0x3A0, 0x3CA, 0x3E8) and 0x2CA (simple byte1 LSB toggle) are transmitted.
  //     The frames marked NEEDS COUNTER below carry a rolling counter on the real bus and are NOT
  //     transmitted yet - sending them frozen risks a "signal invalid" DTC. Kept here for reference.
  CAN_frame BMW_1A1 = {.FD = false,
                       .ext_ID = false,
                       .DLC = 5,
                       .ID = 0x1A1,
                       .data = {0x00, 0xC0, 0x00, 0x00,
                                0x8A}};  // Vehicle speed (DSC), standstill. NEEDS COUNTER: byte1 lo-nibble rolls 0..E
  CAN_frame BMW_3A0 = {.FD = false,
                       .ext_ID = false,
                       .DLC = 8,
                       .ID = 0x3A0,
                       .data = {0xFF, 0xFF, 0xCF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD}};  // Vehicle condition (BDC) - static
  CAN_frame BMW_328 = {
      .FD = false,
      .ext_ID = false,
      .DLC = 6,
      .ID = 0x328,
      .data = {0x27, 0x58, 0xDC, 0x0D, 0x7A,
               0x25}};  // Relative time / clock (KOMBI). Sent 1000ms w/ live counter: bytes0-3=seconds, 4-5=days
  CAN_frame BMW_3CA = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x3CA,
      .data = {0xB7, 0x60, 0x01, 0x0F, 0x0F, 0x31, 0xFF, 0xFF}};  // Driving-info forecast (KOMBI) - static
  CAN_frame BMW_433 = {
      .FD = false,
      .ext_ID = false,
      .DLC = 4,
      .ID = 0x433,
      .data = {0xFF, 0x0C, 0x0C, 0xF1}};  // HV-battery specification (EME). NEEDS COUNTER: byte1 toggles 0x0C<->0x0D
  CAN_frame BMW_2CA = {.FD = false,
                       .ext_ID = false,
                       .DLC = 2,
                       .ID = 0x2CA,
                       .data = {0x6E, 0x6F}};  // Ambient temperature (KOMBI). SENT static: byte0=(T+40)*2 (0x6E=15C)
  CAN_frame BMW_3E8 = {.FD = false,
                       .ext_ID = false,
                       .DLC = 2,
                       .ID = 0x3E8,
                       .data = {0xF1, 0xFF}};  // OBD diagnosis, engine control (EME) - static, diag idle
  //Vehicle CAN END

  //Request Data CAN START

  CAN_frame BMW_PHEV_BUS_WAKEUP_REQUEST = {
      .FD = false,
      .ext_ID = false,
      .DLC = 4,
      .ID = 0x554,
      .data = {
          0x5A, 0xA5, 0x5A,
          0xA5}};  // Won't work at 500kbps! Ideally sent at 50kbps - but can also achieve wakeup at 100kbps (helps with library support but might not be as reliable). Might need to be sent twice + clear buffer

  CAN_frame BMWPHEV_6F1_REQUEST_SOC = {.FD = false,
                                       .ext_ID = false,
                                       .DLC = 5,
                                       .ID = 0x6F1,
                                       .data = {0x07, 0x03, 0x22, 0xDD, 0xC4}};  //  SOC%

  CAN_frame BMWPHEV_6F1_REQUEST_SOH = {.FD = false,
                                       .ext_ID = false,
                                       .DLC = 5,
                                       .ID = 0x6F1,
                                       .data = {0x07, 0x03, 0x22, 0xDD, 0x7B}};  //  SOH%

  CAN_frame BMWPHEV_6F1_REQUEST_CURRENT = {.FD = false,
                                           .ext_ID = false,
                                           .DLC = 5,
                                           .ID = 0x6F1,
                                           .data = {0x07, 0x03, 0x22, 0xDD, 0x69}};  //  SOH%

  CAN_frame BMWPHEV_6F1_REQUEST_VOLTAGE_LIMITS = {
      .FD = false,
      .ext_ID = false,
      .DLC = 5,
      .ID = 0x6F1,
      .data = {0x07, 0x03, 0x22, 0xDD, 0x7E}};  //  Pack Voltage Limits  Multi Frame

  // UDS $19 ReadDTC Request (Report DTC by status mask - all DTCs)
  CAN_frame BMWPHEV_6F1_REQUEST_READ_DTC = {.FD = false,
                                            .ext_ID = false,
                                            .DLC = 8,
                                            .ID = 0x6F1,
                                            .data = {0x07, 0x03, 0x19, 0x02, 0x0C, 0x00, 0x00, 0x00}};

  // UDS $14 ClearDTC Request (Clear all DTCs)
  CAN_frame BMWPHEV_6F1_REQUEST_CLEAR_DTC = {.FD = false,
                                             .ext_ID = false,
                                             .DLC = 8,
                                             .ID = 0x6F1,
                                             .data = {0x07, 0x04, 0x14, 0xFF, 0xFF, 0xFF, 0x00, 0x00}};

  CAN_frame BMWPHEV_6F1_REQUEST_PAIRED_VIN = {.FD = false,
                                              .ext_ID = false,
                                              .DLC = 5,
                                              .ID = 0x6F1,
                                              .data = {0x07, 0x03, 0x22, 0xF1, 0x90}};  //  SME Paired VIN

  CAN_frame BMWPHEV_6F1_REQUEST_PACK_INFO = {
      .FD = false,
      .ext_ID = false,
      .DLC = 5,
      .ID = 0x6F1,
      .data = {0x07, 0x03, 0x22, 0xDF,
               0x71}};  //   62 DF 71 00 60 1C 25 1C? Cell Count,  (byte 4 0x60 = 96)  - Module count assumed here too

  CAN_frame BMWPHEV_6F1_REQUEST_CURRENT_LIMITS = {
      .FD = false,
      .ext_ID = false,
      .DLC = 5,
      .ID = 0x6F1,
      .data = {0x07, 0x03, 0x22, 0xDD, 0x7D}};  //  Pack Current Limits  Multi Frame

  CAN_frame BMWPHEV_6F1_REQUEST_MAINVOLTAGE_PRECONTACTOR = {
      .FD = false,
      .ext_ID = false,
      .DLC = 5,
      .ID = 0x6F1,
      .data = {0x07, 0x03, 0x22, 0xDD, 0xB4}};  //Main Battery Voltage (Pre Contactor)

  CAN_frame BMWPHEV_6F1_REQUEST_MAINVOLTAGE_POSTCONTACTOR = {
      .FD = false,
      .ext_ID = false,
      .DLC = 5,
      .ID = 0x6F1,
      .data = {0x07, 0x03, 0x22, 0xDD, 0x66}};  //Main Battery Voltage (After Contactor)

  CAN_frame BMWPHEV_6F1_REQUEST_CELLSUMMARY = {
      .FD = false,
      .ext_ID = false,
      .DLC = 5,
      .ID = 0x6F1,
      .data = {0x07, 0x03, 0x22, 0xDF, 0xA0}};  //Min and max cell voltage + temps   6.55V = Qualifier Invalid?

  CAN_frame BMWPHEV_6F1_REQUEST_CELLS_INDIVIDUAL_VOLTS = {
      .FD = false,
      .ext_ID = false,
      .DLC = 5,
      .ID = 0x6F1,
      .data = {0x07, 0x03, 0x22, 0xDF, 0xA5}};  //All individual cell voltages

  CAN_frame BMWPHEV_6F1_REQUEST_CELL_TEMP = {
      .FD = false,
      .ext_ID = false,
      .DLC = 5,
      .ID = 0x6F1,
      .data = {
          0x07, 0x03, 0x22, 0xDD,
          0xC0}};  // UDS Request Cell Temperatures min max avg. Has continue frame min in first, then max + avg in second frame

  CAN_frame BMW_6F1_REQUEST_CONTINUE_MULTIFRAME = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x6F1,
      .data = {
          0x07, 0x30, 0x03, 0x00, 0x00, 0x00, 0x00,
          0x00}};  //Request continued frames from UDS Multiframe request  byte[2] is the request messages to return per continue. default 0x03, all is 0x00

  CAN_frame BMW_6F1_REQUEST_HARD_RESET = {.FD = false,
                                          .ext_ID = false,
                                          .DLC = 4,
                                          .ID = 0x6F1,
                                          .data = {0x07, 0x02, 0x11, 0x01}};  // UDS ECU hard reset (0x11 0x01)

  CAN_frame BMWPHEV_6F1_REQUEST_CONTACTORS_CLOSE = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x6F1,
      .data = {0x07, 0x04, 0x2E, 0xDD, 0x61, 0x01, 0x00, 0x00}};  // Request Contactors Close - Unconfirmed
  CAN_frame BMWPHEV_6F1_REQUEST_CONTACTORS_OPEN = {
      .FD = false,
      .ext_ID = false,
      .DLC = 6,
      .ID = 0x6F1,
      .data = {0x07, 0x04, 0x2E, 0xDD, 0x61, 0x00, 0x00, 0x00}};  // Request Contactors Open - Unconfirmed

  CAN_frame BMWPHEV_6F1_REQUEST_BALANCING_STATUS = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x6F1,
      .data = {0x07, 0x04, 0x31, 0x03, 0xAD, 0x6B, 0x00,
               0x00}};  // Balancing status.  Response 7DLC F1 05 71 03 AD 6B 01   (01 = active)  (03 not active)

  CAN_frame BMWPHEV_6F1_REQUEST_ISOLATION_TEST = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x6F1,
      .data = {0x07, 0x04, 0x31, 0x01, 0xAD, 0x61, 0x00, 0x00}};  // Start Isolation Test

  // --- Isolation test: requestRoutineResults (UDS $31 sub 0x03) on RID 0xAD61.
  //     Send AFTER starting the routine (0x31 0x01 0xAD 0x61). Poll this until the SME
  //     reports the measurement finished. Response layout (positive, 0x71):
  //       71 03 AD 61 [B5 STAT_MESSUNG_ERFOLGREICH] [B6 STAT_MESSUNG_ISOLATIONSFEHLER]
  //       B5: 0x00 = Isolationsmessung NICHT erfolgreich (failed / no valid result)
  //           0x01 = Isolationsmessung erfolgreich (measurement valid - read iso kohm values)
  //           0x02 = Isolationsmessung laeuft (IN PROGRESS - keep polling, result not ready)
  //       B6: 0x00 = kein Fehler (no isolation fault)
  //           0xFF = nicht definiert (undefined - typical while result not yet valid)
  //     Observed:
  //       71 03 AD 61 00 FF  -> measurement not successful, fault undefined
  //       71 03 AD 61 02 00  -> measurement in progress (running), no fault yet
  //       71 03 AD 61 01 00  -> measurement successful, no isolation fault
  CAN_frame BMWPHEV_6F1_REQUEST_ISOLATION_RESULT = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x6F1,
      .data = {0x07, 0x04, 0x31, 0x03, 0xAD, 0x61, 0x00, 0x00}};  // requestRoutineResults - Isolation Test

  CAN_frame BMWPHEV_6F1_REQUEST_ISO_READING1 = {
      .FD = false,
      .ext_ID = false,
      .DLC = 5,
      .ID = 0x6F1,
      .data = {
          0x07, 0x03, 0x22, 0xDD,
          0x6A}};  // MULTI FRAME ISOLATIONSWIDERSTAND 62 DD 6A [07 D0] [07 D0] [07 D0] [01] [01] [01] 00 00 00 00 00   [EXT Reading] [INT reading] [ EXT - 0 not plausible, 1 plausible]

  CAN_frame BMWPHEV_6F1_REQUEST_ISO_READING2 = {
      .FD = false,
      .ext_ID = false,
      .DLC = 5,
      .ID = 0x6F1,
      .data = {0x07, 0x03, 0x22, 0xD6,
               0xD9}};  //  R_ISO_ROH 62 D6 D9 [07 FF] [13] (2047kohm) quality of reading 0-21 (19)

  CAN_frame BMWPHEV_6F1_REQUEST_BALANCING_START = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x6F1,
      .data = {0x07, 0x04, 0x31, 0x01, 0xAD, 0x6B, 0x00, 0x00}};  // Balancing start request

  CAN_frame BMWPHEV_6F1_REQUEST_BALANCING_STOP = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x6F1,
      .data = {0x07, 0x04, 0x31, 0x02, 0xAD, 0x6B, 0x00, 0x00}};  // Balancing stop request
                                                                  /*
  CAN_frame BMWPHEV_6F1_CELL_SOC = {.FD = false,
                                    .ext_ID = false,
                                    .DLC = 5,
                                    .ID = 0x6F1,
                                    .data = {0x07, 0x03, 0x22, 0xE5, 0x9A}};
  CAN_frame BMWPHEV_6F1_CELL_TEMP = {.FD = false,
                                     .ext_ID = false,
                                     .DLC = 5,
                                     .ID = 0x6F1,
                                     .data = {0x07, 0x03, 0x22, 0xE5, 0xCA}};
                                     */
  //Request Data CAN End

  //Setup Fast UDS values to poll for
  CAN_frame* UDS_REQUESTS_FAST[4] = {&BMWPHEV_6F1_REQUEST_CELLSUMMARY, &BMWPHEV_6F1_REQUEST_SOC,
                                     &BMWPHEV_6F1_REQUEST_VOLTAGE_LIMITS,
                                     &BMWPHEV_6F1_REQUEST_MAINVOLTAGE_POSTCONTACTOR};
  int numFastUDSreqs =
      sizeof(UDS_REQUESTS_FAST) / sizeof(UDS_REQUESTS_FAST[0]);  //Store Number of elements in the array

  //Setup Slow UDS values to poll for
  CAN_frame* UDS_REQUESTS_SLOW[9] = {&BMWPHEV_6F1_REQUEST_ISO_READING1,
                                     &BMWPHEV_6F1_REQUEST_ISO_READING2,
                                     &BMWPHEV_6F1_REQUEST_CURRENT_LIMITS,
                                     &BMWPHEV_6F1_REQUEST_SOH,
                                     &BMWPHEV_6F1_REQUEST_CELLS_INDIVIDUAL_VOLTS,
                                     &BMWPHEV_6F1_REQUEST_CELL_TEMP,
                                     &BMWPHEV_6F1_REQUEST_BALANCING_STATUS,
                                     &BMWPHEV_6F1_REQUEST_PAIRED_VIN,
                                     &BMWPHEV_6F1_REQUEST_READ_DTC};
  int numSlowUDSreqs =
      sizeof(UDS_REQUESTS_SLOW) / sizeof(UDS_REQUESTS_SLOW[0]);  // Store Number of elements in the array

  //PHEV intermediate vars
  //#define UDS_LOG //Useful for logging multiframe handling

  uint32_t battery_BEV_available_power_shortterm_charge = 0;
  uint32_t battery_BEV_available_power_shortterm_discharge = 0;
  uint32_t battery_BEV_available_power_longterm_charge = 0;
  uint32_t battery_BEV_available_power_longterm_discharge = 0;

  int32_t battery_current = 0;

  uint16_t battery_max_charge_voltage = 0;
  uint16_t battery_min_discharge_voltage = 0;
  uint16_t battery_predicted_energy_charge_condition = 0;
  uint16_t battery_predicted_energy_charging_target = 0;
  uint16_t battery_prediction_voltage_shortterm_charge = 0;
  uint16_t battery_prediction_voltage_shortterm_discharge = 0;
  uint16_t battery_prediction_voltage_longterm_charge = 0;
  uint16_t battery_prediction_voltage_longterm_discharge = 0;
  uint16_t battery_prediction_duration_charging_minutes = 0;
  uint16_t battery_energy_content_maximum_kWh = 0;
  uint16_t battery_target_voltage_in_CV_mode = 0;
  uint16_t battery_display_SOC = 0;
  uint16_t battery_voltage = 3700;  //Initialize as valid - should be fixed in future
  uint16_t battery_voltage_after_contactor = 0;
  uint16_t avg_soc_state = 5000;
  uint16_t min_soh_state = 9999;  // Uses E5 45, also available in 78 73
  uint16_t max_design_voltage = MAX_PACK_VOLTAGE_DV;
  uint16_t min_design_voltage = MIN_PACK_VOLTAGE_DV;
  uint16_t iso_safety_int_kohm = 0;  //STAT_ISOWIDERSTAND_INT_WERT
  uint16_t iso_safety_ext_kohm = 0;  //STAT_ISOWIDERSTAND_EXT_STD_WERT
  uint16_t iso_safety_trg_kohm = 0;
  uint16_t iso_safety_kohm = 0;  //STAT_R_ISO_ROH_01_WERT

  int16_t battery_max_discharge_amperage = 0;
  int16_t battery_max_charge_amperage = 0;
  int16_t battery_temperature_max = 0;
  int16_t battery_temperature_min = 0;
  int16_t min_cell_voltage = 3700;       //Initialize as valid - should be fixed in future
  int16_t max_cell_voltage = 3700;       //Initialize as valid - should be fixed in future
  int16_t allowable_charge_amps = 0;     //E5 62
  int16_t allowable_discharge_amps = 0;  //E5 62

  uint8_t battery_status_service_disconnection_plug = 0;
  uint8_t battery_status_measurement_isolation = 0;
  uint8_t battery_request_abort_charging = 0;
  uint8_t battery_prediction_time_end_of_charging_minutes = 0;
  uint8_t battery_request_charging_condition_minimum = 0;
  uint8_t battery_request_charging_condition_maximum = 0;
  uint8_t battery_request_operating_mode = 0;
  uint8_t battery_status_error_isolation_external_Bordnetz = 0;
  uint8_t battery_status_error_isolation_internal_Bordnetz = 0;
  uint8_t battery_request_cooling = 0;
  uint8_t battery_status_valve_cooling = 0;
  uint8_t battery_status_error_locking = 0;
  uint8_t battery_status_precharge_locked = 0;
  uint8_t battery_status_disconnecting_switch = 0;
  uint8_t battery_status_emergency_mode = 0;
  uint8_t battery_request_service = 0;
  uint8_t battery_error_emergency_mode = 0;
  uint8_t battery_status_error_disconnecting_switch = 0;
  uint8_t battery_status_warning_isolation = 0;
  uint8_t battery_status_cold_shutoff_valve = 0;
  uint8_t battery_request_open_contactors = 0;
  uint8_t battery_request_open_contactors_instantly = 0;
  uint8_t battery_request_open_contactors_fast = 0;
  uint8_t battery_charging_condition_delta = 0;
  uint8_t startup_counter_contactor = 0;
  uint8_t alive_counter_20ms = 0;
  uint8_t iso_safety_ext_plausible = 0;  //STAT_ISOWIDERSTAND_EXT_TRG_PLAUS
  uint8_t iso_safety_int_plausible = 0;  //STAT_ISOWIDERSTAND_EXT_TRG_WERT
  uint8_t iso_safety_trg_plausible = 0;
  uint8_t iso_safety_kohm_quality = 0;    //STAT_R_ISO_ROH_QAL_01_INFO Quality of measurement 0-21 (higher better)
  uint8_t balancing_status = 0;           //4 = not active
  bool phev_balancing_stop_sent = false;  // One-shot: send UDS stopRoutine(0xAD6B) at boot to clear
                                          // any balancing routine left latched in the SME from a prior run.
  uint8_t uds_fast_req_id_counter = 0;
  uint8_t uds_slow_req_id_counter = 0;
  uint8_t alive_counter_100ms = 0;
  // 0x328 relative-time clock counters (ported from i3). Seconds since system start (T_SEC_COU_REL,
  // bytes 0-3 LE) and absolute day counter (T_DAY_COU_ABSL, bytes 4-5 LE, day 1 = 1.1.2000).
  uint32_t BMW_328_seconds = 243785948;  // Seeded so the SME thinks the vehicle was made ~7.7 years ago
  uint16_t BMW_328_days = 9244;          // ~23rd April 2025 (days since 1.1.2000)
  uint32_t BMW_328_seconds_to_day = 0;   // Rolls into a day at 86400
  uint8_t paired_vin[17] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  //17 Byte array for paired VIN
  bool pack_limit_info_available = false;
  bool battery_awake = false;

  // Beta CAN-based contactor close (0x53A) state
  bool userRequestContactorClose = false;
  bool userRequestContactorOpen = false;
  // Mirrors the emulator equipment-stop flag onto the contactor request so the main-page
  // "Open/Close Contactors" buttons (which toggle equipment stop) actually drive the contactors.
  bool phev_last_equipment_stop = false;
  bool contactorCloseReq = false;              // Desired contactor state: true = stream closing/steady frames
  unsigned long contactor_close_start_ms = 0;  // millis() when a close was last requested
  // Pre-close balancing-stop phase: number of guarded stopRoutine frames still to send before 0x10B
  // is allowed to close (balancing blocks contactor close in the SME), and the timestamp of the last
  // one sent (0 = send immediately).
  uint8_t phev_pre_close_stops_remaining = 0;
  unsigned long phev_pre_close_stop_last_ms = 0;
  static const uint8_t PHEV_PRE_CLOSE_STOP_COUNT = 3;                 // Number of stops to send before close
  static const unsigned long PHEV_PRE_CLOSE_STOP_INTERVAL_MS = 1000;  // One per UDS-guard window
  // Balancing start/stop is sent as a guarded burst on request CHANGE (like the pre-close burst),
  // not continuously in a timed loop - a single periodic send was getting missed by the SME.
  bool phev_last_balancing_request = false;     // Tracks user_requests_balancing to detect edges
  bool phev_balancing_burst_start = false;      // true = burst START frames, false = burst STOP frames
  uint8_t phev_balancing_bursts_remaining = 0;  // Guarded balancing frames still to send
  unsigned long phev_balancing_burst_last_ms = 0;
  static const uint8_t PHEV_BALANCING_BURST_COUNT = 3;                 // Frames to send per request change
  static const unsigned long PHEV_BALANCING_BURST_INTERVAL_MS = 1000;  // One per UDS-guard window
  // How long to stream the STATE B closing frame before settling to STATE D steady (observed ~3.5s)
  static const unsigned long CONTACTOR_CLOSE_REQUEST_DURATION_MS = 1000;

  // 0x53A contactor-announce state machine. Replays the OEM open->close handshake observed on the
  // vehicle bus so the SME sees STATE B (close requested) announced for ~3.5s BEFORE 0x10B drives
  // the contactors closed. Jumping straight to closed (STATE D + 0x10B close at once) was
  // triggering an intermittent open->close safety warning.
  // Sequence: A IDLE -> B CLOSE_REQ (held) -> C ESCALATED (single transient) -> D STEADY.
  //   0x10B is held open until the state machine reaches ESCALATED/STEADY (i.e. after the B hold).
  // See "0x53A STATE SEQUENCE" in the CONTACTOR CLOSE notes of the .cpp.
  enum Phev53AState { PHEV_53A_IDLE, PHEV_53A_CLOSE_REQ, PHEV_53A_ESCALATED, PHEV_53A_STEADY };
  Phev53AState phev_53a_state = PHEV_53A_IDLE;

  struct InverterContactorCloseRequestStruct {
    bool previous;
    bool present;
  };
  InverterContactorCloseRequestStruct InverterContactorCloseRequest = {false, false};
};

#endif
