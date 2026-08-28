#ifndef BATTERY_COMMAND_H
#define BATTERY_COMMAND_H

#include <cstdint>
#include <functional>
#include <vector>

#include "translatable_label.h"

// Wire values are the scaled integers the datalayer stores — the C6 has no FPU.
// `decimals` only tells the UI where the point goes; min/max are enforced by the
// dispatcher, so a value_action receives nothing outside them.
struct BatteryCommandValueSpec {
  const char* unit;
  int32_t min;
  int32_t max;
  uint8_t decimals;
};

// Presentation half of a command, shared by every driver that offers it so
// titles and prompts are defined once.
struct BatteryCommandDescriptor {
  const char* identifier;
  const char* title;
  const char* prompt;  // nullptr = no confirmation
  bool reload_after = false;
};

// A distinct type, so binding an action of the wrong arity is a compile error.
struct BatteryValueCommandDescriptor : BatteryCommandDescriptor {
  BatteryCommandValueSpec value;
};

// A command a specific battery offers. `available` is evaluated per request:
// some conditions are learned from the bus at runtime, others track state.
struct BatteryCommand {
  const BatteryCommandDescriptor* descriptor;
  const BatteryCommandValueSpec* value;  // null = takes no value
  std::function<void()> action;
  std::function<void(int32_t)> value_action;
  std::function<bool()> available;  // null = always available
};

inline BatteryCommand command(const BatteryCommandDescriptor& descriptor, std::function<void()> action,
                              std::function<bool()> available = nullptr) {
  return {&descriptor, nullptr, std::move(action), nullptr, std::move(available)};
}

inline BatteryCommand value_command(const BatteryValueCommandDescriptor& descriptor,
                                    std::function<void(int32_t)> action, std::function<bool()> available = nullptr) {
  return {&descriptor, &descriptor.value, nullptr, std::move(action), std::move(available)};
}

// A value descriptor would otherwise bind here through its base and lose its value.
BatteryCommand command(const BatteryValueCommandDescriptor&, std::function<void()>,
                       std::function<bool()> = nullptr) = delete;

// The address is stored, so the descriptor must outlive every command bound to it.
// These overloads reject temporaries; a shorter-lived named descriptor still compiles,
// so bind only the CMD_* constants below.
BatteryCommand command(BatteryCommandDescriptor&&, std::function<void()>, std::function<bool()> = nullptr) = delete;
BatteryCommand value_command(BatteryValueCommandDescriptor&&, std::function<void(int32_t)>,
                             std::function<bool()> = nullptr) = delete;

inline constexpr BatteryCommandDescriptor CMD_CLEAR_ISOLATION{"clearIsolation", TL("Clear isolation fault"),
                                                              TL("clear any active isolation fault?")};
inline constexpr BatteryCommandDescriptor CMD_CALIBRATE_SOC{
    "calibrateSOC", TL("Calibrate SOC"), TL("calibrate SOC? Note this will calibrate BMS according to set targets")};
inline constexpr BatteryCommandDescriptor CMD_CHADEMO_RESTART{"chademoRestart", TL("Restart"),
                                                              TL("restart the V2X session?")};
inline constexpr BatteryCommandDescriptor CMD_CHADEMO_STOP{"chademoStop", TL("Stop"), TL("stop V2X?")};
inline constexpr BatteryCommandDescriptor CMD_RESET_BMS{"resetBMS", TL("BMS Reset"), TL("Reset the BMS?")};
inline constexpr BatteryCommandDescriptor CMD_RESET_SOC{"resetSOC", TL("SOC Reset"), TL("Reset SOC?")};
inline constexpr BatteryCommandDescriptor CMD_RESET_CRASH{
    "resetCrash", TL("Unlock crashed BMS"),
    TL("reset crash data? Note this will unlock your BMS and enable contactor closing and SOC calculation.")};
inline constexpr BatteryCommandDescriptor CMD_RESET_NVROL{
    "resetNVROL", TL("Perform NVROL reset"),
    TL("trigger an NVROL reset? Battery will be unavailable for 30 seconds while this is active!")};
