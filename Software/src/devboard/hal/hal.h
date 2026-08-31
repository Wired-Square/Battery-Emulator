#ifndef _HAL_H_
#define _HAL_H_

#include <soc/gpio_num.h>
#include <chrono>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../../../src/devboard/utils/events.h"
#include "../../../src/devboard/utils/logging.h"
#include "../../../src/devboard/utils/types.h"
#include "../webserver/response_writer.h"
#include "../webserver/settings_field.h"
#include "interface_descriptor.h"
#include "SwitchedOutput.h"
#include "LoadSwitch.h"
#include "gpio_options.h"

// Hardware Abstraction Layer base class.
// Derive a class to define board-specific parameters such as GPIO pin numbers
// This base class implements a mechanism for allocating GPIOs.
class Esp32Hal {
 public:
  virtual const char* name() = 0;

  // Time it takes before system is considered fully started up.
  virtual duration BOOTUP_TIME() { return milliseconds(1000); }
  virtual bool system_booted_up();

  // Core assignment
  virtual int CORE_FUNCTION_CORE() { return 1; }
  virtual int MODBUS_CORE() { return 0; }
  virtual int WIFICORE() { return 0; }

  virtual void set_default_configuration_values() {}

  // Board peripheral bring-up (IO expanders, transceiver standby, ...).
  // Runs once from setup(), after stored settings load and before any
  // communication interface initialises.
  virtual void board_init() {}

  virtual void board_tick() {}

  // Half-duplex direction control for a port that wires a DE pin. Answering
  // false refuses the port rather than letting it run full duplex.
  virtual bool configure_rs485_half_duplex(Rs485Port& /*port*/) { return false; }

  // Software-switchable bus termination, keyed by descriptor index.
  virtual bool supports_interface_termination(size_t /*interface_index*/) { return false; }
  virtual bool set_interface_termination(size_t /*interface_index*/, bool /*enabled*/) { return false; }
  virtual bool interface_termination(size_t /*interface_index*/) { return false; }

  virtual DeviceSettingList settings() { return {}; }
  virtual void write_status(ResponseWriter& /*out*/) {}

  // Never filter here: applicability is a per-row question, answered by
  // DeviceSetting::applies_to reading this live HAL. A second row in the same
  // scope with different applicability cannot be expressed once these are merged.
  virtual ScopeEntries scope_entries(SettingScope scope) {
    if (scope != SettingScope::Interface) {
      return {};
    }
    static ScopeEntry entries[kMaxInterfaceDescriptors];
    InterfaceList list = interfaces();
    const size_t count = list.count < kMaxInterfaceDescriptors ? list.count : kMaxInterfaceDescriptors;
    for (size_t i = 0; i < count; i++) {
      entries[i] = {static_cast<uint8_t>(i), descriptor_name(list.data[i])};
    }
    return {entries, count};
  }

  template <typename... Pins>
  bool alloc_pins(const char* name, Pins... pins) {
    std::vector<gpio_num_t> requested_pins = {static_cast<gpio_num_t>(pins)...};

    for (gpio_num_t pin : requested_pins) {
      if (pin < 0) {
        // Must be set BEFORE set_event(): set_event() logs the event message immediately,
        // and get_event_message_string() reads it back via failed_allocator().
        allocator_name = name;
        set_event(EVENT_GPIO_NOT_DEFINED, (int)pin);  // also printing a log entry
        return false;
      }

      auto it = allocated_pins.find(pin);
      if (it != allocated_pins.end()) {
        allocator_name = name;
        allocated_name = it->second.c_str();
        set_event(EVENT_GPIO_CONFLICT, (int)pin);  // also printing a log entry
        return false;
      }
    }

    for (gpio_num_t pin : requested_pins) {
      allocated_pins[pin] = name;
    }

    return true;
  }

  virtual bool always_enable_bms_power() { return false; }

  // CHAdeMO support pin dependencies
  virtual gpio_num_t CHADEMO_PIN_2() { return GPIO_NUM_NC; }
  virtual gpio_num_t CHADEMO_PIN_10() { return GPIO_NUM_NC; }
  virtual gpio_num_t CHADEMO_PIN_7() { return GPIO_NUM_NC; }
  virtual gpio_num_t CHADEMO_PIN_4() { return GPIO_NUM_NC; }
  virtual gpio_num_t CHADEMO_LOCK() { return GPIO_NUM_NC; }
  virtual gpio_num_t CHADEMO_CT_PIN() { return GPIO_NUM_NC; }

  // Contactor-role outputs, bound by board data. Boot-stable.
  virtual SwitchedOutputList switched_outputs() { return {nullptr, 0}; }

  SwitchedOutput* switched_output(OutputRole role) {
    SwitchedOutputList list = switched_outputs();
    for (size_t i = 0; i < list.count; i++) {
      if (list.data[i].role == role) {
        return list.data[i].output;
      }
    }
    return nullptr;
  }

  // Board-owned multichannel load switch; nullptr when the board has none.
  virtual LoadSwitch* load_switch() { return nullptr; }

