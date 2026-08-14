#ifndef __HW_STARK_H__
#define __HW_STARK_H__

#include <SPI.h>

#include "../../communication/can/Mcp2517fd.h"
#include "../../communication/can/NativeTwai.h"
#include "../../communication/rs485/Rs485Port.h"
#include "hal.h"
#include "GpioOutput.h"

/*
Stark CMR v1 - DIN-rail module with 4 power outputs, 1 x rs485, 1 x can and 1 x can-fd channel.
For more information on this board visit the project discord or contact johan@redispose.se

GPIOs on extra header
* GPIO  2
* GPIO 17 (only available if can channel 2 is inactive)
* GPIO 19
* GPIO 14 (JTAG TMS)
* GPIO 12 (JTAG TDI)
* GPIO 13 (JTAG TCK)
* GPIO 15 (JTAG TDO)
*/

inline NativeTwai stark_can_bus(CAN_LOG_ID_NATIVE, TwaiController::Twai0, {GPIO_NUM_27, GPIO_NUM_26, GPIO_NUM_NC});
inline Mcp2517fd stark_fd1_v1_bus(CAN_LOG_ID_MCP2517FD, HSPI, "CANFD",
                                  {GPIO_NUM_16, GPIO_NUM_34, GPIO_NUM_5, GPIO_NUM_18, GPIO_NUM_35, GPIO_NUM_NC,
                                   GPIO_NUM_NC},
                                  40000000, MCP2517_CLKODIV_DIV10, Mcp2517fdSettingsSlot::Fd1);
inline Mcp2517fd stark_fd1_v2_bus(CAN_LOG_ID_MCP2517FD, HSPI, "CANFD",
                                  {GPIO_NUM_17, GPIO_NUM_34, GPIO_NUM_5, GPIO_NUM_18, GPIO_NUM_35, GPIO_NUM_NC,
                                   GPIO_NUM_NC},
                                  40000000, MCP2517_CLKODIV_DIV10, Mcp2517fdSettingsSlot::Fd1);
inline Mcp2517fd stark_fd2_v1_bus(CAN_LOG_ID_MCP2517FD_2, HSPI, "CANFD2",
                                  {GPIO_NUM_16, GPIO_NUM_34, GPIO_NUM_5, GPIO_NUM_12, GPIO_NUM_14, GPIO_NUM_NC,
                                   GPIO_NUM_NC},
                                  MCP2517_OSC_AUTODETECT, MCP2517_CLKODIV_UNSET, Mcp2517fdSettingsSlot::Fd2);
inline Mcp2517fd stark_fd2_v2_bus(CAN_LOG_ID_MCP2517FD_2, HSPI, "CANFD2",
                                  {GPIO_NUM_17, GPIO_NUM_34, GPIO_NUM_5, GPIO_NUM_12, GPIO_NUM_14, GPIO_NUM_NC,
                                   GPIO_NUM_NC},
                                  MCP2517_OSC_AUTODETECT, MCP2517_CLKODIV_UNSET, Mcp2517fdSettingsSlot::Fd2);
inline Rs485Port stark_rs485_port(Serial2, UART_NUM_2, {GPIO_NUM_22, GPIO_NUM_21, GPIO_NUM_NC});

inline GpioOutput stark_positive_contactor(GPIO_NUM_32, kLedcChannelPositive);
inline GpioOutput stark_negative_contactor(GPIO_NUM_33, kLedcChannelNegative);
inline GpioOutput stark_output_23(GPIO_NUM_23);
inline GpioOutput stark_output_25(GPIO_NUM_25);
inline GpioOutput stark_second_battery_contactors(GPIO_NUM_19);
inline GpioOutput stark_third_battery_contactors(GPIO_NUM_15);

inline constexpr SwitchedOutputBinding kStarkOutputs[] = {
    {OutputRole::PositiveContactor, &stark_positive_contactor},
    {OutputRole::NegativeContactor, &stark_negative_contactor},
    {OutputRole::Precharge, &stark_output_25},
    {OutputRole::BmsPower, &stark_output_23},
    {OutputRole::SecondBatteryContactors, &stark_second_battery_contactors},
    {OutputRole::ThirdBatteryContactors, &stark_third_battery_contactors},
};
inline constexpr SwitchedOutputBinding kStarkOutputsBmsPower25[] = {
    {OutputRole::PositiveContactor, &stark_positive_contactor},
    {OutputRole::NegativeContactor, &stark_negative_contactor},
    {OutputRole::Precharge, &stark_output_23},
    {OutputRole::BmsPower, &stark_output_25},
    {OutputRole::SecondBatteryContactors, &stark_second_battery_contactors},
    {OutputRole::ThirdBatteryContactors, &stark_third_battery_contactors},
};

// No pin roles: the choice only selects which switched-output table is active.
inline constexpr GpioOptionChoice kStarkGpioOpt5Choices[] = {
    {0, "Pin 23 (BMS POWER)", nullptr, 0, 0},
    {1, "Pin 25 (PRECHARGE)", nullptr, 0, 1},
};
inline constexpr GpioOptionGroup kStarkGpioOptions[] = {
    {"GPIOOPT5", "BMS Power pin", kStarkGpioOpt5Choices, 2, 0},
};
static_assert(gpio_choices_valid(kStarkGpioOptions[0]));
static_assert(gpio_choices_cover_same_roles(kStarkGpioOptions[0]));
static_assert(gpio_groups_roles_disjoint(make_gpio_option_catalog(kStarkGpioOptions)));

