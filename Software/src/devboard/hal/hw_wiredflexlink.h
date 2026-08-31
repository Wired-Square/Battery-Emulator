#ifndef __HW_WIREDFLEXLINK_H__
#define __HW_WIREDFLEXLINK_H__

#include <SPI.h>
#include <Wire.h>
#include <cmath>

#include "../../communication/can/NativeTwai.h"
#include "../../communication/rs485/Rs485Port.h"
#include "../../lib/tca9538/tca9538.h"
#include "../../lib/vn9dxxxxxx/vn9dxxxxxx.h"
#include "hal.h"

// WiredFlexLink v0.0.2 (ESP32-C6-WROOM-1-N8).

// TCA9538 IO expander on I2C0 @ 0x20: transceiver standby, bus termination
// and the VN9D DI pins.
static constexpr uint8_t kWflExpanderAddress = 0x20;
static constexpr gpio_num_t kWflI2cSdaPin = GPIO_NUM_14;
static constexpr gpio_num_t kWflI2cSclPin = GPIO_NUM_15;

// Expander port assignments (P0..P7).
static constexpr uint8_t kWflExpSwDi0 = 0;
static constexpr uint8_t kWflExpSwDi1 = 1;
static constexpr uint8_t kWflExpTermCan0 = 2;
static constexpr uint8_t kWflExpTermCan1 = 3;
static constexpr uint8_t kWflExpTermRs0 = 4;
static constexpr uint8_t kWflExpTermRs1 = 5;
static constexpr uint8_t kWflExpStbyCan0 = 6;
static constexpr uint8_t kWflExpStbyCan1 = 7;

// All pins are outputs. DI0/DI1 are driven low so the VN9D direct inputs
// can never switch a channel — the SPI SOCR path is the only control.
static constexpr uint8_t kWflExpanderConfigMask = 0;
// MCP2562FD standby is active-high, so a low leaves the transceivers on.
static constexpr uint8_t kWflExpanderOutputInit = 0;

// WS2812 chain order: 0 status, 1-4 SW0-SW3, 5 CAN0, 6 CAN1, 7 RS0, 8 RS1.
static constexpr uint16_t kWflLedCount = 9;
static constexpr uint16_t kWflLedStatus = 0;
static constexpr int kWflLedCan0 = 5;
static constexpr int kWflLedCan1 = 6;
static constexpr int kWflLedRs0 = 7;
static constexpr int kWflLedRs1 = 8;

static constexpr uint8_t kWflCan1LogId = CAN_LOG_ID_BOARD_BASE;

inline NativeTwai wfl_can0_bus(CAN_LOG_ID_NATIVE, TwaiController::Twai0, {GPIO_NUM_23, GPIO_NUM_22, GPIO_NUM_NC});
inline NativeTwai wfl_can1_bus(kWflCan1LogId, TwaiController::Twai1, {GPIO_NUM_21, GPIO_NUM_20, GPIO_NUM_NC});
inline Rs485Port wfl_rs0_port(Serial0, UART_NUM_0, {GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_3});
inline Rs485Port wfl_rs1_port(Serial1, UART_NUM_1, {GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_4});

// VN9D5D20FN load switch on SPI2/FSPI; PWM engine clock from LEDC on GPIO0.
static constexpr uint8_t kWflVn9dSpiBus = FSPI;
static constexpr gpio_num_t kWflVn9dSckPin = GPIO_NUM_6;
static constexpr gpio_num_t kWflVn9dMisoPin = GPIO_NUM_2;
static constexpr gpio_num_t kWflVn9dMosiPin = GPIO_NUM_7;
static constexpr gpio_num_t kWflVn9dCsPin = GPIO_NUM_5;
static constexpr gpio_num_t kWflVn9dPwmClkPin = GPIO_NUM_0;
static constexpr uint8_t kWflVn9dLedcChannel = 0;

inline Vn9d wfl_load_switch(kWflVn9dSpiBus, {kWflVn9dSckPin, kWflVn9dMisoPin, kWflVn9dMosiPin, kWflVn9dCsPin},
                            kWflVn9dPwmClkPin, kWflVn9dLedcChannel);