  // Output pins to latch at their driven level across a firmware-initiated reset/OTA
  // reboot, so they don't float during the boot window. RTC-capable pins only.
  virtual std::vector<gpio_num_t> reset_hold_pins() { return {}; }

  // Automatic precharging
  virtual gpio_num_t HIA4V1_PIN() { return GPIO_NUM_NC; }
  virtual gpio_num_t INVERTER_DISCONNECT_CONTACTOR_PIN() { return GPIO_NUM_NC; }

  // SMA CAN contactor pins
  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_PIN() { return GPIO_NUM_NC; }

  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_LED_PIN() { return GPIO_NUM_NC; }

#ifdef SDCARD
  virtual uint8_t SD_SPI_BUS() = 0;
#endif  // SDCARD

  // SD card
  virtual gpio_num_t SD_MISO_PIN() { return GPIO_NUM_NC; }
  virtual gpio_num_t SD_MOSI_PIN() { return GPIO_NUM_NC; }
  virtual gpio_num_t SD_SCLK_PIN() { return GPIO_NUM_NC; }
  virtual gpio_num_t SD_CS_PIN() { return GPIO_NUM_NC; }

  // LED
  virtual gpio_num_t LED_PIN() { return GPIO_NUM_NC; }
  virtual uint8_t LED_MAX_BRIGHTNESS() { return 40; }
  // WS281x chain length and role→pixel map; -1 = no LED for that role.
  virtual uint16_t LED_COUNT() { return 1; }
  virtual uint16_t LED_STATUS_INDEX() { return 0; }
  virtual int LED_INTERFACE_ACTIVITY_INDEX(size_t /*interface_index*/) { return -1; }
  virtual int LED_SWITCHED_OUTPUT_INDEX(size_t /*channel*/) { return -1; }
  virtual int LED_INDICATOR_INDEX(size_t /*indicator*/) { return -1; }
  virtual led_color_order LED_COLOR_ORDER() { return led_color_order::RGB; }

#ifndef SMALL_FLASH_DEVICE
  // i2c display
  virtual gpio_num_t DISPLAY_SDA_PIN() { return GPIO_NUM_NC; }
  virtual gpio_num_t DISPLAY_SCL_PIN() { return GPIO_NUM_NC; }
#endif  // SMALL_FLASH_DEVICE

  // Equipment stop pin
  virtual gpio_num_t EQUIPMENT_STOP_PIN() { return GPIO_NUM_NC; }

  // Runtime input on the BOOT strapping button. GPIO_NUM_NC on boards that
  // repurpose the strapping pin or have no button.
  virtual gpio_num_t BOOT_BUTTON_PIN() { return GPIO_NUM_0; }

  // Battery wake up pins
  virtual gpio_num_t WUP_PIN1() { return GPIO_NUM_NC; }
  virtual gpio_num_t WUP_PIN2() { return GPIO_NUM_NC; }

  // Momentary push-button that can be long-pressed at runtime to start the Wi-Fi AP. Usually the BOOT button on GPIO0.
  virtual gpio_num_t AP_BUTTON_PIN() { return GPIO_NUM_NC; }

  // Route through WUP_PIN1/WUP_PIN2 so per-board overrides still apply.
  virtual gpio_num_t wakeup_pin(size_t battery_index) {
    return battery_index == 0 ? WUP_PIN1() : (battery_index == 1 ? WUP_PIN2() : GPIO_NUM_NC);
  }

  virtual InterfaceList interfaces() = 0;

  String failed_allocator() { return allocator_name; }
  String conflicting_allocator() { return allocated_name; }

  // Default: no options. Boards override gpio_options() with their static catalog.
  virtual GpioOptionCatalog gpio_options() { return {nullptr, 0}; }
  uint8_t gpio_option_value(size_t group_index) const {
    return group_index < kMaxGpioOptionGroups ? gpio_option_selections_[group_index] : 0;
  }
  void set_gpio_option_value(size_t group_index, uint8_t value) {
    if (group_index < kMaxGpioOptionGroups) {
      gpio_option_selections_[group_index] = value;
    }
  }
  gpio_num_t pin_for(GpioPinRole role, gpio_num_t fallback) {
    return (gpio_num_t)resolve_gpio_pin(gpio_options(), gpio_option_selections_, kMaxGpioOptionGroups, role,
                                        (int16_t)fallback);
  }

  // Boards that swap switched-output tables key on this, not a raw choice value.
  uint8_t gpio_output_variant(size_t group_index) {
    GpioOptionCatalog catalog = gpio_options();
    if (group_index >= catalog.group_count) {
      return 0;
    }
    const GpioOptionChoice* choice = find_gpio_option_choice(catalog.groups[group_index], gpio_option_value(group_index));
    return choice != nullptr ? choice->output_variant : 0;
  }

 private:
  std::unordered_map<gpio_num_t, std::string> allocated_pins;

  // For event logging, store the name of the allocator/allocated
  // for failed gpio allocations.
  String allocator_name;
  String allocated_name;

  uint8_t gpio_option_selections_[kMaxGpioOptionGroups] = {0};
};

extern Esp32Hal* esp32hal;

// Needed for AsyncTCPSock library.
#define WIFI_CORE (esp32hal->WIFICORE())

void init_hal();

#endif
