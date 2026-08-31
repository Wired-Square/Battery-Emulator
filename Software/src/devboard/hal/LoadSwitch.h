#ifndef _LOAD_SWITCH_H_
#define _LOAD_SWITCH_H_

#include <stdint.h>

// Channel roles stored in LSROLE<n>; persisted values, append-only.
enum class LoadSwitchRole : uint8_t {
  Disabled = 0,
  PositiveContactor = 1,
  NegativeContactor = 2,
  Precharge = 3,
  BmsPower = 4,
  Manual = 5,
  Highest
};

inline constexpr uint8_t kLoadSwitchMaxChannels = 6;    // VN9D30Q100F
inline constexpr uint8_t kLoadSwitchConfigChannels = 4; // LSROLE0..3 (VN9D5D20FN)
inline constexpr uint16_t kLoadSwitchDutyMax = 1023;
inline constexpr uint8_t kLoadSwitchDivisorCodes = 4;
// Code n selects ratio base<<n: ÷512/1024/2048/4096.
inline constexpr uint16_t kLoadSwitchDivisorBase = 512;

constexpr uint32_t load_switch_divisor_ratio(uint8_t code) {
  return static_cast<uint32_t>(kLoadSwitchDivisorBase) << code;
}

constexpr uint32_t load_switch_pwm_frequency_hz(uint32_t clock_hz, uint8_t code) {
  return (clock_hz + load_switch_divisor_ratio(code) / 2) / load_switch_divisor_ratio(code);
}

constexpr const char* name_for_load_switch_role(LoadSwitchRole role) {
  switch (role) {
    case LoadSwitchRole::Disabled:
      return "Disabled";
    case LoadSwitchRole::PositiveContactor:
      return "Positive contactor";
    case LoadSwitchRole::NegativeContactor:
      return "Negative contactor";
    case LoadSwitchRole::Precharge:
      return "Precharge";
    case LoadSwitchRole::BmsPower:
      return "BMS power";
    case LoadSwitchRole::Manual:
      return "Manual";
    case LoadSwitchRole::Highest:
      break;
  }
  return "";
}

struct LoadSwitchChannelStatus {
  LoadSwitchRole role;
  bool on;           // commanded on (SOCR bit)
  bool pending;      // a manual request is queued for the tick, not yet applied
  bool pending_on;   // the requested value, meaningful only while pending
  uint16_t duty;
  uint32_t current_mA;
  bool fault;        // CHFBSR: VDS/power-limit/thermal aggregate
  bool latched_off;  // CHLOFFSR
  bool open_load;    // STKFLTR or OLPUSR: open-load / stuck to VCC
};

struct LoadSwitchStatus {
  bool device_ok;
  // VCC undervoltage: the analog side (current sense, frame temperature)
  // is unpowered, so those readings are meaningless while this is set.
  bool vcc_undervoltage;
  uint8_t channel_count;
  int16_t frame_temperature_dC;  // 0.1 °C
  uint8_t gsb;                   // last global status byte
  LoadSwitchChannelStatus channels[kLoadSwitchMaxChannels];
};

// Generic base only (the CanBus.h principle): shared code reaches the board's
// load switch through this seam, so an image built without the driver source
// still links.
class LoadSwitch {
 public:
  virtual void set_channel_role(uint8_t channel, LoadSwitchRole role) = 0;
  virtual void tick() = 0;
  virtual const LoadSwitchStatus& status() = 0;
  // Output frequency = this clock / divisor ratio.
  virtual uint32_t pwm_clock_hz() const = 0;
  // Marshalled from other tasks; consumed by the core-loop tick.
  virtual void request_manual(uint8_t channel, bool on) = 0;
  virtual void request_duty(uint8_t channel, uint16_t duty) = 0;
  virtual void request_divisor(uint8_t channel, uint8_t divisor_code) = 0;
};

#endif
