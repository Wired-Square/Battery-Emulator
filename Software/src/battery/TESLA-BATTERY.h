#ifndef TESLA_BATTERY_H
#define TESLA_BATTERY_H
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "BatterySlotContext.h"
#include "CanBattery.h"

// 0x7FF gateway config, "Gen3" vehicles only, not applicable to Gen2 "classic" Model S and Model X
// These are user configurable from the Webserver UI
extern bool user_selected_tesla_digital_HVIL;
extern uint16_t user_selected_tesla_GTW_country;
extern bool user_selected_tesla_GTW_rightHandDrive;
extern uint16_t user_selected_tesla_GTW_mapRegion;
extern uint16_t user_selected_tesla_GTW_chassisType;
extern uint16_t user_selected_tesla_GTW_packEnergy;

// Advances the 0x213 alive counter held in the frame itself; per-frame safe,
// so each instance's member frame keeps its own sequence.
void generateTESLA_213(CAN_frame& f);

class TeslaBattery : public CanBattery {
 public:
  TeslaBattery(const BatterySlotContext& ctx)
      : CanBattery(ctx.can_interface), type_(battery_type_for_slot(ctx.slot)) {
    datalayer_battery = ctx.datalayer;
    allows_contactor_closing = ctx.is_primary() ? ctx.contactor_flag : nullptr;
    // The webserver/MQTT read the shared extended block, which belongs to the
    // primary; a secondary gets its own so it cannot overwrite the primary's.
    datalayer_tesla = ctx.is_primary() ? &datalayer_extended.tesla : new DATALAYER_INFO_TESLA();
  }