// Legacy CanFdNative is physically the first MCP2517FD; the label stays.
inline constexpr InterfaceDescriptor kStarkV1Interfaces[] = {
    {InterfaceType::Modbus, nullptr, comm_interface::Modbus, nullptr, &stark_rs485_port},
    {InterfaceType::Rs485, nullptr, comm_interface::RS485, nullptr, &stark_rs485_port},
    {InterfaceType::CanNative, "CAN 1 (Native)", comm_interface::CanNative, &stark_can_bus},
    {InterfaceType::CanMcp2517fd, "CAN FD 2 (Native)", comm_interface::CanFdNative, &stark_fd1_v1_bus},
    {InterfaceType::CanMcp2517fd, "MCP2518FD (GPIO add-on)", comm_interface::CanFdAddonMcp2518_2, &stark_fd2_v1_bus},
};
static_assert(has_type(make_interface_list(kStarkV1Interfaces), InterfaceType::CanNative),
              "every board table needs a native CAN descriptor");
inline constexpr InterfaceDescriptor kStarkV2Interfaces[] = {
    {InterfaceType::Modbus, nullptr, comm_interface::Modbus, nullptr, &stark_rs485_port},
    {InterfaceType::Rs485, nullptr, comm_interface::RS485, nullptr, &stark_rs485_port},
    {InterfaceType::CanNative, "CAN 1 (Native)", comm_interface::CanNative, &stark_can_bus},
    {InterfaceType::CanMcp2517fd, "CAN FD 2 (Native)", comm_interface::CanFdNative, &stark_fd1_v2_bus},
    {InterfaceType::CanMcp2517fd, "MCP2518FD (GPIO add-on)", comm_interface::CanFdAddonMcp2518_2, &stark_fd2_v2_bus},
};
static_assert(has_type(make_interface_list(kStarkV2Interfaces), InterfaceType::CanNative),
              "every board table needs a native CAN descriptor");

class StarkHal : public Esp32Hal {
 public:
  const char* name() { return "Stark CMR Module"; }

  //Always enable BMS power on Stark CMR, it does not collide with any pin definitions
  virtual bool always_enable_bms_power() { return true; }

  // Stark CMR v1 has GPIO pin 16 for SCK, CMR v2 has GPIO pin 17. Only diff between the two boards
  bool isStarkVersion1() {
    size_t flashSize = ESP.getFlashChipSize();
    if (flashSize == 4 * 1024 * 1024) {
      return true;
    } else {  //v2
      return false;
    }
  }

  // Pins to be latched across a reset/OTA reboot (RTC-capable pins only): BMS_POWER can be GPIO25
  virtual std::vector<gpio_num_t> reset_hold_pins() { return {GPIO_NUM_25}; }

  // Automatic precharging
  virtual gpio_num_t HIA4V1_PIN() { return GPIO_NUM_19; }
  virtual gpio_num_t INVERTER_DISCONNECT_CONTACTOR_PIN() { return GPIO_NUM_25; }

  // SMA CAN contactor pins
  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_PIN() { return GPIO_NUM_2; }

  // LED
  virtual gpio_num_t LED_PIN() { return GPIO_NUM_4; }
  virtual uint8_t LED_MAX_BRIGHTNESS() { return 20; }
  // LEDs 1-4 (PRECHARGE, CONTACTOR NEG, CONTACTOR POS, BMS POWER) are chained off the STATUS LED
  // (pixel 0). On boards with the older plain hardwired LEDs there's no physical RGB LED at those
  // positions, so this extra chain data has nowhere to go and is simply a no-op; on boards with
  // the RGB LED PCB it lights them. No user-facing option needed either way.
  virtual uint16_t LED_COUNT() { return 5; }
  virtual int LED_INDICATOR_INDEX(size_t indicator) { return indicator + 1; }

  // Equipment stop pin
  virtual gpio_num_t EQUIPMENT_STOP_PIN() { return GPIO_NUM_2; }

  // Battery wake up pins
  virtual gpio_num_t WUP_PIN1() { return GPIO_NUM_25; }
  virtual gpio_num_t WUP_PIN2() { return GPIO_NUM_32; }

  // the FLA momentary push-button that can be long-pressed at runtime to start the Wi-Fi AP if not running
  virtual gpio_num_t AP_BUTTON_PIN() { return GPIO_NUM_0; }

  GpioOptionCatalog gpio_options() override { return make_gpio_option_catalog(kStarkGpioOptions); }

  virtual SwitchedOutputList switched_outputs() {
    return gpio_output_variant(0) == 0 ? make_switched_output_list(kStarkOutputs)
                                       : make_switched_output_list(kStarkOutputsBmsPower25);
  }

  InterfaceList interfaces() {
    return isStarkVersion1() ? make_interface_list(kStarkV1Interfaces) : make_interface_list(kStarkV2Interfaces);
  }
};

#endif  // __HW_STARK_H__
