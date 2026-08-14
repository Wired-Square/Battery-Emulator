#ifndef _NATIVE_TWAI_H_
#define _NATIVE_TWAI_H_

#include "CanBus.h"

class ACAN_ESP32_Settings;

// Native TWAI controller ids. The wiring behind an id (driver instance,
// reset module) resolves inside NativeTwai.cpp. Twai1 exists only on
// multi-controller SoCs; a bus bound to it elsewhere fails init rather
// than aliasing controller 0.
enum class TwaiController : uint8_t { Twai0, Twai1 };

struct NativeTwaiPins {
  gpio_num_t tx;
  gpio_num_t rx;
  gpio_num_t se;
};

class NativeTwai : public CanBus {
 public:
  NativeTwai(uint8_t log_id, TwaiController controller, NativeTwaiPins pins)
      : CanBus(log_id), controller_(controller), pins_(pins) {}
  void receive() override;
  bool transmit_frame(const CAN_frame& frame) override;
  bool change_speed(CAN_Speed new_speed) override;
  void stop() override;
  void restart() override;

 protected:
  bool init_hw() override;

 private:
  uint32_t begin_driver(CAN_Speed new_speed, gpio_num_t tx_pin, gpio_num_t rx_pin);
  TwaiController controller_;
  NativeTwaiPins pins_;
  ACAN_ESP32_Settings* settings_ = nullptr;
};

#endif