inline Vn9dOutput wfl_load_switch_outputs[kLoadSwitchConfigChannels] = {{wfl_load_switch, 0},
                                                                        {wfl_load_switch, 1},
                                                                        {wfl_load_switch, 2},
                                                                        {wfl_load_switch, 3}};

// SW0-SW3 state LEDs (chain indices 1-4).
static constexpr int kWflSwitchLeds[kLoadSwitchConfigChannels] = {1, 2, 3, 4};

static constexpr ScopeEntry kWflSwitchChannels[kLoadSwitchConfigChannels] = {
    {0, "SW0"}, {1, "SW1"}, {2, "SW2"}, {3, "SW3"}};

// The last request, not the applied state: the tick may not have run yet, and
// write_status() is where the output's real level travels.
inline bool wfl_manual_request[kLoadSwitchConfigChannels] = {};

inline constexpr uint8_t kWflDutyPercentMax = 100;

// Every VN9D SPI access runs in the core-loop task; the resulting 40 ms cadence
// sits inside the chip's 70 ms watchdog ceiling with margin for slot jitter.
// Raising this drops the chip into fail-safe and opens every output.
inline constexpr int kWflLoadSwitchTickDivider = 4;

inline constexpr DeviceSetting kWiredFlexLinkSettings[] = {
    board_row(setting("TERMIF", SettingType::Bool, kHardware, SettingApplies::Live)
                  .bound({SettingRam::None, nullptr, 1, SettingScope::Interface,
                          [](uint8_t index, double value) {
                            esp32hal->set_interface_termination(index, value != 0.0);
                          }}),
              "Termination", [](uint8_t index) { return esp32hal->supports_interface_termination(index); }),

    board_row(setting("LSROLE", SettingType::EnumUint, kHardware, SettingApplies::Boot,
                      (int32_t)LoadSwitchRole::Disabled)
                  .with_options("loadswitchrole")
                  .with_range(0, (int32_t)LoadSwitchRole::Highest - 1)
                  .bound({SettingRam::None, nullptr, 1, SettingScope::LoadSwitchChannel,
                          [](uint8_t index, double value) {
                            wfl_load_switch.set_channel_role(index, (LoadSwitchRole)value);
                          }}),
              "Role"),

    board_row(setting("LSDUTY", SettingType::Float, kHardware, SettingApplies::Live, kWflDutyPercentMax)
                  .with_range(0, kWflDutyPercentMax)
                  .bound({SettingRam::None, nullptr, 1, SettingScope::LoadSwitchChannel,
                          [](uint8_t index, double value) {
                            wfl_load_switch.request_duty(
                                index, (uint16_t)std::lround(value * kLoadSwitchDutyMax / kWflDutyPercentMax));
                          }}),
              "Steady-state duty (%)"),

    board_row(setting("LSDIV", SettingType::EnumUint, kHardware, SettingApplies::Live)
                  .with_options("loadswitchdivisor")
                  .with_range(0, kLoadSwitchDivisorCodes - 1)
                  .bound({SettingRam::None, nullptr, 1, SettingScope::LoadSwitchChannel,
                          [](uint8_t index, double value) {
                            wfl_load_switch.request_divisor(index, (uint8_t)value);
                          }}),
              "PWM divisor"),

    board_row(setting("LSMANUAL", SettingType::Bool, kLive, SettingApplies::Live)
                  .volatile_storage()
                  .in_section(kSecLoadSwitch)
                  .bound({SettingRam::Bool, [](uint8_t index) -> void* { return &wfl_manual_request[index]; }, 1,
                          SettingScope::LoadSwitchChannel,
                          [](uint8_t index, double value) { wfl_load_switch.request_manual(index, value != 0.0); }}),
              "Manual output",
              [](uint8_t index) { return wfl_load_switch.channel_role(index) == LoadSwitchRole::Manual; }),
};
static_assert(fields_valid(kWiredFlexLinkSettings), "a board setting key is invalid, or its range is inverted");

