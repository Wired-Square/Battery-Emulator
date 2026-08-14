#ifndef _SWITCHED_OUTPUT_H_
#define _SWITCHED_OUTPUT_H_

#include <stddef.h>
#include <stdint.h>

// Roles the contactor-control code drives. A board without an output for a
// role has no binding row; callers treat the feature as absent.
enum class OutputRole : uint8_t {
  PositiveContactor,
  NegativeContactor,
  Precharge,
  BmsPower,
  SecondBatteryContactors,
  ThirdBatteryContactors,
};

inline constexpr uint16_t PWM_ON_DUTY = 1023;
inline constexpr uint16_t PWM_OFF_DUTY = 0;
inline constexpr uint8_t PWM_RESOLUTION = 10;

class SwitchedOutput {
 public:
  // Claims pins and prepares the driver; applies no output level. The owner
  // label feeds pin-allocation conflict events.
  virtual bool init(const char* owner) = 0;
  // Contactor semantics: contactor_control_inverted_logic flips the digital
  // path only; a PWM duty is never inverted.
  virtual void set(bool on) = 0;
  // Supply-rail semantics (BMS power): never inverted.
  virtual void set_raw(bool on) { set(on); }
  // Drop an engaged output to its economisation hold level.
  virtual void set_hold() = 0;
  virtual bool fault() { return false; }
  // Raw output read-back, matching digitalRead on a GPIO pin.
  virtual bool level() = 0;
};

struct SwitchedOutputBinding {
  OutputRole role;
  SwitchedOutput* output;
};

struct SwitchedOutputList {
  const SwitchedOutputBinding* data;
  size_t count;
};

template <size_t N>
constexpr SwitchedOutputList make_switched_output_list(const SwitchedOutputBinding (&arr)[N]) {
  return {arr, N};
}

#endif