inline constexpr BatteryCommandDescriptor CMD_RESET_CONTACTOR{"resetContactor", TL("Perform contactor reset"),
                                                              TL("reset contactors?")};
inline constexpr BatteryCommandDescriptor CMD_RESET_DTC{"resetDTC", TL("Erase DTC"), TL("erase DTCs?"), true};
inline constexpr BatteryCommandDescriptor CMD_START_BALANCING{
    "startBalancing", TL("Balancing"),
    TL("continue? Please charge battery fully for this to work. After a couple of minutes, battery will sleep and do "
       "balancing. It often takes many hours. There will be no progress indication.")};
inline constexpr BatteryCommandDescriptor CMD_END_BALANCING{"endBalancing", TL("Stop Balancing Mode"),
                                                            TL("end offline balancing?")};
inline constexpr BatteryCommandDescriptor CMD_START_BALANCING_REQUEST{
    "startBalancingRequest", TL("Start Balancing"), TL("request the BMS to start cell balancing?")};
inline constexpr BatteryCommandDescriptor CMD_STOP_BALANCING_REQUEST{
    "stopBalancingRequest", TL("Stop Balancing"), TL("request the BMS to stop cell balancing?")};
inline constexpr BatteryCommandDescriptor CMD_ISOLATION_TEST{"isolationTest", TL("Isolation Test"),
                                                             TL("start an isolation test?")};
inline constexpr BatteryCommandDescriptor CMD_ISO_MONITOR_ENABLE{"isoMonitorEnable", TL("Enable monitoring"),
                                                                 TL("enable the isolation resistance monitor?")};
inline constexpr BatteryCommandDescriptor CMD_ISO_MONITOR_DISABLE{"isoMonitorDisable", TL("Disable monitoring"),
                                                                  TL("disable the isolation resistance monitor?")};
inline constexpr BatteryCommandDescriptor CMD_READ_DTC{"readDTC", TL("Read DTC"), nullptr, true};
inline constexpr BatteryCommandDescriptor CMD_READ_CELL_BALANCE_TIMES{
    "readCellBalanceTimes", TL("Read cell balance timers"),
    TL("read every cell's lifetime balancing hours? The scan queries one cell at a time and takes about half a "
       "minute.")};
inline constexpr BatteryCommandDescriptor CMD_RESET_BECM{"resetBECM", TL("Restart BECM module"), TL("restart BECM??")};
inline constexpr BatteryCommandDescriptor CMD_CONTACTOR_CLOSE{"contactorClose", TL("Close Contactors"),
                                                              TL("a contactor close request?")};
inline constexpr BatteryCommandDescriptor CMD_CONTACTOR_OPEN{"contactorOpen", TL("Open Contactors"),
                                                             TL("a contactor open request?")};
inline constexpr BatteryCommandDescriptor CMD_RESET_SOH{"resetSOH", TL("Reset degradation data"),
                                                         TL("reset degradation data?")};
inline constexpr BatteryCommandDescriptor CMD_SET_FACTORY_MODE{
    "setFactoryMode", TL("Set Factory Mode"), TL("set factory mode and disable isolation measurement?")};
inline constexpr BatteryCommandDescriptor CMD_RESET_ENERGY_SAVING_MODE{
    "resetEnergySavingMode", TL("Reset Energy Saving Mode"), TL("reset energy saving mode to normal?")};

inline constexpr int32_t FAKE_VOLTAGE_MIN_DV = 0;
inline constexpr int32_t FAKE_VOLTAGE_MAX_DV = UINT16_MAX;
inline constexpr uint8_t DECI_UNIT_DECIMALS = 1;
inline constexpr BatteryValueCommandDescriptor CMD_SET_FAKE_VOLTAGE{
    {"setFakeVoltage", TL("Set Voltage"), nullptr, false},
    {"V", FAKE_VOLTAGE_MIN_DV, FAKE_VOLTAGE_MAX_DV, DECI_UNIT_DECIMALS}};

#endif