inline constexpr InterfaceDescriptor kWiredFlexLinkInterfaces[] = {
    {InterfaceType::CanNative, "CAN0", comm_interface::CanNative, &wfl_can0_bus},
    {InterfaceType::CanNative, "CAN1", comm_interface::Highest, &wfl_can1_bus},
    {InterfaceType::Rs485, "RS0", comm_interface::Highest, nullptr, &wfl_rs0_port},
    {InterfaceType::Rs485, "RS1", comm_interface::Highest, nullptr, &wfl_rs1_port},
};
static_assert(has_type(make_interface_list(kWiredFlexLinkInterfaces), InterfaceType::CanNative),
              "every board table needs a native CAN descriptor");

// Index-aligned with kWiredFlexLinkInterfaces: expander pin that terminates
// each interface's bus.
static constexpr uint8_t kWflTerminationPins[] = {kWflExpTermCan0, kWflExpTermCan1, kWflExpTermRs0, kWflExpTermRs1};
static_assert(sizeof(kWflTerminationPins) / sizeof(kWflTerminationPins[0]) <=
                  make_interface_list(kWiredFlexLinkInterfaces).count,
              "termination map cannot exceed the descriptor table");

// Index-aligned with kWiredFlexLinkInterfaces: traffic LED for each interface.
static constexpr int kWflActivityLeds[] = {kWflLedCan0, kWflLedCan1, kWflLedRs0, kWflLedRs1};
static_assert(sizeof(kWflActivityLeds) / sizeof(kWflActivityLeds[0]) <=
                  make_interface_list(kWiredFlexLinkInterfaces).count,
              "LED activity map cannot exceed the descriptor table");

class WiredFlexLinkHal : public Esp32Hal {
 public:
  const char* name() { return "WiredFlexLink v0.0.2"; }

  // Single-core SoC: everything runs on core 0.
  virtual int CORE_FUNCTION_CORE() { return 0; }

  // Data pin of the 9-pixel WS2812 chain.
  virtual gpio_num_t LED_PIN() { return GPIO_NUM_9; }
  virtual uint16_t LED_COUNT() { return kWflLedCount; }
  virtual uint16_t LED_STATUS_INDEX() { return kWflLedStatus; }
  virtual int LED_INTERFACE_ACTIVITY_INDEX(size_t interface_index) {
    return interface_index < sizeof(kWflActivityLeds) / sizeof(kWflActivityLeds[0]) ? kWflActivityLeds[interface_index]
                                                                                    : -1;
  }

  // No runtime inputs on this board; GPIO0 is the load-switch PWM clock.
  virtual gpio_num_t BOOT_BUTTON_PIN() { return GPIO_NUM_NC; }

  virtual void board_init() {
    if (!alloc_pins("I2C", kWflI2cSdaPin, kWflI2cSclPin)) {
      return;
    }
    Wire.begin(kWflI2cSdaPin, kWflI2cSclPin);
    if (!expander_.begin(kWflExpanderConfigMask, (uint8_t)(kWflExpanderOutputInit | termination_mask_))) {
      set_event(EVENT_IOEXPANDER_INIT_FAILURE, 0);
      // With the expander down the DI pin state is unknown; leave the VN9D
      // in fail-safe (all outputs open) rather than enabling it.
      set_event(EVENT_LOAD_SWITCH_INIT_FAILURE, 0);
      return;
    }
    expander_up_ = true;
    if (wfl_load_switch.init()) {
      bind_switched_outputs();
    }
  }

  virtual void board_tick() {
    if (++load_switch_slot_ < kWflLoadSwitchTickDivider) {
      return;
    }
    load_switch_slot_ = 0;
    wfl_load_switch.tick();
  }

  virtual DeviceSettingList settings() { return device_settings(kWiredFlexLinkSettings); }

  virtual ScopeEntries scope_entries(SettingScope scope) {
    if (scope == SettingScope::LoadSwitchChannel) {
      return {kWflSwitchChannels, kLoadSwitchConfigChannels};
    }
    return Esp32Hal::scope_entries(scope);
  }

