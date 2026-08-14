#ifndef __HW_DEVKIT_H__
#define __HW_DEVKIT_H__

#include <SPI.h>

#include "../../communication/can/Mcp2515.h"
#include "../../communication/can/Mcp2517fd.h"
#include "../../communication/can/NativeTwai.h"
#include "../../communication/rs485/Rs485Port.h"
#include "GpioOutput.h"

/*
ESP32 DevKit V1 development board with 30 pins.
For more information, see: https://lastminuteengineers.com/esp32-pinout-reference/.

The pin layout below supports the following:
- 1x RS485
- 2x CAN (1x via SN65HVD230 (UART), 1x via MCP2515 (SPI))
- 1x CANFD (via MCP2518FD (SPI))
*/

inline NativeTwai devkit_can_bus(CAN_LOG_ID_NATIVE, TwaiController::Twai0, {GPIO_NUM_27, GPIO_NUM_26, GPIO_NUM_NC});
inline Mcp2515 devkit_mcp2515_bus(CAN_LOG_ID_MCP2515, VSPI, "CAN",
                                  {GPIO_NUM_22, GPIO_NUM_19, GPIO_NUM_21, GPIO_NUM_18, GPIO_NUM_23, GPIO_NUM_NC},
                                  MCP2515_FREQ_AUTODETECT);
inline Mcp2517fd devkit_mcp2517fd_bus(CAN_LOG_ID_MCP2517FD, HSPI, "CANFD",
                                      {GPIO_NUM_33, GPIO_NUM_35, GPIO_NUM_32, GPIO_NUM_25, GPIO_NUM_34, GPIO_NUM_NC,
                                       GPIO_NUM_NC},
                                      MCP2517_OSC_AUTODETECT, MCP2517_CLKODIV_DIV10, Mcp2517fdSettingsSlot::Fd1);
inline Rs485Port devkit_rs485_port(Serial2, UART_NUM_2, {GPIO_NUM_1, GPIO_NUM_3, GPIO_NUM_NC});

inline GpioOutput devkit_positive_contactor(GPIO_NUM_5, kLedcChannelPositive);
inline GpioOutput devkit_negative_contactor(GPIO_NUM_16, kLedcChannelNegative);
inline GpioOutput devkit_precharge(GPIO_NUM_17);
inline GpioOutput devkit_second_battery_contactors(GPIO_NUM_32);

inline constexpr SwitchedOutputBinding kDevKitOutputs[] = {
    {OutputRole::PositiveContactor, &devkit_positive_contactor},
    {OutputRole::NegativeContactor, &devkit_negative_contactor},
    {OutputRole::Precharge, &devkit_precharge},
    {OutputRole::SecondBatteryContactors, &devkit_second_battery_contactors},
};

inline constexpr InterfaceDescriptor kDevKitInterfaces[] = {
    {InterfaceType::Modbus, nullptr, comm_interface::Modbus, nullptr, &devkit_rs485_port},
    {InterfaceType::Rs485, nullptr, comm_interface::RS485, nullptr, &devkit_rs485_port},
    {InterfaceType::CanNative, nullptr, comm_interface::CanNative, &devkit_can_bus},
    {InterfaceType::CanMcp2515, nullptr, comm_interface::CanAddonMcp2515, &devkit_mcp2515_bus},
    {InterfaceType::CanMcp2517fd, nullptr, comm_interface::CanFdAddonMcp2518, &devkit_mcp2517fd_bus},
};
static_assert(has_type(make_interface_list(kDevKitInterfaces), InterfaceType::CanNative),
              "every board table needs a native CAN descriptor");

class DevKitHal : public Esp32Hal {
 public:
  const char* name() { return "ESP32 DevKit V1"; }

  // Automatic precharging
  virtual gpio_num_t HIA4V1_PIN() { return GPIO_NUM_4; }
  virtual gpio_num_t INVERTER_DISCONNECT_CONTACTOR_PIN() { return GPIO_NUM_5; }

  // SMA CAN contactor pins
  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_PIN() { return GPIO_NUM_14; }

  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_LED_PIN() { return GPIO_NUM_2; }

  // LED
  virtual gpio_num_t LED_PIN() { return GPIO_NUM_4; }
  // Equipment stop pin
  virtual gpio_num_t EQUIPMENT_STOP_PIN() { return GPIO_NUM_12; }

  // Battery wake up pins
  virtual gpio_num_t WUP_PIN1() { return GPIO_NUM_25; }
  virtual gpio_num_t WUP_PIN2() { return GPIO_NUM_32; }

  // Momentary push-button that can be long-pressed at runtime to start the Wi-Fi AP.
  virtual gpio_num_t AP_BUTTON_PIN() { return GPIO_NUM_0; }

  virtual SwitchedOutputList switched_outputs() { return make_switched_output_list(kDevKitOutputs); }

  InterfaceList interfaces() { return make_interface_list(kDevKitInterfaces); }
};

#endif  // __HW_DEVKIT_H__
