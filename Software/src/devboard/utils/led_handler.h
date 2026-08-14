#ifndef LED_H_
#define LED_H_

#include <soc/gpio_num.h>
#include "../../devboard/utils/types.h"
#include "../../lib/adafruit-Adafruit_NeoPixel/Adafruit_NeoPixel.h"

static const uint32_t LED_COLOR_WHITE = 0xFFFFFF;  // R=G=B=255

enum class IndicatorLed : uint8_t { PRECHARGE = 0, CONTACTOR_NEG = 1, CONTACTOR_POS = 2, BMS_POWER = 3 };
inline constexpr uint8_t kIndicatorLedCount = 4;

class LED {
 public:
  LED(led_mode_enum mode, gpio_num_t pin, uint8_t maxBrightness, uint16_t pixel_count, uint16_t status_index,
      led_color_order order)
      : pixels(pin, pixel_count),
        max_brightness(maxBrightness),
        brightness(maxBrightness),
        mode(mode),
        status_index(status_index) {
    pixels.setColorOrder(order);
  }

  void exe(void);

 private:
  Adafruit_NeoPixel pixels;
  uint8_t max_brightness;
  uint8_t brightness;
  led_mode_enum mode;
  uint16_t status_index;

  void classic_run(void);
  void flow_run(void);
  void heartbeat_run(void);
  void render_indicators(uint32_t color);
#ifdef BOARD_HAS_INTERFACE_ACTIVITY_LEDS
  void render_interface_activity(void);
#endif
#ifdef BOARD_HAS_LOAD_SWITCH
  void render_load_switch_channels(void);
#endif

  uint8_t up_down(uint16_t middle_point_f);
  uint16_t LED_PERIOD_MS = 3000;
};

bool led_init(void);
void led_exe(void);

// Temporarily override the LED for button-hold feedback: blinks `color` on/off at
// `period_ms`. Pass active=false to release and resume normal battery-state behavior.
// No-op when no LED is present (GPIO unset, or configured as an OLED instead).
void set_led_override(bool active, uint32_t color, uint16_t period_ms);

// Mirrors on/off state onto an RGB indicator LED, for boards where an indicator position is an
// RGB LED instead of a plain hardwired GPIO LED. No-op on boards without RGB indicator LEDs.
void set_indicator_led(IndicatorLed indicator, bool on);

#endif  // LED_H_