  virtual void write_status(ResponseWriter& out) {
    const LoadSwitchStatus& ls_status = wfl_load_switch.status();
    out.begin_object("load_switch");
    out.field("device_ok", ls_status.device_ok);
    out.begin_array("channels");
    // Every channel is emitted, disabled ones included: the client addresses a
    // channel by its array index when toggling.
    for (uint8_t ch = 0; ch < ls_status.channel_count; ch++) {
      const LoadSwitchChannelStatus& channel = ls_status.channels[ch];
      out.begin_object();
      out.field("role_id", static_cast<uint32_t>(channel.role));
      out.field("role", name_for_load_switch_role(channel.role));
      out.field("manual", channel.role == LoadSwitchRole::Manual);
      out.field("on", channel.on);
      out.field("pending", channel.pending);
      out.field("pending_on", channel.pending_on);
      out.field("current_mA", channel.current_mA);
      out.field("fault", channel.fault || channel.latched_off);
      out.end_object();
    }
    out.end_array();
    out.end_object();
  }

  virtual SwitchedOutputList switched_outputs() { return {output_bindings_, output_binding_count_}; }

  virtual LoadSwitch* load_switch() { return &wfl_load_switch; }

  virtual int LED_SWITCHED_OUTPUT_INDEX(size_t channel) {
    return channel < kLoadSwitchConfigChannels ? kWflSwitchLeds[channel] : -1;
  }

  virtual bool supports_interface_termination(size_t interface_index) {
    return interface_index < sizeof(kWflTerminationPins) / sizeof(kWflTerminationPins[0]);
  }

  // Stored settings apply before board_init(), so banking into the mask that
  // seeds the expander's first write is what makes them stick.
  virtual bool set_interface_termination(size_t interface_index, bool enabled) {
    if (!supports_interface_termination(interface_index)) {
      return false;
    }
    const uint8_t bit = (uint8_t)(1u << kWflTerminationPins[interface_index]);
    if (enabled) {
      termination_mask_ |= bit;
    } else {
      termination_mask_ &= (uint8_t)~bit;
    }
    return expander_up_ ? expander_.write_pin(kWflTerminationPins[interface_index], enabled) : true;
  }

  virtual bool interface_termination(size_t interface_index) {
    return supports_interface_termination(interface_index) &&
           (termination_mask_ & (uint8_t)(1u << kWflTerminationPins[interface_index])) != 0;
  }

  InterfaceList interfaces() { return make_interface_list(kWiredFlexLinkInterfaces); }

 private:
  SwitchedOutputBinding output_bindings_[kLoadSwitchConfigChannels] = {};
  size_t output_binding_count_ = 0;

  // Roles apply at the next boot, so the binding table is boot-stable like
  // every other board table.
  void bind_switched_outputs() {
    for (uint8_t ch = 0; ch < kLoadSwitchConfigChannels; ch++) {
      OutputRole role;
      switch (wfl_load_switch.channel_role(ch)) {
        case LoadSwitchRole::PositiveContactor:
          role = OutputRole::PositiveContactor;
          break;
        case LoadSwitchRole::NegativeContactor:
          role = OutputRole::NegativeContactor;
          break;
        case LoadSwitchRole::Precharge:
          role = OutputRole::Precharge;
          break;
        case LoadSwitchRole::BmsPower:
          role = OutputRole::BmsPower;
          break;
        default:
          continue;  // Disabled and Manual channels are module-owned
      }
      output_bindings_[output_binding_count_++] = {role, &wfl_load_switch_outputs[ch]};
    }
  }

  Tca9538 expander_{Wire, kWflExpanderAddress};
  int load_switch_slot_ = 0;
  uint8_t termination_mask_ = 0;
  bool expander_up_ = false;
};

/* ----- Error checks below, don't change (can't be moved to separate file) ----- */
#ifndef HW_CONFIGURED
#define HW_CONFIGURED
#else
#error Multiple HW defined! Please select a single HW
#endif

#endif