  virtual void setup();
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);

  bool supports_charged_energy() { return true; }

  const std::vector<BatteryCommand>& get_commands() override { return commands_; }

  bool supports_insulation_resistance() override { return true; }

  const char* get_dtc_json_filename() override { return "tesla_model3y_dtc.json"; }

  void write_advanced_status(AdvancedStatusWriter& out) override {

    float beginning_of_life = static_cast<float>(datalayer_tesla->battery_beginning_of_life);
    float battTempPct = static_cast<float>(datalayer_tesla->battery_battTempPct) * 0.4f;
    float dcdcLvBusVolt = static_cast<float>(datalayer_tesla->battery_dcdcLvBusVolt) * 0.0390625f;
    float dcdcHvBusVolt = static_cast<float>(datalayer_tesla->battery_dcdcHvBusVolt) * 0.146484f;
    float dcdcLvOutputCurrent = static_cast<float>(datalayer_tesla->battery_dcdcLvOutputCurrent) * 0.1f;
    float nominal_full_pack_energy =
        static_cast<float>(datalayer_tesla->battery_nominal_full_pack_energy) * 0.1f;
    float nominal_full_pack_energy_m0 =
        static_cast<float>(datalayer_tesla->battery_nominal_full_pack_energy_m0) * 0.02f;
    float nominal_energy_remaining =
        static_cast<float>(datalayer_tesla->battery_nominal_energy_remaining) * 0.1f;
    float nominal_energy_remaining_m0 =
        static_cast<float>(datalayer_tesla->battery_nominal_energy_remaining_m0) * 0.02f;
    float ideal_energy_remaining = static_cast<float>(datalayer_tesla->battery_ideal_energy_remaining) * 0.1f;
    float ideal_energy_remaining_m0 =
        static_cast<float>(datalayer_tesla->battery_ideal_energy_remaining_m0) * 0.02f;
    float energy_to_charge_complete =
        static_cast<float>(datalayer_tesla->battery_energy_to_charge_complete) * 0.1f;
    float energy_to_charge_complete_m1 =
        static_cast<float>(datalayer_tesla->battery_energy_to_charge_complete_m1) * 0.02f;
    float energy_buffer = static_cast<float>(datalayer_tesla->battery_energy_buffer) * 0.1f;
    float energy_buffer_m1 = static_cast<float>(datalayer_tesla->battery_energy_buffer_m1) * 0.01f;
    float expected_energy_remaining_m1 =
        static_cast<float>(datalayer_tesla->battery_expected_energy_remaining_m1) * 0.02f;
    float total_discharge = static_cast<float>(datalayer_battery->status.total_discharged_battery_Wh) * 0.001f;
    float total_charge = static_cast<float>(datalayer_battery->status.total_charged_battery_Wh) * 0.001f;
    float packMass = static_cast<float>(datalayer_tesla->battery_packMass);
    float platformMaxBusVoltage =
        static_cast<float>(datalayer_tesla->battery_platformMaxBusVoltage) * 0.1f + 375;
    float bms_min_voltage = static_cast<float>(datalayer_tesla->BMS_min_voltage) * 0.01f * 2;
    float bms_max_voltage = static_cast<float>(datalayer_tesla->BMS_max_voltage) * 0.01f * 2;
    float max_charge_current = static_cast<float>(datalayer_tesla->battery_max_charge_current);
    float max_discharge_current = static_cast<float>(datalayer_tesla->battery_max_discharge_current);
    float soc_ave = static_cast<float>(datalayer_tesla->battery_soc_ave) * 0.1f;
    float soc_max = static_cast<float>(datalayer_tesla->battery_soc_max) * 0.1f;
    float soc_min = static_cast<float>(datalayer_tesla->battery_soc_min) * 0.1f;
    float soc_ui = static_cast<float>(datalayer_tesla->battery_soc_ui) * 0.1f;
    float BrickVoltageMax = static_cast<float>(datalayer_tesla->battery_BrickVoltageMax) * 0.002f;
    float BrickVoltageMin = static_cast<float>(datalayer_tesla->battery_BrickVoltageMin) * 0.002f;
    float isolationResistance = static_cast<float>(datalayer_tesla->BMS_isolationResistance) * 10;
    float PCS_dcdcMaxOutputCurrentAllowed =
        static_cast<float>(datalayer_tesla->PCS_dcdcMaxOutputCurrentAllowed) * 0.1f;
    float PCS_dcdcTemp = static_cast<float>(datalayer_tesla->PCS_dcdcTemp) * 0.1f + 40.0f;
    float PCS_ambientTemp = static_cast<float>(datalayer_tesla->PCS_ambientTemp) * 0.1f + 40.0f;
    float PCS_chgPhATemp = static_cast<float>(datalayer_tesla->PCS_chgPhATemp) * 0.1f + 40.0f;
    float PCS_chgPhBTemp = static_cast<float>(datalayer_tesla->PCS_chgPhBTemp) * 0.1f + 40.0f;
    float PCS_chgPhCTemp = static_cast<float>(datalayer_tesla->PCS_chgPhCTemp) * 0.1f + 40.0f;
    float BMS_maxRegenPower = static_cast<float>(datalayer_tesla->BMS_maxRegenPower) * 0.01f;
    float BMS_maxDischargePower = static_cast<float>(datalayer_tesla->BMS_maxDischargePower) * 0.013f;
    float BMS_powerDissipation = static_cast<float>(datalayer_tesla->BMS_powerDissipation) * 0.02f;
    float BMS_flowRequest = static_cast<float>(datalayer_tesla->BMS_flowRequest) * 0.3f;
    float BMS_inletActiveCoolTargetT =
        static_cast<float>(datalayer_tesla->BMS_inletActiveCoolTargetT) * 0.25f - 25;
    float BMS_inletPassiveTargetT = static_cast<float>(datalayer_tesla->BMS_inletPassiveTargetT) * 0.25f - 25;
    float BMS_inletActiveHeatTargetT =
        static_cast<float>(datalayer_tesla->BMS_inletActiveHeatTargetT) * 0.25f - 25;
    float BMS_packTMin = static_cast<float>(datalayer_tesla->BMS_packTMin) * 0.25f - 25;
    float BMS_packTMax = static_cast<float>(datalayer_tesla->BMS_packTMax) * 0.25f - 25;
    float PCS_dcdcMaxLvOutputCurrent = static_cast<float>(datalayer_tesla->PCS_dcdcMaxLvOutputCurrent) * 0.1f;
    float PCS_dcdcCurrentLimit = static_cast<float>(datalayer_tesla->PCS_dcdcCurrentLimit) * 0.1f;
    float PCS_dcdcLvOutputCurrentTempLimit =
        static_cast<float>(datalayer_tesla->PCS_dcdcLvOutputCurrentTempLimit) * 0.1f;
    float PCS_dcdcUnifiedCommand = static_cast<float>(datalayer_tesla->PCS_dcdcUnifiedCommand) * 0.001f;
    float PCS_dcdcCLAControllerOutput =
        static_cast<float>(datalayer_tesla->PCS_dcdcCLAControllerOutput * 0.001f);
    float PCS_dcdcTankVoltage = static_cast<float>(datalayer_tesla->PCS_dcdcTankVoltage);
    float PCS_dcdcTankVoltageTarget = static_cast<float>(datalayer_tesla->PCS_dcdcTankVoltageTarget);
    float PCS_dcdcClaCurrentFreq = static_cast<float>(datalayer_tesla->PCS_dcdcClaCurrentFreq) * 0.0976563f;
    float PCS_dcdcTCommMeasured = static_cast<float>(datalayer_tesla->PCS_dcdcTCommMeasured) * 0.00195313f;
    float PCS_dcdcShortTimeUs = static_cast<float>(datalayer_tesla->PCS_dcdcShortTimeUs) * 0.000488281f;
    float PCS_dcdcHalfPeriodUs = static_cast<float>(datalayer_tesla->PCS_dcdcHalfPeriodUs) * 0.000488281f;
    float PCS_dcdcIntervalMaxFrequency = static_cast<float>(datalayer_tesla->PCS_dcdcIntervalMaxFrequency);
    float PCS_dcdcIntervalMaxHvBusVolt =
        static_cast<float>(datalayer_tesla->PCS_dcdcIntervalMaxHvBusVolt) * 0.1f;
    float PCS_dcdcIntervalMaxLvBusVolt =
        static_cast<float>(datalayer_tesla->PCS_dcdcIntervalMaxLvBusVolt) * 0.1f;
    float PCS_dcdcIntervalMaxLvOutputCurr =
        static_cast<float>(datalayer_tesla->PCS_dcdcIntervalMaxLvOutputCurr);
    float PCS_dcdcIntervalMinFrequency = static_cast<float>(datalayer_tesla->PCS_dcdcIntervalMinFrequency);
    float PCS_dcdcIntervalMinHvBusVolt =
        static_cast<float>(datalayer_tesla->PCS_dcdcIntervalMinHvBusVolt) * 0.1f;
    float PCS_dcdcIntervalMinLvBusVolt =
        static_cast<float>(datalayer_tesla->PCS_dcdcIntervalMinLvBusVolt) * 0.1f;
    float PCS_dcdcIntervalMinLvOutputCurr =
        static_cast<float>(datalayer_tesla->PCS_dcdcIntervalMinLvOutputCurr);
    float PCS_dcdc12vSupportLifetimekWh =
        static_cast<float>(datalayer_tesla->PCS_dcdc12vSupportLifetimekWh) * 0.01f;
    float HVP_hvp1v5Ref = static_cast<float>(datalayer_tesla->HVP_hvp1v5Ref) * 0.1f;
    float HVP_shuntCurrentDebug = static_cast<float>(datalayer_tesla->HVP_shuntCurrentDebug) * 0.1f;
    float HVP_dcLinkVoltage = static_cast<float>(datalayer_tesla->HVP_dcLinkVoltage) * 0.1f;
    float HVP_packVoltage = static_cast<float>(datalayer_tesla->HVP_packVoltage) * 0.1f;
    float HVP_packContVoltage = static_cast<float>(datalayer_tesla->HVP_packContVoltage) * 0.1f;
    float HVP_pyroAnalog = static_cast<float>(datalayer_tesla->HVP_pyroAnalog) * 0.1f;
    float HVP_hvilInVoltage = static_cast<float>(datalayer_tesla->HVP_hvilInVoltage) * 0.1f;
    float HVP_hvilOutVoltage = static_cast<float>(datalayer_tesla->HVP_hvilOutVoltage) * 0.1f;
    float HVP_packContCoilCurrent = static_cast<float>(datalayer_tesla->HVP_packContCoilCurrent) * 0.1f;
    float HVP_battery12V = static_cast<float>(datalayer_tesla->HVP_battery12V) * 0.1f;

    static const char* contactorText[] = {"UNKNOWN(0)",  "OPEN",        "CLOSING",    "BLOCKED", "OPENING",
                                          "CLOSED",      "UNKNOWN(6)",  "WELDED",     "POS_CL",  "NEG_CL",
                                          "UNKNOWN(10)", "UNKNOWN(11)", "UNKNOWN(12)"};
    static const char* hvilStatusState[] = {"UNKNOWN or CONTACTORS OPEN",
                                            "STATUS_OK",
                                            "CURRENT_SOURCE_FAULT",
                                            "INTERNAL_OPEN_FAULT",
                                            "VEHICLE_OPEN_FAULT",
                                            "PENTHOUSE_LID_OPEN_FAULT",
                                            "UNKNOWN_LOCATION_OPEN_FAULT",
                                            "VEHICLE_NODE_FAULT",
                                            "NO_12V_SUPPLY",
                                            "VEHICLE_OR_PENTHOUSE_LID_OPENFAULT",
                                            "UNKNOWN(10)",
                                            "UNKNOWN(11)",
                                            "UNKNOWN(12)",
                                            "UNKNOWN(13)",
                                            "UNKNOWN(14)",
                                            "UNKNOWN(15)"};
    static const char* contactorState[] = {"SNA",        "OPEN",       "PRECHARGE",   "BLOCKED",
                                           "PULLED_IN",  "OPENING",    "ECONOMIZED",  "WELDED",
                                           "UNKNOWN(8)", "UNKNOWN(9)", "UNKNOWN(10)", "UNKNOWN(11)"};
    static const char* BMS_state[] = {"STANDBY",     "DRIVE", "SUPPORT", "CHARGE", "FEIM",
                                      "CLEAR_FAULT", "FAULT", "WELD",    "TEST",   "SNA"};
    static const char* BMS_contactorState[] = {"SNA", "OPEN", "OPENING", "CLOSING", "CLOSED", "WELDED", "BLOCKED"};
    static const char* BMS_hvState[] = {"DOWN",          "COMING_UP",        "GOING_DOWN", "UP_FOR_DRIVE",
                                        "UP_FOR_CHARGE", "UP_FOR_DC_CHARGE", "UP"};
    static const char* BMS_uiChargeStatus[] = {"DISCONNECTED", "NO_POWER",        "ABOUT_TO_CHARGE",
                                               "CHARGING",     "CHARGE_COMPLETE", "CHARGE_STOPPED"};
    static const char* PCS_dcdcStatus[] = {"IDLE", "ACTIVE", "FAULTED"};
    static const char* PCS_dcdcMainState[] = {"STANDBY",          "12V_SUPPORT_ACTIVE", "PRECHARGE_STARTUP",
                                              "PRECHARGE_ACTIVE", "DIS_HVBUS_ACTIVE",   "SHUTDOWN",
                                              "FAULTED"};
    static const char* PCS_dcdcSubState[] = {"PWR_UP_INIT",
                                             "STANDBY",
                                             "12V_SUPPORT_ACTIVE",
                                             "DIS_HVBUS",
                                             "PCHG_FAST_DIS_HVBUS",
                                             "PCHG_SLOW_DIS_HVBUS",
                                             "PCHG_DWELL_CHARGE",
                                             "PCHG_DWELL_WAIT",
                                             "PCHG_DI_RECOVERY_WAIT",
                                             "PCHG_ACTIVE",
                                             "PCHG_FLT_FAST_DIS_HVBUS",
                                             "SHUTDOWN",
                                             "12V_SUPPORT_FAULTED",
                                             "DIS_HVBUS_FAULTED",
                                             "PCHG_FAULTED",
                                             "CLEAR_FAULTS",
                                             "FAULTED",
                                             "NUM"};
    static const char* BMS_powerLimitState[] = {"NOT_CALCULATED_FOR_DRIVE", "CALCULATED_FOR_DRIVE"};
    static const char* HVP_contactor[] = {"NOT_ACTIVE", "ACTIVE", "COMPLETED"};
    static const char* falseTrue[] = {"False", "True"};
    static const char* noYes[] = {"No", "Yes"};

    // ---- Leading (untitled) section: main battery / contactor info ----
    out.section("");

    char readableBatterySerialNumber[15];  // One extra space for null terminator
    memcpy(readableBatterySerialNumber, datalayer_tesla->battery_serialNumber,
           sizeof(datalayer_tesla->battery_serialNumber));
    readableBatterySerialNumber[14] = '\0';  // Null terminate the string
    out.kv(TL("Battery Serial Number"), String(readableBatterySerialNumber));
    char readableBatteryPartNumber[13];  // One extra space for null terminator
    memcpy(readableBatteryPartNumber, datalayer_tesla->battery_partNumber,
           sizeof(datalayer_tesla->battery_partNumber));
    readableBatteryPartNumber[12] = '\0';  // Null terminate the string
    out.kv(TL("Battery Part Number"), String(readableBatteryPartNumber));
    char readablePCSPartNumber[13];  // One extra space for null terminator
    memcpy(readablePCSPartNumber, datalayer_tesla->PCS_partNumber,
           sizeof(datalayer_tesla->PCS_partNumber));
    readablePCSPartNumber[12] = '\0';  // Null terminate the string
    out.kv(TL("PCS Part Number"), String(readablePCSPartNumber));
    out.kv(TL("Battery Manufacture Date"), String(datalayer_tesla->battery_manufactureDate));
    out.kv(TL("Battery Pack Mass"), String(packMass), "KG");
    out.kv(TL("Battery Total Discharge"), String(total_discharge), "kWh");
    out.kv(TL("Battery Total Charge"), String(total_charge), "kWh");
    out.kv(TL("HVIL Status"), String(hvilStatusState[datalayer_tesla->hvil_status]));
    out.kv(TL("HVP Contactor State"), String(contactorText[datalayer_tesla->packContactorSetState]));
    out.kv(TL("BMS Contactor State"), String(BMS_contactorState[datalayer_tesla->BMS_contactorState]));
    out.kv(TL("Negative Contactor"), String(contactorState[datalayer_tesla->packContNegativeState]));
    out.kv(TL("Positive Contactor"), String(contactorState[datalayer_tesla->packContPositiveState]));
    String closing_blocked = String(noYes[datalayer_tesla->packCtrsClosingBlocked]);
    if (datalayer_tesla->packContactorSetState == 5) {  //Closed
      closing_blocked += " (already CLOSED)";
    }
    out.kv(TL("Closing blocked"), closing_blocked);
    out.kv(TL("Pyrotest in progress"), String(noYes[datalayer_tesla->pyroTestInProgress]));
    out.kv(TL("Contactors Open Now Requested"),
                                     String(noYes[datalayer_tesla->battery_packCtrsOpenNowRequested]));
    out.kv(TL("Contactors Open Requested"), String(noYes[datalayer_tesla->battery_packCtrsOpenRequested]));
    out.kv(
        TL("Contactors Request Status"), String(HVP_contactor[datalayer_tesla->battery_packCtrsRequestStatus]));
    out.kv(TL("Contactors Reset Request Required"),
                                     String(noYes[datalayer_tesla->battery_packCtrsResetRequestRequired]));
    out.kv(
        TL("DC Link Allowed to Energize"), String(noYes[datalayer_tesla->battery_dcLinkAllowedToEnergize]));

    // ---- "BMS 0x352 w/o mux" or "BMS 0x352 w/ mux" section: everything through the HVP debug block ----
    if (datalayer_tesla->BMS352_mux == false) {
      out.section("BMS 0x352 w/o mux");
      out.kv(TL("Calculated SOH"), String(nominal_full_pack_energy * 100 / beginning_of_life));
      out.kv(TL("Nominal Full Pack Energy"), String(nominal_full_pack_energy), "kWh");
      out.kv(TL("Nominal Energy Remaining"), String(nominal_energy_remaining), "kWh");
      out.kv(TL("Ideal Energy Remaining"), String(ideal_energy_remaining), "kWh");
      out.kv(TL("Energy to Charge Complete"), String(energy_to_charge_complete), "kWh");
      out.kv(TL("Energy Buffer"), String(energy_buffer), "kWh");
      out.kv(TL("Full Charge Complete"), String(noYes[datalayer_tesla->battery_full_charge_complete]));
    } else {
      out.section("BMS 0x352 w/ mux");
      out.kv(TL("Calculated SOH"), String(nominal_full_pack_energy_m0 * 100 / beginning_of_life));
      out.kv(TL("Nominal Full Pack Energy"), String(nominal_full_pack_energy_m0), "kWh");
      out.kv(TL("Nominal Energy Remaining"), String(nominal_energy_remaining_m0), "kWh");
      out.kv(TL("Ideal Energy Remaining"), String(ideal_energy_remaining_m0), "kWh");
      out.kv(TL("Energy to Charge Complete"), String(energy_to_charge_complete_m1), "kWh");
      out.kv(TL("Energy Buffer"), String(energy_buffer_m1), "kWh");
      out.kv(TL("Expected Energy Remaining"), String(expected_energy_remaining_m1), "kWh");
      out.kv(TL("Fully Charged"), String(noYes[datalayer_tesla->battery_fully_charged]));
    }
    out.kv(TL("Isolation Resistance"), String(isolationResistance), "kOhms");
    out.kv(TL("BMS State"), String(BMS_state[datalayer_tesla->BMS_state]));
    out.kv(TL("BMS HV State"), String(BMS_hvState[datalayer_tesla->BMS_hvState]));
    out.kv(TL("BMS UI Charge Status"), String(BMS_uiChargeStatus[datalayer_tesla->BMS_uiChargeStatus]));
    out.kv("BMS_buildConfigId", String(datalayer_tesla->BMS_info_buildConfigId));
    out.kv("BMS_hardwareId", String(datalayer_tesla->BMS_info_hardwareId));
    out.kv("BMS_componentId", String(datalayer_tesla->BMS_info_componentId));
    if (datalayer_tesla->BMS_pcsPwmEnabled) {
      out.kv(TL("BMS PCS PWM Enabled"), "ACTIVE");
    }
    out.kv(TL("Battery Beginning of Life"), String(beginning_of_life), "kWh");
    out.kv(TL("Battery SOC UI"), String(soc_ui));
    out.kv(TL("Battery SOC Ave"), String(soc_ave));
    out.kv(TL("Battery SOC Max"), String(soc_max));
    out.kv(TL("Battery SOC Min"), String(soc_min));
    out.kv(TL("Battery Temp Percent"), String(battTempPct));
    out.kv(TL("PCS Lv Output"), String(dcdcLvOutputCurrent), "A");
    out.kv(TL("PCS Lv Bus"), String(dcdcLvBusVolt), "V");
    out.kv(TL("PCS Hv Bus"), String(dcdcHvBusVolt), "V");
    out.kv(TL("Platform Max Bus Voltage"), String(platformMaxBusVoltage), "V");
    out.kv(TL("BMS Min Voltage"), String(bms_min_voltage), "V");
    out.kv(TL("BMS Max Voltage"), String(bms_max_voltage), "V");
    out.kv(TL("Max Charge Current"), String(max_charge_current), "A");
    out.kv(TL("Max Discharge Current"), String(max_discharge_current), "A");
    out.kv(TL("Brick Voltage Max"), String(BrickVoltageMax), "V");
    out.kv(TL("Brick Voltage Min"), String(BrickVoltageMin), "V");
    out.kv(TL("Brick Temp Max Num"), String(datalayer_tesla->battery_BrickTempMaxNum));
    out.kv(TL("Brick Temp Min Num"), String(datalayer_tesla->battery_BrickTempMinNum));
    out.kv(TL("PCS dcdc Temp"), String(PCS_dcdcTemp), "DegC");
    out.kv(TL("PCS Ambient Temp"), String(PCS_ambientTemp), "DegC");
    out.kv(TL("PCS Chg PhA Temp"), String(PCS_chgPhATemp), "DegC");
    out.kv(TL("PCS Chg PhB Temp"), String(PCS_chgPhBTemp), "DegC");
    out.kv(TL("PCS Chg PhC Temp"), String(PCS_chgPhCTemp), "DegC");
    out.kv(TL("Max Regen Power"), String(BMS_maxRegenPower), "kW");
    out.kv(TL("Max Discharge Power"), String(BMS_maxDischargePower), "kW");
    out.kv(TL("Power Limit State"), String(BMS_powerLimitState[datalayer_tesla->BMS_powerLimitState]));
    out.kv(TL("Power Dissipation"), String(BMS_powerDissipation), "kW");
    out.kv(TL("Flow Request"), String(BMS_flowRequest), "LPM");
    out.kv(TL("Inlet Active Cool Target Temp"), String(BMS_inletActiveCoolTargetT), "DegC");
    out.kv(TL("Inlet Passive Target Temp"), String(BMS_inletPassiveTargetT), "DegC");
    out.kv(TL("Inlet Active Heat Target Temp"), String(BMS_inletActiveHeatTargetT), "DegC");
    out.kv(TL("Pack Temp Min"), String(BMS_packTMin), "DegC");
    out.kv(TL("Pack Temp Max"), String(BMS_packTMax), "DegC");
    if (datalayer_tesla->BMS_pcsNoFlowRequest) {
      out.kv(TL("PCS No Flow Request"), "ACTIVE");
    }
    if (datalayer_tesla->BMS_noFlowRequest) {
      out.kv(TL("BMS No Flow Request"), "ACTIVE");
    }
    out.kv(TL("Precharge Status"), String(PCS_dcdcStatus[datalayer_tesla->PCS_dcdcPrechargeStatus]));
    out.kv(TL("12V Support Status"), String(PCS_dcdcStatus[datalayer_tesla->PCS_dcdc12VSupportStatus]));
    out.kv(
        TL("HV Bus Discharge Status"), String(PCS_dcdcStatus[datalayer_tesla->PCS_dcdcHvBusDischargeStatus]));
    out.kv(TL("Main State"), String(PCS_dcdcMainState[datalayer_tesla->PCS_dcdcMainState]));
    out.kv(TL("Sub State"), String(PCS_dcdcSubState[datalayer_tesla->PCS_dcdcSubState]));
    if (datalayer_tesla->PCS_dcdcFaulted) {
      out.kv(TL("PCS Faulted"), "ACTIVE");
    }
    if (datalayer_tesla->PCS_dcdcOutputIsLimited) {
      out.kv(TL("Output Is Limited"), "ACTIVE");
    }
    out.kv(TL("Max Output Current Allowed"), String(PCS_dcdcMaxOutputCurrentAllowed), "A");
    out.kv(TL("Precharge Rty Cnt"), String(falseTrue[datalayer_tesla->PCS_dcdcPrechargeRtyCnt]));
    out.kv(TL("12V Support Rty Cnt"), String(falseTrue[datalayer_tesla->PCS_dcdc12VSupportRtyCnt]));
    out.kv(TL("Discharge Rty Cnt"), String(falseTrue[datalayer_tesla->PCS_dcdcDischargeRtyCnt]));
    if (datalayer_tesla->PCS_dcdcPwmEnableLine) {
      out.kv(TL("PWM Enable Line"), "ACTIVE");
    }
    if (datalayer_tesla->PCS_dcdcSupportingFixedLvTarget) {
      out.kv(TL("Supporting Fixed LV Target"), "ACTIVE");
    }
    out.kv(TL("Precharge Restart Cnt"), String(falseTrue[datalayer_tesla->PCS_dcdcPrechargeRestartCnt]));
    out.kv(TL("Initial Precharge Substate"),
                                    String(PCS_dcdcSubState[datalayer_tesla->PCS_dcdcInitialPrechargeSubState]));
    out.kv("PCS_buildConfigId", String(datalayer_tesla->PCS_info_buildConfigId));
    out.kv("PCS_hardwareId", String(datalayer_tesla->PCS_info_hardwareId));
    out.kv("PCS_componentId", String(datalayer_tesla->PCS_info_componentId));
    out.kv("PCS_dcdcMaxLvOutputCurrent", String(PCS_dcdcMaxLvOutputCurrent), "A");
    out.kv("PCS_dcdcCurrentLimit", String(PCS_dcdcCurrentLimit), "A");
    out.kv("PCS_dcdcLvOutputCurrentTempLimit", String(PCS_dcdcLvOutputCurrentTempLimit), "A");
    out.kv("PCS_dcdcUnifiedCommand", String(PCS_dcdcUnifiedCommand));
    out.kv("PCS_dcdcCLAControllerOutput", String(PCS_dcdcCLAControllerOutput));
    out.kv("PCS_dcdcTankVoltage", String(PCS_dcdcTankVoltage), "V");
    out.kv("PCS_dcdcTankVoltageTarget", String(PCS_dcdcTankVoltageTarget), "V");
    out.kv("PCS_dcdcClaCurrentFreq", String(PCS_dcdcClaCurrentFreq), "kHz");
    out.kv("PCS_dcdcTCommMeasured", String(PCS_dcdcTCommMeasured), "us");
    out.kv("PCS_dcdcShortTimeUs", String(PCS_dcdcShortTimeUs), "us");
    out.kv("PCS_dcdcHalfPeriodUs", String(PCS_dcdcHalfPeriodUs), "us");
    out.kv("PCS_dcdcIntervalMaxFrequency", String(PCS_dcdcIntervalMaxFrequency), "kHz");
    out.kv("PCS_dcdcIntervalMaxHvBusVolt", String(PCS_dcdcIntervalMaxHvBusVolt), "V");
    out.kv("PCS_dcdcIntervalMaxLvBusVolt", String(PCS_dcdcIntervalMaxLvBusVolt), "V");
    out.kv("PCS_dcdcIntervalMaxLvOutputCurr", String(PCS_dcdcIntervalMaxLvOutputCurr), "A");
    out.kv("PCS_dcdcIntervalMinFrequency", String(PCS_dcdcIntervalMinFrequency), "kHz");
    out.kv("PCS_dcdcIntervalMinHvBusVolt", String(PCS_dcdcIntervalMinHvBusVolt), "V");
    out.kv("PCS_dcdcIntervalMinLvBusVolt", String(PCS_dcdcIntervalMinLvBusVolt), "V");
    out.kv("PCS_dcdcIntervalMinLvOutputCurr", String(PCS_dcdcIntervalMinLvOutputCurr), "A");
    out.kv("PCS_dcdc12vSupportLifetimekWh", String(PCS_dcdc12vSupportLifetimekWh), "kWh");
    out.kv("HVP_buildConfigId", String(datalayer_tesla->HVP_info_buildConfigId));
    out.kv("HVP_hardwareId", String(datalayer_tesla->HVP_info_hardwareId));
    out.kv("HVP_componentId", String(datalayer_tesla->HVP_info_componentId));
    out.kv("HVP_battery12V", String(HVP_battery12V), "V");
    out.kv("HVP_dcLinkVoltage", String(HVP_dcLinkVoltage), "V");
    out.kv("HVP_packVoltage", String(HVP_packVoltage), "V");
    out.kv("HVP_packContVoltage", String(HVP_packContVoltage), "V");
    out.kv("HVP_packContCoilCurrent", String(HVP_packContCoilCurrent), "A");
    out.kv("HVP_pyroAnalog", String(HVP_pyroAnalog), "V");
    out.kv("HVP_hvp1v5Ref", String(HVP_hvp1v5Ref), "V");
    out.kv("HVP_hvilInVoltage", String(HVP_hvilInVoltage), "V");
    out.kv("HVP_hvilOutVoltage", String(HVP_hvilOutVoltage), "V");
    if (datalayer_tesla->HVP_gpioPassivePyroDepl) {
      out.kv("HVP_gpioPassivePyroDepl", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioPyroIsoEn) {
      out.kv("HVP_gpioPyroIsoEn", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioCpFaultIn) {
      out.kv("HVP_gpioCpFaultIn", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioPackContPowerEn) {
      out.kv("HVP_gpioPackContPowerEn", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioHvCablesOk) {
      out.kv("HVP_gpioHvCablesOk", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioHvpSelfEnable) {
      out.kv("HVP_gpioHvpSelfEnable", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioLed) {
      out.kv("HVP_gpioLed", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioCrashSignal) {
      out.kv("HVP_gpioCrashSignal", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioShuntDataReady) {
      out.kv("HVP_gpioShuntDataReady", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioFcContPosAux) {
      out.kv("HVP_gpioFcContPosAux", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioFcContNegAux) {
      out.kv("HVP_gpioFcContNegAux", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioBmsEout) {
      out.kv("HVP_gpioBmsEout", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioCpFaultOut) {
      out.kv("HVP_gpioCpFaultOut", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioPyroPor) {
      out.kv("HVP_gpioPyroPor", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioShuntEn) {
      out.kv("HVP_gpioShuntEn", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioHvpVerEn) {
      out.kv("HVP_gpioHvpVerEn", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioPackCoontPosFlywheel) {
      out.kv("HVP_gpioPackCoontPosFlywheel", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioCpLatchEnable) {
      out.kv("HVP_gpioCpLatchEnable", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioPcsEnable) {
      out.kv("HVP_gpioPcsEnable", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioPcsDcdcPwmEnable) {
      out.kv("HVP_gpioPcsDcdcPwmEnable", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioPcsChargePwmEnable) {
      out.kv("HVP_gpioPcsChargePwmEnable", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioFcContPowerEnable) {
      out.kv("HVP_gpioFcContPowerEnable", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioHvilEnable) {
      out.kv("HVP_gpioHvilEnable", "ACTIVE");
    }
    if (datalayer_tesla->HVP_gpioSecDrdy) {
      out.kv("HVP_gpioSecDrdy", "ACTIVE");
    }
    out.kv("HVP_shuntCurrentDebug", String(HVP_shuntCurrentDebug), "A");
    out.kv("HVP_packCurrentMia", String(noYes[datalayer_tesla->HVP_packCurrentMia]));
    out.kv("HVP_auxCurrentMia", String(noYes[datalayer_tesla->HVP_auxCurrentMia]));
    out.kv("HVP_currentSenseMia", String(noYes[datalayer_tesla->HVP_currentSenseMia]));
    out.kv("HVP_shuntRefVoltageMismatch",
                                    String(noYes[datalayer_tesla->HVP_shuntRefVoltageMismatch]));
    out.kv("HVP_shuntThermistorMia", String(noYes[datalayer_tesla->HVP_shuntThermistorMia]));
    out.kv("HVP_shuntHwMia", String(noYes[datalayer_tesla->HVP_shuntHwMia]));

    // ---- "Active Faults: N" section: bespoke ECU/Description table ----
    {
      struct AlertGroup {
        const char* label;
        int base;  // integer "code" base for this ECU (BMS 1xx, PCS 2xx, CP 3xx)
        const bool* active;
        int count;
      };
      const AlertGroup groups[] = {
          {"BMS 0x320", 100, datalayer_tesla->BMS_alertMatrixActive, 100},
          {"PCS 0x3A4", 200, datalayer_tesla->PCS_alertMatrixActive, 94},
          {"CP 0x31E", 300, datalayer_tesla->CP_alertMatrixActive, 96},
      };

      int total_active = 0;
      for (auto& g : groups) {
        for (int i = 0; i < g.count; i++) {
          if (g.active[i]) {
            total_active++;
          }
        }
      }

      out.section(("Active Faults: " + String(total_active)).c_str());
      if (total_active > 0) {
        out.table("", {TL("ECU"), TL("Description")}, get_dtc_json_filename());
        for (auto& g : groups) {
          for (int i = 0; i < g.count; i++) {
            if (!g.active[i]) {
              continue;
            }
            // Integer match key; the catalogue maps it to the Tesla code and description.
            out.row_begin(String(g.base + i).c_str());
            out.cell(String(g.label));
            out.cell(String(g.base + i));
            out.row_end();
          }
        }
      }
    }
  }

 private:
  void clear_isolation() { datalayer_battery->settings.user_requests_tesla_isolation_clear = true; }
  void reset_BMS() { datalayer_battery->settings.user_requests_tesla_bms_reset = true; }
  void reset_SOC() { datalayer_battery->settings.user_requests_tesla_soc_reset = true; }

  bool balancing_active() const { return datalayer_battery->settings.user_requests_balancing; }
  void start_balancing() { datalayer_battery->settings.user_requests_balancing = true; }
  void stop_balancing() { datalayer_battery->settings.user_requests_balancing = false; }

  std::vector<BatteryCommand> commands_ = {
      command(CMD_CLEAR_ISOLATION, [this] { clear_isolation(); }),
      command(CMD_RESET_BMS, [this] { reset_BMS(); }),
      command(CMD_RESET_SOC, [this] { reset_SOC(); }),
      command(
          CMD_START_BALANCING, [this] { start_balancing(); }, [this] { return !balancing_active(); }),
      command(
          CMD_END_BALANCING, [this] { stop_balancing(); }, [this] { return balancing_active(); }),
  };

 protected:
  /* Do not change anything below this line! */
  static const int RAMPDOWNPOWERALLOWED = 10000;      // What power we ramp down from towards top balancing
  static const int FLOAT_MAX_POWER_W = 200;           // W, what power to allow for top balancing battery
  static const int FLOAT_START_MV = 20;               // mV, how many mV under overvoltage to start float charging
  static const int MAX_PACK_VOLTAGE_SX_NCMA = 4600;   // V+1, if pack voltage goes over this, charge stops
  static const int MIN_PACK_VOLTAGE_SX_NCMA = 2850;   // V+1, if pack voltage goes over this, charge stops
  static const int MAX_PACK_VOLTAGE_3Y_NCMA = 4030;   // V+1, if pack voltage goes over this, charge stops
  static const int MIN_PACK_VOLTAGE_3Y_NCMA = 2850;   // V+1, if pack voltage goes below this, discharge stops
  static const int MAX_PACK_VOLTAGE_3Y_LFP = 3880;    // V+1, if pack voltage goes over this, charge stops
  static const int MIN_PACK_VOLTAGE_3Y_LFP = 2968;    // V+1, if pack voltage goes below this, discharge stops
  static const int MAX_CELL_DEVIATION_NCA_NCM = 500;  //LED turns yellow on the board if mv delta exceeds this value
  static const int MAX_CELL_DEVIATION_LFP = 400;      //LED turns yellow on the board if mv delta exceeds this value
  static const int MAX_CELL_VOLTAGE_NCA_NCM =
      4250;  //Battery is put into emergency stop if one cell goes over this value
  static const int MIN_CELL_VOLTAGE_NCA_NCM =
      2950;                                      //Battery is put into emergency stop if one cell goes below this value
  static const int MAX_CELL_VOLTAGE_LFP = 3650;  //Battery is put into emergency stop if one cell goes over this value
  static const int MIN_CELL_VOLTAGE_LFP = 2800;  //Battery is put into emergency stop if one cell goes below this value

  DATALAYER_BATTERY_TYPE* datalayer_battery;
  DATALAYER_INFO_TESLA* datalayer_tesla;
  const BatteryType type_;

  // Per-instance state for handle_incoming_can_frame.
  // These were previously static locals — static locals are shared across all
  // class instances, which breaks double/triple battery support.
  uint8_t mux = 0;
  uint16_t temp = 0;
  bool mux0_read = false;
  bool mux1_read = false;
  uint16_t brick_volts = 0;      // per-brick voltage scratch variable (0x401)
  uint8_t mux_zero_counter = 0;  // counts mux==0 frames to detect full cell scan
  uint8_t mux_max = 0;           // highest mux index seen so far

  // Per-instance state for transmit_can.
  int transmitPhase = -1;

  // If not null, this battery decides when the contactor can be closed and writes the value here.
  bool* allows_contactor_closing;

  void printFaultCodesIfActive();
  void printFaultCodesPcsCp();  // PCS 0x3A4 + CP 0x31E alert matrices (tesla-m3-pack-findings)

  unsigned long previousMillis10 = 0;    // will store last time a 50ms CAN Message was sent
  unsigned long previousMillis50 = 0;    // will store last time a 50ms CAN Message was sent
  unsigned long previousMillis100 = 0;   // will store last time a 100ms CAN Message was sent
  unsigned long previousMillis500 = 0;   // will store last time a 500ms CAN Message was sent
  unsigned long previousMillis1000 = 0;  // will store last time a 1000ms CAN Message was sent

  //UDS session tracker
  //static bool uds_SessionInProgress = false; // Future use
  //0x221 VCFRONT_LVPowerState
  uint8_t alternateMux = 0;
  uint8_t frameCounter_TESLA_221 = 15;  // Start at 15 for Mux 0
  uint8_t vehicleState = 1;             // "OFF": 0, "DRIVE": 1, "ACCESSORY": 2, "GOING_DOWN": 3
  static const uint8_t CAR_OFF = 0;
  static const uint8_t CAR_DRIVE = 1;
  static const uint8_t ACCESSORY = 2;
  static const uint8_t GOING_DOWN = 3;
  uint8_t powerDownSeconds = 9;  // Car power down (i.e. contactor open) tracking timer, 3 seconds per sendingState
  //0x2E1 VCFRONT_status, 6 mux tracker
  uint8_t muxNumber_TESLA_2E1 = 0;
  //0x334 UI
  bool TESLA_334_INITIAL_SENT = false;
  //0x3A1 VCFRONT_vehicleStatus, 15 frame counter (temporary)
  uint8_t frameCounter_TESLA_3A1 = 0;
  bool timeToMux3A1 = true;
  //0x7FF GTW_carConfig, 5 mux tracker
  uint8_t muxNumber_TESLA_7FF = 0;

  //0x082 UI_tripPlanning: "cycle_time" 1000ms
  static constexpr CAN_frame TESLA_082 = {.FD = false,
                                          .ext_ID = false,
                                          .DLC = 8,
                                          .ID = 0x082,
                                          .data = {0x00, 0x00, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80}};

  //0x102 VCLEFT_doorStatus: "cycle_time" 100ms
  static constexpr CAN_frame TESLA_102 = {.FD = false,
                                          .ext_ID = false,
                                          .DLC = 8,
                                          .ID = 0x102,
                                          .data = {0x22, 0x33, 0x00, 0x00, 0xC0, 0x38, 0x21, 0x08}};

  //0x103 VCRIGHT_doorStatus: "cycle_time" 100ms
  static constexpr CAN_frame TESLA_103 = {.FD = false,
                                          .ext_ID = false,
                                          .DLC = 8,
                                          .ID = 0x103,
                                          .data = {0x22, 0x33, 0x00, 0x00, 0x30, 0xF2, 0x20, 0x02}};

  //0x118 DI_systemStatus: "cycle_time" 50ms, DI_systemStatusChecksum/DI_systemStatusCounter generated via generateFrameCounterChecksum
  CAN_frame TESLA_118 = {.FD = false,
                         .ext_ID = false,
                         .DLC = 8,
                         .ID = 0x118,
                         .data = {0xAB, 0x60, 0x2A, 0x00, 0x00, 0x08, 0x00, 0x00}};
  CAN_frame TESLA_118_digital_hvil = {.FD = false,
                                      .ext_ID = false,
                                      .DLC = 8,
                                      .ID = 0x118,
                                      .data = {0x61, 0x80, 0x30, 0x10, 0x00, 0x08, 0x00, 0x80}};
  uint16_t content_118_digital_hvil[16] = {0x6180, 0x6281, 0x6382, 0x6483, 0x6584, 0x6685, 0x6786, 0x6887,
                                           0x6988, 0x6A89, 0x6B8A, 0x6C8B, 0x6D8C, 0x6E8D, 0x6F8E, 0x708F};

  //0x2A8 CMPD_state: "cycle_time" 100ms, different depending on firmware, semi-manual increment for now
  CAN_frame TESLA_2A8 = {.FD = false,
                         .ext_ID = false,
                         .DLC = 8,
                         .ID = 0x2A8,
                         .data = {0x02, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x2C}};

  //0x213 UI_cruiseControl: "cycle_time" 500ms, UI_speedLimitTick/UI_cruiseControlCounter - different depending on firmware, semi-manual increment for now
  //Ref tesla-m3-pack-findings (fw 2019.20.4.2): 0x213 UI_cruiseControl DLC 3 (this frame uses DLC 2; likely firmware drift)
  CAN_frame TESLA_213 = {.FD = false, .ext_ID = false, .DLC = 2, .ID = 0x213, .data = {0x00, 0x15}};

  //0x221 These frames will/should eventually be migrated to 2 base frames (1 per mux), and then just the relevant bits changed

  //0x221 VCFRONT_LVPowerState "Drive"
  //VCFRONT_vehiclePowerState VEHICLE_POWER_STATE_DRIVE (Mux0, Counter 15): "cycle_time" 50ms each mux/LVPowerStateIndex, VCFRONT_LVPowerStateChecksum/VCFRONT_LVPowerStateCounter generated via generateMuxFrameCounterChecksum
  CAN_frame TESLA_221_DRIVE_Mux0 = {.FD = false,
                                    .ext_ID = false,
                                    .DLC = 8,
                                    .ID = 0x221,
                                    .data = {0x60, 0x55, 0x55, 0x15, 0x54, 0x51, 0xF1, 0xD8}};

  //0x221 VCFRONT_LVPowerState "Drive"
  //VCFRONT_vehiclePowerState VEHICLE_POWER_STATE_DRIVE (Mux1, Counter 0): "cycle_time" 50ms each mux/LVPowerStateIndex, VCFRONT_LVPowerStateChecksum/VCFRONT_LVPowerStateCounter generated via generateMuxFrameCounterChecksum
  CAN_frame TESLA_221_DRIVE_Mux1 = {.FD = false,
                                    .ext_ID = false,
                                    .DLC = 8,
                                    .ID = 0x221,
                                    .data = {0x61, 0x05, 0x55, 0x05, 0x00, 0x00, 0x00, 0xE3}};

  //0x221 VCFRONT_LVPowerState "Accessory"
  //VCFRONT_vehiclePowerState VEHICLE_POWER_STATE_ACCESSORY (Mux0, Counter 15): "cycle_time" 50ms each mux/LVPowerStateIndex, VCFRONT_LVPowerStateChecksum/VCFRONT_LVPowerStateCounter generated via generateMuxFrameCounterChecksum
  CAN_frame TESLA_221_ACCESSORY_Mux0 = {.FD = false,
                                        .ext_ID = false,
                                        .DLC = 8,
                                        .ID = 0x221,
                                        .data = {0x40, 0x55, 0x55, 0x05, 0x54, 0x51, 0xF5, 0xAC}};

  //0x221 VCFRONT_LVPowerState "Accessory"
  //VCFRONT_vehiclePowerState VEHICLE_POWER_STATE_ACCESSORY (Mux1, Counter 0): "cycle_time" 50ms each mux/LVPowerStateIndex, VCFRONT_LVPowerStateChecksum/VCFRONT_LVPowerStateCounter generated via generateMuxFrameCounterChecksum
  CAN_frame TESLA_221_ACCESSORY_Mux1 = {.FD = false,
                                        .ext_ID = false,
                                        .DLC = 8,
                                        .ID = 0x221,
                                        .data = {0x41, 0x05, 0x55, 0x55, 0x01, 0x00, 0x04, 0x18}};

  //0x221 VCFRONT_LVPowerState "Going Down"
  //VCFRONT_vehiclePowerState VEHICLE_POWER_STATE_OFF, key parts GOING_DOWN (Mux0, Counter 15): "cycle_time" 50ms each mux/LVPowerStateIndex, VCFRONT_LVPowerStateChecksum/VCFRONT_LVPowerStateCounter generated via generateMuxFrameCounterChecksum
  CAN_frame TESLA_221_GOING_DOWN_Mux0 = {.FD = false,
                                         .ext_ID = false,
                                         .DLC = 8,
                                         .ID = 0x221,
                                         .data = {0x00, 0x89, 0x55, 0x06, 0xA4, 0x51, 0xF1, 0xED}};

  //0x221 VCFRONT_LVPowerState "Going Down"
  //VCFRONT_vehiclePowerState VEHICLE_POWER_STATE_OFF, key parts GOING_DOWN (Mux1, Counter 0): "cycle_time" 50ms each mux/LVPowerStateIndex, VCFRONT_LVPowerStateChecksum/VCFRONT_LVPowerStateCounter generated via generateMuxFrameCounterChecksum
  CAN_frame TESLA_221_GOING_DOWN_Mux1 = {.FD = false,
                                         .ext_ID = false,
                                         .DLC = 8,
                                         .ID = 0x221,
                                         .data = {0x01, 0x09, 0x55, 0x59, 0x00, 0x00, 0x00, 0xDB}};

  //0x221 VCFRONT_LVPowerState "Off"
  //VCFRONT_vehiclePowerState VEHICLE_POWER_STATE_OFF, key parts OFF (Mux0, Counter 15): "cycle_time" 50ms each mux/LVPowerStateIndex, VCFRONT_LVPowerStateChecksum/VCFRONT_LVPowerStateCounter generated via generateMuxFrameCounterChecksum
  CAN_frame TESLA_221_OFF_Mux0 = {.FD = false,
                                  .ext_ID = false,
                                  .DLC = 8,
                                  .ID = 0x221,
                                  .data = {0x00, 0x01, 0x00, 0x00, 0x00, 0x50, 0xF1, 0x65}};

  //0x221 VCFRONT_LVPowerState "Off"
  //VCFRONT_vehiclePowerState VEHICLE_POWER_STATE_OFF, key parts OFF (Mux1, Counter 0): "cycle_time" 50ms each mux/LVPowerStateIndex, VCFRONT_LVPowerStateChecksum/VCFRONT_LVPowerStateCounter generated via generateMuxFrameCounterChecksum
  CAN_frame TESLA_221_OFF_Mux1 = {.FD = false,
                                  .ext_ID = false,
                                  .DLC = 8,
                                  .ID = 0x221,
                                  .data = {0x01, 0x01, 0x01, 0x50, 0x00, 0x00, 0x00, 0x76}};

  //0x229 SCCM_rightStalk: "cycle_time" 100ms, SCCM_rightStalkChecksum/SCCM_rightStalkCounter generated via dedicated generateTESLA_229 function for now
  //CRC seemingly related to AUTOSAR ID array... "autosarDataIds": [124,182,240,47,105,163,221,28,86,144,202,9,67,125,183,241] found in Model 3 firmware
  CAN_frame TESLA_229 = {.FD = false, .ext_ID = false, .DLC = 3, .ID = 0x229, .data = {0x46, 0x00, 0x00}};

  //0x241 VCFRONT_coolant: "cycle_time" 100ms
  static constexpr CAN_frame TESLA_241 = {.FD = false,
                                          .ext_ID = false,
                                          .DLC = 7,
                                          .ID = 0x241,
                                          .data = {0x35, 0x34, 0x0C, 0x0F, 0x8F, 0x55, 0x00}};

  //0x2D1 VCFRONT_okToUseHighPower: "cycle_time" 100ms
  static constexpr CAN_frame TESLA_2D1 = {.FD = false, .ext_ID = false, .DLC = 2, .ID = 0x2D1, .data = {0xFF, 0x01}};

  //0x2E1, 6 muxes
  //0x2E1 VCFRONT_status: "cycle_time" 10ms each mux/statusIndex
  static constexpr CAN_frame TESLA_2E1_VEHICLE_AND_RAILS = {.FD = false,
                                                            .ext_ID = false,
                                                            .DLC = 8,
                                                            .ID = 0x2E1,
                                                            .data = {0x29, 0x0A, 0x00, 0xFF, 0x0F, 0x00, 0x00, 0x00}};

  //{0x29, 0x0A, 0x00, 0xFF, 0x0F, 0x00, 0x00, 0x00} INIT
  //{0x29, 0x0A, 0x0D, 0xFF, 0x0F, 0x00, 0x00, 0x00} DRIVE
  //{0x29, 0x0A, 0x09, 0xFF, 0x0F, 0x00, 0x00, 0x00} HV_UP_STANDBY
  //{0x29, 0x0A, 0x0A, 0xFF, 0x0F, 0x00, 0x00, 0x00} ACCESSORY
  //{0x29, 0x0A, 0x06, 0xFF, 0x0F, 0x00, 0x00, 0x00} SLEEP_STANDBY

  //0x2E1 VCFRONT_status: "cycle_time" 10ms each mux/statusIndex
  static constexpr CAN_frame TESLA_2E1_HOMELINK = {.FD = false,
                                                   .ext_ID = false,
                                                   .DLC = 8,
                                                   .ID = 0x2E1,
                                                   .data = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00}};

  //0x2E1 VCFRONT_status: "cycle_time" 10ms each mux/statusIndex
  static constexpr CAN_frame TESLA_2E1_REFRIGERANT_SYSTEM = {.FD = false,
                                                             .ext_ID = false,
                                                             .DLC = 8,
                                                             .ID = 0x2E1,
                                                             .data = {0x03, 0x6D, 0x99, 0x02, 0x1B, 0x57, 0x00, 0x00}};

  //0x2E1 VCFRONT_status: "cycle_time" 10ms each mux/statusIndex
  static constexpr CAN_frame TESLA_2E1_LV_BATTERY_DEBUG = {.FD = false,
                                                           .ext_ID = false,
                                                           .DLC = 8,
                                                           .ID = 0x2E1,
                                                           .data = {0xFC, 0x1B, 0xD1, 0x99, 0x9A, 0xD8, 0x09, 0x00}};

  //0x2E1 VCFRONT_status: "cycle_time" 10ms each mux/statusIndex
  static constexpr CAN_frame TESLA_2E1_MUX_5 = {.FD = false,
                                                .ext_ID = false,
                                                .DLC = 8,
                                                .ID = 0x2E1,
                                                .data = {0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

  //0x2E1 VCFRONT_status: "cycle_time" 10ms each mux/statusIndex
  static constexpr CAN_frame TESLA_2E1_BODY_CONTROLS = {.FD = false,
                                                        .ext_ID = false,
                                                        .DLC = 8,
                                                        .ID = 0x2E1,
                                                        .data = {0x08, 0x21, 0x04, 0x6E, 0xA0, 0x88, 0x06, 0x04}};

  //0x2E8 EPBR_status: "cycle_time" 100ms
  CAN_frame TESLA_2E8 = {.FD = false,
                         .ext_ID = false,
                         .DLC = 8,
                         .ID = 0x2E8,
                         .data = {0x02, 0x00, 0x10, 0x00, 0x00, 0x80, 0x00, 0x6C}};

  //0x284 UI_vehicleModes: "cycle_time" 500ms
  static constexpr CAN_frame TESLA_284 = {.FD = false,
                                          .ext_ID = false,
                                          .DLC = 5,
                                          .ID = 0x284,
                                          .data = {0x10, 0x00, 0x00, 0x00, 0x00}};

  //0x293 UI_chassisControl: "cycle_time" 500ms, UI_chassisControlChecksum/UI_chassisControlCounter generated via generateFrameCounterChecksum
  CAN_frame TESLA_293 = {.FD = false,
                         .ext_ID = false,
                         .DLC = 8,
                         .ID = 0x293,
                         .data = {0x01, 0x0C, 0x55, 0x91, 0x55, 0x15, 0x01, 0xF3}};

  //0x3A1 VCFRONT_vehicleStatus: "cycle_time" 50ms, VCFRONT_vehicleStatusChecksum/VCFRONT_vehicleStatusCounter eventually need to be generated via generateMuxFrameCounterChecksum
  //Looks like 2 muxes, counter at bit 52 width 4 and checksum at bit 56 width 8? Need later software Model3_ETH.compact.json signal file or DBC.
  CAN_frame TESLA_3A1 = {.FD = false,
                         .ext_ID = false,
                         .DLC = 8,
                         .ID = 0x3A1,
                         .data = {0xC3, 0xFF, 0xFF, 0xFF, 0x3D, 0x00, 0xD0, 0x01}};
  uint8_t frame6_3A1[16] = {0xD0, 0xE2, 0xF0, 0x02, 0x10, 0x22, 0x30, 0x42,
                            0x50, 0x62, 0x70, 0x82, 0x90, 0xA2, 0xB0, 0xC2};
  uint8_t frame7_3A1[16] = {0x01, 0xCB, 0x21, 0xEB, 0x41, 0x0B, 0x61, 0x2B,
                            0x81, 0x4B, 0xA1, 0x6B, 0xC1, 0x8B, 0xE1, 0xAB};
  //0x313 UI_powertrainControl: "cycle_time" 500ms, UI_powertrainControlChecksum/UI_powertrainControlCounter generated via generateFrameCounterChecksum
  //Ref tesla-m3-pack-findings (fw 2019.20.4.2): CAN 0x313 = UI_trackModeSettings on that firmware; UI_powertrainControl is 0x334 there (name drift)
  CAN_frame TESLA_313 = {.FD = false,
                         .ext_ID = false,
                         .DLC = 8,
                         .ID = 0x313,
                         .data = {0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x1B}};

  //0x321 VCFRONT_sensors: "cycle_time" 1000ms
  CAN_frame TESLA_321 = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x321,
      .data = {0xEC, 0x71, 0xA7, 0x6E, 0x02, 0x6C, 0x00, 0x04}};  // Last 2 bytes are counter and checksum

  //0x333 UI_chargeRequest: "cycle_time" 500ms, UI_chargeTerminationPct value = 900 [bit 16, width 10, scale 0.1, min 25, max 100]
  //Ref tesla-m3-pack-findings (fw 2019.20.4.2): 0x333 UI_chargeRequest DLC 4 (this frame uses DLC 5; likely firmware drift)
  CAN_frame TESLA_333 = {.FD = false, .ext_ID = false, .DLC = 5, .ID = 0x333, .data = {0x84, 0x30, 0x84, 0x07, 0x02}};

  //0x334 UI request: "cycle_time" 500ms, initial frame car sends
  //Ref tesla-m3-pack-findings (fw 2019.20.4.2): CAN 0x334 = UI_powertrainControl on that firmware
  static constexpr CAN_frame TESLA_334_INITIAL = {.FD = false,
                                                  .ext_ID = false,
                                                  .DLC = 8,
                                                  .ID = 0x334,
                                                  .data = {0x3F, 0x3F, 0xC8, 0x00, 0xE2, 0x3F, 0x80, 0x1E}};

  //0x334 UI request: "cycle_time" 500ms, generated via generateFrameCounterChecksum
  CAN_frame TESLA_334 = {.FD = false,
                         .ext_ID = false,
                         .DLC = 8,
                         .ID = 0x334,
                         .data = {0x3F, 0x3F, 0x00, 0x0F, 0xE2, 0x3F, 0x90, 0x75}};

  //0x3B3 UI_vehicleControl2: "cycle_time" 500ms
  //Ref tesla-m3-pack-findings (fw 2019.20.4.2): 0x3B3 UI_vehicleControl2 DLC 2 (this frame uses DLC 8; likely firmware drift)
  static constexpr CAN_frame TESLA_3B3 = {.FD = false,
                                          .ext_ID = false,
                                          .DLC = 8,
                                          .ID = 0x3B3,
                                          .data = {0x90, 0x80, 0x05, 0x08, 0x00, 0x00, 0x00, 0x01}};

  //0x39D IBST_status: "cycle_time" 50ms, IBST_statusChecksum/IBST_statusCounter generated via generateFrameCounterChecksum
  CAN_frame TESLA_39D = {.FD = false, .ext_ID = false, .DLC = 5, .ID = 0x39D, .data = {0xE1, 0x59, 0xC1, 0x27, 0x00}};

  //0x3C2 VCLEFT_switchStatus (Mux0, initial frame car sends): "cycle_time" 50ms, sent once
  static constexpr CAN_frame TESLA_3C2_INITIAL = {.FD = false,
                                                  .ext_ID = false,
                                                  .DLC = 8,
                                                  .ID = 0x3C2,
                                                  .data = {0x00, 0x55, 0x55, 0x55, 0x00, 0x00, 0x5A, 0x05}};

  //0x3C2 VCLEFT_switchStatus (Mux0): "cycle_time" 50ms each mux/SwitchStatusIndex
  static constexpr CAN_frame TESLA_3C2_Mux0 = {.FD = false,
                                               .ext_ID = false,
                                               .DLC = 8,
                                               .ID = 0x3C2,
                                               .data = {0x00, 0x55, 0x55, 0x55, 0x00, 0x00, 0x5A, 0x45}};

  //0x3C2 VCLEFT_switchStatus (Mux1): "cycle_time" 50ms each mux/SwitchStatusIndex
  static constexpr CAN_frame TESLA_3C2_Mux1 = {.FD = false,
                                               .ext_ID = false,
                                               .DLC = 8,
                                               .ID = 0x3C2,
                                               .data = {0x29, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

  //0x55A Unknown but always sent: "cycle_time" 500ms
  static constexpr CAN_frame TESLA_55A = {.FD = false,
                                          .ext_ID = false,
                                          .DLC = 8,
                                          .ID = 0x55A,
                                          .data = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

  //0x7FF GTW_carConfig: "cycle_time" 100ms each mux/carConfigMultiplexer (UK/RHD)
  CAN_frame TESLA_7FF_Mux1 = {.FD = false,
                              .ext_ID = false,
                              .DLC = 8,
                              .ID = 0x7FF,
                              .data = {0x01, 0x49, 0x42, 0x47, 0x00, 0x03, 0x15, 0x01}};

  //0x7FF GTW_carConfig: "cycle_time" 100ms each mux/carConfigMultiplexer
  static constexpr CAN_frame TESLA_7FF_Mux2 = {.FD = false,
                                               .ext_ID = false,
                                               .DLC = 8,
                                               .ID = 0x7FF,
                                               .data = {0x02, 0x66, 0x32, 0x24, 0x04, 0x49, 0x95, 0x82}};

  //0x7FF GTW_carConfig: "cycle_time" 100ms each mux/carConfigMultiplexer (EU/Long Range)
  CAN_frame TESLA_7FF_Mux3 = {.FD = false,
                              .ext_ID = false,
                              .DLC = 8,
                              .ID = 0x7FF,
                              .data = {0x03, 0x01, 0x08, 0x48, 0x01, 0x00, 0x00, 0x12}};

  //0x7FF GTW_carConfig: "cycle_time" 100ms each mux/carConfigMultiplexer
  static constexpr CAN_frame TESLA_7FF_Mux4 = {.FD = false,
                                               .ext_ID = false,
                                               .DLC = 8,
                                               .ID = 0x7FF,
                                               .data = {0x04, 0x73, 0x03, 0x67, 0x5C, 0x00, 0x00, 0x00}};

  //0x7FF GTW_carConfig: "cycle_time" 100ms each mux/carConfigMultiplexer
  static constexpr CAN_frame TESLA_7FF_Mux5 = {.FD = false,
                                               .ext_ID = false,
                                               .DLC = 8,
                                               .ID = 0x7FF,
                                               .data = {0x05, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00}};

  //0x7FF GTW_carConfig: "cycle_time" 100ms each mux/carConfigMultiplexer - later firmware has muxes 6 & 7, needed?
  static constexpr CAN_frame TESLA_7FF_Mux6 = {.FD = false,
                                               .ext_ID = false,
                                               .DLC = 8,
                                               .ID = 0x7FF,
                                               .data = {0x06, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0xD0}};

  //0x7FF GTW_carConfig: "cycle_time" 100ms each mux/carConfigMultiplexer - later firmware has muxes 6 & 7, needed?
  static constexpr CAN_frame TESLA_7FF_Mux7 = {.FD = false,
                                               .ext_ID = false,
                                               .DLC = 8,
                                               .ID = 0x7FF,
                                               .data = {0x07, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00}};

  //0x722 BMS_bmbKeepAlive: "cycle_time" 100ms, should only be sent when testing packs or diagnosing problems
  static constexpr CAN_frame TESLA_722 = {.FD = false,
                                          .ext_ID = false,
                                          .DLC = 8,
                                          .ID = 0x722,
                                          .data = {0x02, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80}};

  //0x25D CP_status: "cycle_time" 100ms, stops some cpMia errors, but not necessary for standalone pack operation so not used/necessary. Note CP_type for different regions, the below has "IEC_CCS"
  static constexpr CAN_frame TESLA_25D = {.FD = false,
                                          .ext_ID = false,
                                          .DLC = 8,
                                          .ID = 0x25D,
                                          .data = {0x37, 0x41, 0x01, 0x16, 0x08, 0x00, 0x00, 0x00}};

  //0x602 BMS UDS diagnostic request: on demand
  CAN_frame TESLA_602 = {.FD = false,
                         .ext_ID = false,
                         .DLC = 8,
                         .ID = 0x602,
                         .data = {0x02, 0x27, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00}};  // Define initial UDS request

  //0x610 BMS Query UDS request: on demand
  //Ref tesla-m3-pack-findings (fw 2019.20.4.2): CAN 0x610 = UDS_hvpRequest (HVP). On that firmware the BMS/HVBMS UDS request is 0x602 (UDS_bmsRequest) -> response 0x612. Verify addressing against target firmware.
  static constexpr CAN_frame TESLA_610 = {
      .FD = false,
      .ext_ID = false,
      .DLC = 8,
      .ID = 0x610,
      .data = {0x02, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}};  // Define initial UDS request
  //1CF needed for Digital HVIL option
  CAN_frame TESLA_1CF_digital_hvil = {.FD = false,
                                      .ext_ID = false,
                                      .DLC = 8,
                                      .ID = 0x1CF,
                                      .data = {0x01, 0x00, 0x00, 0x1A, 0x1C, 0x02, 0x60, 0x69}};
  uint16_t content_1CF_digital_hvil[8] = {0x6069, 0x8089, 0xA0A9, 0xC0C9, 0xE0E9, 0x0009, 0x2029, 0x4049};
  uint8_t index_1CF = 0;
  uint8_t index_118 = 0;
  uint8_t contactor_counter = 0;
  uint8_t stateMachineClearIsolationFault = 0xFF;
  uint8_t stateMachineBMSReset = 0xFF;
  uint8_t stateMachineSOCReset = 0xFF;
  uint8_t stateMachineBMSQuery = 0xFF;
  uint16_t battery_cell_max_v = 3300;
  uint16_t battery_cell_min_v = 3300;
  bool cellvoltagesRead = false;
  //0x3d2: 978 BMS_kwhCounter
  uint32_t battery_total_discharge = 0;
  uint32_t battery_total_charge = 0;
  //0x352: 850 BMS_energyStatus
  bool BMS352_mux = false;                            // variable to store when 0x352 mux is present
  uint16_t battery_energy_buffer = 0;                 // kWh
  uint16_t battery_energy_buffer_m1 = 0;              // kWh
  uint16_t battery_energy_to_charge_complete = 0;     // kWh
  uint16_t battery_energy_to_charge_complete_m1 = 0;  // kWh
  uint16_t battery_expected_energy_remaining = 0;     // kWh
  uint16_t battery_expected_energy_remaining_m1 = 0;  // kWh
  bool battery_full_charge_complete = false;          // Changed to bool
  bool battery_fully_charged = false;                 // Changed to bool
  uint16_t battery_ideal_energy_remaining = 0;        // kWh
  uint16_t battery_ideal_energy_remaining_m0 = 0;     // kWh
  uint16_t battery_nominal_energy_remaining = 0;      // kWh
  uint16_t battery_nominal_energy_remaining_m0 = 0;   // kWh
  uint16_t battery_nominal_full_pack_energy = 0;      // Kwh
  uint16_t battery_nominal_full_pack_energy_m0 = 0;   // Kwh
  //0x132 306 HVBattAmpVolt
  uint16_t battery_volts = 0;                  // V
  int16_t battery_amps = 0;                    // A
  int16_t battery_raw_amps = 0;                // A
  uint16_t battery_charge_time_remaining = 0;  // Minutes
  //0x252 594 BMS_powerAvailable
  uint16_t BMS_maxRegenPower = 0;           //rename from battery_regenerative_limit
  uint16_t BMS_maxDischargePower = 0;       // rename from battery_discharge_limit
  uint16_t BMS_maxStationaryHeatPower = 0;  //rename from battery_max_heat_park
  uint16_t BMS_hvacPowerBudget = 0;         //rename from battery_hvac_max_power
  uint8_t BMS_notEnoughPowerForHeatPump = 0;
  uint8_t BMS_powerLimitState = 0;
  uint8_t BMS_inverterTQF = 0;
  //0x2d2: 722 BMSVAlimits
  uint16_t battery_max_discharge_current = 0;
  uint16_t battery_max_charge_current = 0;
  uint16_t BMS_max_voltage = 0;
  uint16_t BMS_min_voltage = 0;
  //0x2b4: 692 PCS_dcdcRailStatus
  uint16_t battery_dcdcHvBusVolt = 0;        // Change name from battery_high_voltage to battery_dcdcHvBusVolt
  uint16_t battery_dcdcLvBusVolt = 0;        // Change name from battery_low_voltage to battery_dcdcLvBusVolt
  uint16_t battery_dcdcLvOutputCurrent = 0;  // Change name from battery_output_current to battery_dcdcLvOutputCurrent
  //0x292: 658 BMS_socStatus
  uint16_t battery_beginning_of_life = 0;  // kWh
  uint16_t battery_soc_min = 0;
  uint16_t battery_soc_max = 0;
  uint16_t battery_soc_ui = 0;  //Change name from battery_soc_vi to reflect DBC battery_soc_ui
  uint16_t battery_soc_ave = 0;
  uint8_t battery_battTempPct = 0;
  //0x392: BMS_packConfig
  uint32_t battery_packMass = 0;
  uint32_t battery_platformMaxBusVoltage = 0;
  uint32_t battery_packConfigMultiplexer = 0;
  uint32_t battery_moduleType = 0;
  uint32_t battery_reservedConfig = 0;
  //0x332: 818 BattBrickMinMax:BMS_bmbMinMax
  int16_t battery_max_temp = 0;  // C*
  int16_t battery_min_temp = 0;  // C*
  uint16_t battery_BrickVoltageMax = 0;
  uint16_t battery_BrickVoltageMin = 0;
  uint8_t battery_BrickTempMaxNum = 0;
  uint8_t battery_BrickTempMinNum = 0;
  uint8_t battery_BrickModelTMax = 0;
  uint8_t battery_BrickModelTMin = 0;
  uint8_t battery_BrickVoltageMaxNum = 0;  //rename from battery_max_vno
  uint8_t battery_BrickVoltageMinNum = 0;  //rename from battery_min_vno
  //0x20A: 522 HVP_contactorState
  uint8_t battery_contactor = 0;  //State of contactor
  uint8_t battery_hvil_status = 0;
  uint8_t battery_packContNegativeState = 0;
  uint8_t battery_packContPositiveState = 0;
  uint8_t battery_packContactorSetState = 0;
  bool battery_packCtrsClosingBlocked = false;    // Change to bool
  bool battery_pyroTestInProgress = false;        // Change to bool
  bool battery_packCtrsOpenNowRequested = false;  // Change to bool
  bool battery_packCtrsOpenRequested = false;     // Change to bool
  uint8_t battery_packCtrsRequestStatus = 0;
  bool battery_packCtrsResetRequestRequired = false;  // Change to bool
  bool battery_dcLinkAllowedToEnergize = false;       // Change to bool
  bool battery_fcContNegativeAuxOpen = false;         // Change to bool
  uint8_t battery_fcContNegativeState = 0;
  bool battery_fcContPositiveAuxOpen = false;  // Change to bool
  uint8_t battery_fcContPositiveState = 0;
  uint8_t battery_fcContactorSetState = 0;
  bool battery_fcCtrsClosingAllowed = false;    // Change to bool
  bool battery_fcCtrsOpenNowRequested = false;  // Change to bool
  bool battery_fcCtrsOpenRequested = false;     // Change to bool
  uint8_t battery_fcCtrsRequestStatus = 0;
  bool battery_fcCtrsResetRequestRequired = false;  // Change to bool
  bool battery_fcLinkAllowedToEnergize = false;     // Change to bool
                                                    //0x72A: BMS_serialNumber
  uint8_t battery_serialNumber[14] = {0};           // Stores raw HEX values for ASCII chars
  bool parsed_battery_serialNumber = false;
  char* battery_manufactureDate = nullptr;  // YYYY-MM-DD\0, points at dayOfYearToDate's static buffer
                                            //Via UDS
  uint8_t battery_partNumber[12] = {0};     //stores raw HEX values for ASCII chars
  bool parsed_battery_partNumber = false;
  //Via UDS
  //static uint8_t BMS_partNumber[12] = {0};  //stores raw HEX values for ASCII chars
  //static bool parsed_BMS_partNumber = false;
  //0x300: BMS_info
  uint16_t BMS_info_buildConfigId = 0;
  uint16_t BMS_info_hardwareId = 0;
  uint16_t BMS_info_componentId = 0;
  uint8_t BMS_info_pcbaId = 0;
  uint8_t BMS_info_assemblyId = 0;
  uint16_t BMS_info_usageId = 0;
  uint16_t BMS_info_subUsageId = 0;
  uint8_t BMS_info_platformType = 0;
  uint32_t BMS_info_appCrc = 0;
  uint64_t BMS_info_bootGitHash = 0;
  uint8_t BMS_info_bootUdsProtoVersion = 0;
  uint32_t BMS_info_bootCrc = 0;
  //0x212: 530 BMS_status
  bool BMS_hvacPowerRequest = false;          //Change to bool
  bool BMS_notEnoughPowerForDrive = false;    //Change to bool
  bool BMS_notEnoughPowerForSupport = false;  //Change to bool
  bool BMS_preconditionAllowed = false;       //Change to bool
  bool BMS_updateAllowed = false;             //Change to bool
  bool BMS_activeHeatingWorthwhile = false;   //Change to bool
  bool BMS_cpMiaOnHvs = false;                //Change to bool
  uint8_t BMS_contactorState = 0;
  uint8_t BMS_state = 0;
  uint8_t BMS_hvState = 0;
  uint16_t BMS_isolationResistance = 0;
  bool BMS_chargeRequest = false;    //Change to bool
  bool BMS_keepWarmRequest = false;  //Change to bool
  uint8_t BMS_uiChargeStatus = 0;
  bool BMS_diLimpRequest = false;   //Change to bool
  bool BMS_okToShipByAir = false;   //Change to bool
  bool BMS_okToShipByLand = false;  //Change to bool
  uint32_t BMS_chgPowerAvailable = 0;
  uint8_t BMS_chargeRetryCount = 0;
  bool BMS_pcsPwmEnabled = false;        //Change to bool
  bool BMS_ecuLogUploadRequest = false;  //Change to bool
  uint8_t BMS_minPackTemperature = 0;
  // 0x224:548 PCS_dcdcStatus
  uint8_t PCS_dcdcPrechargeStatus = 0;
  uint8_t PCS_dcdc12VSupportStatus = 0;
  uint8_t PCS_dcdcHvBusDischargeStatus = 0;
  uint16_t PCS_dcdcMainState = 0;
  uint8_t PCS_dcdcSubState = 0;
  bool PCS_dcdcFaulted = false;          //Change to bool
  bool PCS_dcdcOutputIsLimited = false;  //Change to bool
  uint32_t PCS_dcdcMaxOutputCurrentAllowed = 0;
  uint8_t PCS_dcdcPrechargeRtyCnt = 0;
  uint8_t PCS_dcdc12VSupportRtyCnt = 0;
  uint8_t PCS_dcdcDischargeRtyCnt = 0;
  uint8_t PCS_dcdcPwmEnableLine = 0;
  uint8_t PCS_dcdcSupportingFixedLvTarget = 0;
  uint8_t PCS_ecuLogUploadRequest = 0;
  uint8_t PCS_dcdcPrechargeRestartCnt = 0;
  uint8_t PCS_dcdcInitialPrechargeSubState = 0;
  //0x312: 786 BMS_thermalStatus
  uint16_t BMS_powerDissipation = 0;
  uint16_t BMS_flowRequest = 0;
  uint16_t BMS_inletActiveCoolTargetT = 0;
  uint16_t BMS_inletPassiveTargetT = 0;
  uint16_t BMS_inletActiveHeatTargetT = 0;
  uint16_t BMS_packTMin = 0;
  uint16_t BMS_packTMax = 0;
  bool BMS_pcsNoFlowRequest = false;
  bool BMS_noFlowRequest = false;
  //0x3C4: PCS_info
  uint8_t PCS_partNumber[13] = {0};  //stores raw HEX values for ASCII chars
  bool parsed_PCS_partNumber = false;
  uint16_t PCS_info_buildConfigId = 0;
  uint16_t PCS_info_hardwareId = 0;
  uint16_t PCS_info_componentId = 0;
  uint8_t PCS_info_pcbaId = 0;
  uint8_t PCS_info_assemblyId = 0;
  uint16_t PCS_info_usageId = 0;
  uint16_t PCS_info_subUsageId = 0;
  uint8_t PCS_info_platformType = 0;
  uint32_t PCS_info_appCrc = 0;
  uint32_t PCS_info_cpu2AppCrc = 0;
  uint64_t PCS_info_bootGitHash = 0;
  uint8_t PCS_info_bootUdsProtoVersion = 0;
  uint32_t PCS_info_bootCrc = 0;
  //0x2A4; 676 PCS_thermalStatus
  int16_t PCS_chgPhATemp = 0;
  int16_t PCS_chgPhBTemp = 0;
  int16_t PCS_chgPhCTemp = 0;
  int16_t PCS_dcdcTemp = 0;
  int16_t PCS_ambientTemp = 0;
  //0x2C4; 708 PCS_logging
  uint16_t PCS_logMessageSelect = 0;
  uint16_t PCS_dcdcMaxLvOutputCurrent = 0;
  uint16_t PCS_dcdcCurrentLimit = 0;
  uint16_t PCS_dcdcLvOutputCurrentTempLimit = 0;
  uint16_t PCS_dcdcUnifiedCommand = 0;
  uint16_t PCS_dcdcCLAControllerOutput = 0;
  int16_t PCS_dcdcTankVoltage = 0;
  uint16_t PCS_dcdcTankVoltageTarget = 0;
  uint16_t PCS_dcdcClaCurrentFreq = 0;
  int16_t PCS_dcdcTCommMeasured = 0;
  uint16_t PCS_dcdcShortTimeUs = 0;
  uint16_t PCS_dcdcHalfPeriodUs = 0;
  uint16_t PCS_dcdcIntervalMaxFrequency = 0;
  uint16_t PCS_dcdcIntervalMaxHvBusVolt = 0;
  uint16_t PCS_dcdcIntervalMaxLvBusVolt = 0;
  uint16_t PCS_dcdcIntervalMaxLvOutputCurr = 0;
  uint16_t PCS_dcdcIntervalMinFrequency = 0;
  uint16_t PCS_dcdcIntervalMinHvBusVolt = 0;
  uint16_t PCS_dcdcIntervalMinLvBusVolt = 0;
  uint16_t PCS_dcdcIntervalMinLvOutputCurr = 0;
  uint32_t PCS_dcdc12vSupportLifetimekWh = 0;
  //0x7AA: //1962 HVP_debugMessage:
  uint8_t HVP_debugMessageMultiplexer = 0;
  bool HVP_gpioPassivePyroDepl = false;       //Change to bool
  bool HVP_gpioPyroIsoEn = false;             //Change to bool
  bool HVP_gpioCpFaultIn = false;             //Change to bool
  bool HVP_gpioPackContPowerEn = false;       //Change to bool
  bool HVP_gpioHvCablesOk = false;            //Change to bool
  bool HVP_gpioHvpSelfEnable = false;         //Change to bool
  bool HVP_gpioLed = false;                   //Change to bool
  bool HVP_gpioCrashSignal = false;           //Change to bool
  bool HVP_gpioShuntDataReady = false;        //Change to bool
  bool HVP_gpioFcContPosAux = false;          //Change to bool
  bool HVP_gpioFcContNegAux = false;          //Change to bool
  bool HVP_gpioBmsEout = false;               //Change to bool
  bool HVP_gpioCpFaultOut = false;            //Change to bool
  bool HVP_gpioPyroPor = false;               //Change to bool
  bool HVP_gpioShuntEn = false;               //Change to bool
  bool HVP_gpioHvpVerEn = false;              //Change to bool
  bool HVP_gpioPackCoontPosFlywheel = false;  //Change to bool
  bool HVP_gpioCpLatchEnable = false;         //Change to bool
  bool HVP_gpioPcsEnable = false;             //Change to bool
  bool HVP_gpioPcsDcdcPwmEnable = false;      //Change to bool
  bool HVP_gpioPcsChargePwmEnable = false;    //Change to bool
  bool HVP_gpioFcContPowerEnable = false;     //Change to bool
  bool HVP_gpioHvilEnable = false;            //Change to bool
  bool HVP_gpioSecDrdy = false;               //Change to bool
  uint16_t HVP_hvp1v5Ref = 0;
  int16_t HVP_shuntCurrentDebug = 0;
  bool HVP_packCurrentMia = false;           //Change to bool
  bool HVP_auxCurrentMia = false;            //Change to bool
  bool HVP_currentSenseMia = false;          //Change to bool
  bool HVP_shuntRefVoltageMismatch = false;  //Change to bool
  bool HVP_shuntThermistorMia = false;       //Change to bool
  bool HVP_shuntHwMia = false;               //Change to bool
  uint16_t HVP_info_buildConfigId = 0;
  uint16_t HVP_info_hardwareId = 0;
  uint16_t HVP_info_componentId = 0;
  uint8_t HVP_info_pcbaId = 0;
  uint8_t HVP_info_assemblyId = 0;
  uint16_t HVP_info_usageId = 0;
  uint16_t HVP_info_subUsageId = 0;
  uint8_t HVP_info_platformType = 0;
  uint32_t HVP_info_appCrc = 0;
  uint64_t HVP_info_bootGitHash = 0;
  uint8_t HVP_info_bootUdsProtoVersion = 0;
  uint32_t HVP_info_bootCrc = 0;
  int16_t HVP_dcLinkVoltage = 0;
  int16_t HVP_packVoltage = 0;
  int16_t HVP_fcLinkVoltage = 0;
  uint16_t HVP_packContVoltage = 0;
  int16_t HVP_packNegativeV = 0;
  int16_t HVP_packPositiveV = 0;
  uint16_t HVP_pyroAnalog = 0;
  int16_t HVP_dcLinkNegativeV = 0;
  int16_t HVP_dcLinkPositiveV = 0;
  int16_t HVP_fcLinkNegativeV = 0;
  uint16_t HVP_fcContCoilCurrent = 0;
  uint16_t HVP_fcContVoltage = 0;
  uint16_t HVP_hvilInVoltage = 0;
  uint16_t HVP_hvilOutVoltage = 0;
  int16_t HVP_fcLinkPositiveV = 0;
  uint16_t HVP_packContCoilCurrent = 0;
  uint16_t HVP_battery12V = 0;
  int16_t HVP_shuntRefVoltageDbg = 0;
  int16_t HVP_shuntAuxCurrentDbg = 0;
  int16_t HVP_shuntBarTempDbg = 0;
  int16_t HVP_shuntAsicTempDbg = 0;
  uint8_t HVP_shuntAuxCurrentStatus = 0;
  uint8_t HVP_shuntBarTempStatus = 0;
  uint8_t HVP_shuntAsicTempStatus = 0;
  //0x320: 800 BMS_alertMatrix
  uint8_t BMS_matrixIndex = 0;  // Changed to bool
  // Alerts below added from tesla-m3-pack-findings (firmware 2019.20.4.2)
  bool BMS_a001_Pack_Config_Mismatch = false;
  bool BMS_a055_SW_HvChain_Model_Fault = false;
  bool BMS_a126_SW_Thermistor_Failure = false;
  bool BMS_a135_HW_BMB_Diagnostics_Failure = false;
  bool BMS_a143_SW_CAC_Change = false;
  bool BMS_a155_SW_Weak_short_impedence = false;
  bool BMS_a173_SW_Charge_Component_Fault = false;
  bool BMS_a178_SW_Uncontrolled_Regen_PwrB = false;
  bool BMS_a061_robinBrickOverVoltage = false;
  bool BMS_a062_SW_BrickV_Imbalance = false;
  bool BMS_a063_SW_ChargePort_Fault = false;
  bool BMS_a064_SW_SOC_Imbalance = false;
  bool BMS_a127_SW_shunt_SNA = false;
  bool BMS_a128_SW_shunt_MIA = false;
  bool BMS_a069_SW_Low_Power = false;
  bool BMS_a130_IO_CAN_Error = false;
  bool BMS_a071_SW_SM_TransCon_Not_Met = false;
  bool BMS_a132_HW_BMB_OTP_Uncorrctbl = false;
  bool BMS_a134_SW_Delayed_Ctr_Off = false;
  bool BMS_a075_SW_Chg_Disable_Failure = false;
  bool BMS_a076_SW_Dch_While_Charging = false;
  bool BMS_a017_SW_Brick_OV = false;
  bool BMS_a018_SW_Brick_UV = false;
  bool BMS_a019_SW_Module_OT = false;
  bool BMS_a021_SW_Dr_Limits_Regulation = false;
  bool BMS_a022_SW_Over_Current = false;
  bool BMS_a023_SW_Stack_OV = false;
  bool BMS_a024_SW_Islanded_Brick = false;
  bool BMS_a025_SW_PwrBalance_Anomaly = false;
  bool BMS_a026_SW_HFCurrent_Anomaly = false;
  bool BMS_a087_SW_Feim_Test_Blocked = false;
  bool BMS_a088_SW_VcFront_MIA_InDrive = false;
  bool BMS_a089_SW_VcFront_MIA = false;
  bool BMS_a090_SW_Gateway_MIA = false;
  bool BMS_a091_SW_ChargePort_MIA = false;
  bool BMS_a092_SW_ChargePort_Mia_On_Hv = false;
  bool BMS_a034_SW_Passive_Isolation = false;
  bool BMS_a035_SW_Isolation = false;
  bool BMS_a036_SW_HvpHvilFault = false;
  bool BMS_a037_SW_Flood_Port_Open = false;
  bool BMS_a158_SW_HVP_HVI_Comms = false;
  bool BMS_a039_SW_DC_Link_Over_Voltage = false;
  bool BMS_a041_SW_Power_On_Reset = false;
  bool BMS_a042_SW_MPU_Error = false;
  bool BMS_a043_SW_Watch_Dog_Reset = false;
  bool BMS_a044_SW_Assertion = false;
  bool BMS_a045_SW_Exception = false;
  bool BMS_a046_SW_Task_Stack_Usage = false;
  bool BMS_a047_SW_Task_Stack_Overflow = false;
  bool BMS_a048_SW_Log_Upload_Request = false;
  bool BMS_a169_SW_FC_Pack_Weld = false;
  bool BMS_a050_SW_Brick_Voltage_MIA = false;
  bool BMS_a051_SW_HVC_Vref_Bad = false;
  bool BMS_a052_SW_PCS_MIA = false;
  bool BMS_a053_SW_ThermalModel_Sanity = false;
  bool BMS_a054_SW_Ver_Supply_Fault = false;
  bool BMS_a176_SW_GracefulPowerOff = false;
  bool BMS_a059_SW_Pack_Voltage_Sensing = false;
  bool BMS_a060_SW_Leakage_Test_Failure = false;
  bool BMS_a077_SW_Charger_Regulation = false;
  bool BMS_a081_SW_Ctr_Close_Blocked = false;
  bool BMS_a082_SW_Ctr_Force_Open = false;
  bool BMS_a083_SW_Ctr_Close_Failure = false;
  bool BMS_a084_SW_Sleep_Wake_Aborted = false;
  bool BMS_a094_SW_Drive_Inverter_MIA = false;
  bool BMS_a099_SW_BMB_Communication = false;
  bool BMS_a105_SW_One_Module_Tsense = false;
  bool BMS_a106_SW_All_Module_Tsense = false;
  bool BMS_a107_SW_Stack_Voltage_MIA = false;
  bool BMS_a121_SW_NVRAM_Config_Error = false;
  bool BMS_a122_SW_BMS_Therm_Irrational = false;
  bool BMS_a123_SW_Internal_Isolation = false;
  bool BMS_a129_SW_VSH_Failure = false;
  bool BMS_a131_Bleed_FET_Failure = false;
  bool BMS_a136_SW_Module_OT_Warning = false;
  bool BMS_a137_SW_Brick_UV_Warning = false;
  bool BMS_a138_SW_Brick_OV_Warning = false;
  bool BMS_a139_SW_DC_Link_V_Irrational = false;
  bool BMS_a141_SW_BMB_Status_Warning = false;
  bool BMS_a144_Hvp_Config_Mismatch = false;
  bool BMS_a145_SW_SOC_Change = false;
  bool BMS_a146_SW_Brick_Overdischarged = false;
  bool BMS_a149_SW_Missing_Config_Block = false;
  bool BMS_a151_SW_external_isolation = false;
  bool BMS_a156_SW_BMB_Vref_bad = false;
  bool BMS_a157_SW_HVP_HVS_Comms = false;
  bool BMS_a159_SW_HVP_ECU_Error = false;
  bool BMS_a161_SW_DI_Open_Request = false;
  bool BMS_a162_SW_No_Power_For_Support = false;
  bool BMS_a163_SW_Contactor_Mismatch = false;
  bool BMS_a164_SW_Uncontrolled_Regen = false;
  bool BMS_a165_SW_Pack_Partial_Weld = false;
  bool BMS_a166_SW_Pack_Full_Weld = false;
  bool BMS_a167_SW_FC_Partial_Weld = false;
  bool BMS_a168_SW_FC_Full_Weld = false;
  bool BMS_a170_SW_Limp_Mode = false;
  bool BMS_a171_SW_Stack_Voltage_Sense = false;
  bool BMS_a174_SW_Charge_Failure = false;
  bool BMS_a179_SW_Hvp_12V_Fault = false;
  bool BMS_a180_SW_ECU_reset_blocked = false;
  //0x3A4: PCS_alertMatrix — Tesla Model 3/Y, mapped from tesla-m3-pack-findings (fw 2019.20.4.2)
  // NOTE: findings dictionary (libQtCarCANData.so) lists alt names for a032=excessiveGridTransientsDetected,
  //       a047=bootloaderCrcMismatch, a048=softwareAssertion, a084=vDropFastInParasiticDiodeRegion.
  //       Names below follow the extracted bit-map table (dtc_matrices.json).
  bool PCS_a001_chgHwInputOc = false;
  bool PCS_a002_chgHwOutputOc = false;
  bool PCS_a003_chgHwInputOv = false;
  bool PCS_a004_chgHwIntBusOv = false;
  bool PCS_a005_chgOutputOv = false;
  bool PCS_a006_chgPrechargeFailedScr = false;
  bool PCS_a007_chgPhaseTempHot = false;
  bool PCS_a008_chgPhaseOverTemp = false;
  bool PCS_a009_chgPfcCurrentRegulation = false;
  bool PCS_a010_chgIntBusVRegulation = false;
  bool PCS_a011_chgLlcCurrentRegulation = false;
  bool PCS_a012_chgPfcIBandTracerFault = false;
  bool PCS_a013_chgPrechargeFailedBoost = false;
  bool PCS_a014_chgTempRationality = false;
  bool PCS_a015_chg12vUv = false;
  bool PCS_a016_chgAllPhasesFaulted = false;
  bool PCS_a017_chgWallPowerRemoval = false;
  bool PCS_a018_chgUnknownGridConfig = false;
  bool PCS_a019_acChargePowerLimited = false;
  bool PCS_a020_chgEnableLineMismatch = false;
  bool PCS_a021_hvpMia = false;
  bool PCS_a022_bmsMia = false;
  bool PCS_a023_cpMia = false;
  bool PCS_a024_vcfrontMia = false;
  bool PCS_a025_cpu2Malfunction = false;
  bool PCS_a026_watchdogAlarmed = false;
  bool PCS_a027_chgInsufficientCooling = false;
  bool PCS_a028_chgOutputUv = false;
  bool PCS_a029_chgPowerRationality = false;
  bool PCS_a030_canRationality = false;
  bool PCS_a031_uiMia = false;
  bool PCS_a032_gtwMia = false;
  bool PCS_a033_hvBusUv = false;
  bool PCS_a034_hvBusOv = false;
  bool PCS_a035_lvBusUv = false;
  bool PCS_a036_lvBusOv = false;
  bool PCS_a037_resonantTankOc = false;
  bool PCS_a038_claFaulted = false;
  bool PCS_a039_sdModuleClkFault = false;
  bool PCS_a040_dcdcMaxPowerReached = false;
  bool PCS_a041_dcdcOverTemp = false;
  bool PCS_a042_dcdcEnableLineMismatch = false;
  bool PCS_a043_hvBusPrechargeFailure = false;
  bool PCS_a044_12vSupportRegulation = false;
  bool PCS_a045_hvBusLowImpedance = false;
  bool PCS_a046_hvBusHighImpedence = false;
  bool PCS_a047_lvBusLowImpedance = false;
  bool PCS_a048_lvBusHighImpedance = false;
  bool PCS_a049_dcdcTempRationality = false;
  bool PCS_a050_dcdc12VsupportFaulted = false;
  bool PCS_a051_chgIntBusUv = false;
  bool PCS_a052_acVoltageNotPresent = false;
  bool PCS_a053_chgInputVDropHigh = false;
  bool PCS_a054_chgInputVDropTooHigh = false;
  bool PCS_a055_chgLineImedanceHigh = false;
  bool PCS_a056_chgLineImedanceTooHigh = false;
  bool PCS_a057_chgInputOverFreq = false;
  bool PCS_a058_chgInputUnderFreq = false;
  bool PCS_a059_chgInputOvRms = false;
  bool PCS_a060_chgInputOvPeak = false;
  bool PCS_a061_chgVLineRationality = false;
  bool PCS_a062_chgILineRationality = false;
  bool PCS_a063_chgVOutRationality = false;
  bool PCS_a064_chgIOutRationality = false;
  bool PCS_a065_chgPllNotLocked = false;
  bool PCS_a066_dcdcHvRationality = false;
  bool PCS_a067_dcdcLvRationality = false;
  bool PCS_a068_dcdcTankvRationality = false;
  bool PCS_a069_chgPfcLineDidt = false;
  bool PCS_a070_chgPfcLineDvdt = false;
  bool PCS_a071_chgPfcILoopRationality = false;
  bool PCS_a072_cpu2ClaStopped = false;
  bool PCS_a073_unexpectedAcInputVoltage = false;
  bool PCS_a074_hvBusDischargeFailure = false;
  bool PCS_a075_hvBusDischargeTimeout = false;
  bool PCS_a076_dcdcEnDeassertedErr = false;
  bool PCS_a077_microGridEnergyLow = false;
  bool PCS_a078_chgStopDcdcTooHot = false;
  bool PCS_a079_eepromOperationError = false;
  bool PCS_a080_damagedPhaseDetected = false;
  bool PCS_a081_dcdcPchgTimeout = false;
  bool PCS_a082_dcdcPchgUnsafeDiVoltage = false;
  bool PCS_a083_triggerOdin = false;
  bool PCS_a084_dcdcPchgStartVoltages = false;
  bool PCS_a085_dcdcFetsNotSwitching = false;
  bool PCS_a086_dcdcInsufficientCooling = false;
  bool PCS_a087_nvramRecordStatusError = false;
  bool PCS_a088_pchgParameters = false;
  bool PCS_a089_hvBusDischargeIrrational = false;
  bool PCS_a090_expectedAcVoltageSourceMissing = false;
  bool PCS_a091_chgIntBusRationality = false;
  bool PCS_a092_chgPowerLimitedByBusRipple = false;
  bool PCS_a093_powerRailRationality = false;
  bool PCS_a094_pcsDcdcNeedService = false;

  //0x31E: CP_alertMatrix — Tesla Model 3/Y, mapped from tesla-m3-pack-findings (fw 2019.20.4.2)
  bool CP_a001_canRx = false;
  bool CP_a002_canTx = false;
  bool CP_a003_canError = false;
  bool CP_a004_proximityRationality = false;
  bool CP_a005_gbdcLiveDisconnect = false;
  bool CP_a006_lostCommsBMS = false;
  bool CP_a007_watchdog = false;
  bool CP_a008_memoryError = false;
  bool CP_a009_coverOpen = false;
  bool CP_a010_pilotRationality = false;
  bool CP_a011_eeprom = false;
  bool CP_a012_ledDriver = false;
  bool CP_a013_lostCommsGTW = false;
  bool CP_a014_lostCommsCHG = false;
  bool CP_a015_apsVov = false;
  bool CP_a016_apsVuv = false;
  bool CP_a017_fiveVov = false;
  bool CP_a018_fiveVuv = false;
  bool CP_a019_threeVov = false;
  bool CP_a020_threeVuv = false;
  bool CP_a021_zeroVov = false;
  bool CP_a022_zeroVuv = false;
  bool CP_a023_gbdcSessionFailed = false;
  bool CP_a024_ledsUC = false;
  bool CP_a025_ledsOC = false;
  bool CP_a026_networkManagement = false;
  bool CP_a027_doorSensorOutOfSpec = false;
  bool CP_a028_insertEnableMismatch = false;
  bool CP_a029_doorClosedProxPilot = false;
  bool CP_a030_busOff = false;
  bool CP_a031_doorClosedCommandedOpen = false;
  bool CP_a032_doorOpenExpectedClosed = false;
  bool CP_a033_spiOpen = false;
  bool CP_a034_calibrationIncomplete = false;
  bool CP_a035_latchMovement_1 = false;
  bool CP_a036_latchNotDisengaged = false;
  bool CP_a037_latchNotEngaged = false;
  bool CP_a038_latchNotBlocking = false;
  bool CP_a039_latchMovement_2 = false;
  bool CP_a040_doNotUse = false;
  bool CP_a041_doorSensorUnplugged = false;
  bool CP_a042_doorAssemblyBroken = false;
  bool CP_a043_doorPotIrrational = false;
  bool CP_a044_lostCommsHVP = false;
  bool CP_a045_lostCommsVCSEC = false;
  bool CP_a046_lostCommsEVSE = false;
  bool CP_a047_lostCommsVCFRONT = false;
  bool CP_a048_lostCommsUI = false;
  bool CP_a049_multipleCablesDetected = false;
  bool CP_a050_latchNotConnected = false;
  bool CP_a051_doorInductiveSensorMIA = false;
  bool CP_a052_evseNotSupported = false;
  bool CP_a053_proxLatchedNoPilot = false;
  bool CP_a054_cableNotSecured = false;
  bool CP_a055_chargeStoppedNoPilot = false;
  bool CP_a056_proxDisconnected = false;
  bool CP_a057_evseFaulted = false;
  bool CP_a058_acChargingBlocked = false;
  bool CP_a059_swcanError = false;
  bool CP_a060_lostCommsPCS = false;
  bool CP_a061_uhfReceiverMIA = false;
  bool CP_a062_scOutOfService = false;
  bool CP_a063_scUpdateInProgress = false;
  bool CP_a064_superchargingBlocked = false;
  bool CP_a065_selfTestFailed = false;
  bool CP_a066_proxLatchedIdlePilot = false;
  bool CP_a067_gbdcConnFault = false;
  bool CP_a068_doorSensorMismatch = false;
  bool CP_a069_doorInductiveSensorError = false;
  bool CP_a070_doorInductiveSensorReset = false;
  bool CP_a071_exiDecodeFailure = false;
  bool CP_a072_v2gEvccTimeout = false;
  bool CP_a073_iecComboShutdown = false;
  bool CP_a074_failedToEstablishV2gComm = false;
  bool CP_a075_v2gCommsFailure = false;
  bool CP_a076_LDC1612errorWatchdog = false;
  bool CP_a077_invalidMacAddress = false;
  bool CP_a078_latchNotDisengagedCold = false;
  bool CP_a079_cableNotSecuredCold = false;
  bool CP_a080_taskStackOverflow = false;
  bool CP_a081_swException = false;
  bool CP_a082_powerOnReset = false;
  bool CP_a083_watchdogTraceData = false;
  bool CP_a084_proximityPeDisconnected = false;
  bool CP_a085_dcPinTempFaulted = false;
  bool CP_a086_dcPinTempIrrational = false;
  bool CP_a087_dcTempModelFault = false;
  bool CP_a088_dcTempModelDeviation = false;
  bool CP_a089_plcConfigMismatch = false;
  bool CP_a090_ccsEvseLowIso = false;
  bool CP_a091_wrongSuperchargerHandle = false;
  bool CP_a092_modemAppLoadFailed = false;
  bool CP_a093_modemLoadedWithReset = false;
  bool CP_a094_inductiveResetSuccessful = false;
  bool CP_a095_thermalDcLimitActive = false;
  bool CP_a096_pilotWake = false;
};

#endif
