#ifndef __HW_BECOM_H__
#define __HW_BECOM_H__

#include <SPI.h>

#include "../../communication/can/Mcp2517fd.h"
#include "../../communication/can/NativeTwai.h"
#include "../../communication/rs485/Rs485Port.h"
#include "hal.h"
#include "GpioOutput.h"

inline NativeTwai becom_can_bus(CAN_LOG_ID_NATIVE, TwaiController::Twai0, {GPIO_NUM_8, GPIO_NUM_18, GPIO_NUM_NC});
inline Mcp2517fd becom_fd1_bus(CAN_LOG_ID_MCP2517FD, FSPI, "CANFD",
                               {GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_11, GPIO_NUM_14, GPIO_NUM_10, GPIO_NUM_NC,
                                GPIO_NUM_NC},
                               40000000, MCP2517_CLKODIV_DIV1, Mcp2517fdSettingsSlot::Fd1);
inline Mcp2517fd becom_fd2_bus(CAN_LOG_ID_MCP2517FD_2, FSPI, "CANFD2",
                               {GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_11, GPIO_NUM_21, GPIO_NUM_9, GPIO_NUM_NC,
                                GPIO_NUM_NC},
                               40000000, MCP2517_CLKODIV_UNSET, Mcp2517fdSettingsSlot::Fd2);
// New Drive Enable Pin for RS485 Half Duplex communication
inline Rs485Port becom_rs485_port(Serial2, UART_NUM_2, {GPIO_NUM_15, GPIO_NUM_17, GPIO_NUM_16});

inline GpioOutput becom_positive_contactor(GPIO_NUM_47, kLedcChannelPositive);
inline GpioOutput becom_negative_contactor(GPIO_NUM_48, kLedcChannelNegative);
inline GpioOutput becom_precharge(GPIO_NUM_45);
// BMS power sense is inverted in hardware (high shuts the battery down);
// the shared BMS-power code still drives it active-high.
inline GpioOutput becom_bms_power(GPIO_NUM_1);
inline GpioOutput becom_second_battery_contactors(GPIO_NUM_37);

inline constexpr SwitchedOutputBinding kBEComOutputs[] = {
    {OutputRole::PositiveContactor, &becom_positive_contactor},
    {OutputRole::NegativeContactor, &becom_negative_contactor},
    {OutputRole::Precharge, &becom_precharge},
    {OutputRole::BmsPower, &becom_bms_power},
    {OutputRole::SecondBatteryContactors, &becom_second_battery_contactors},
};

inline constexpr InterfaceDescriptor kBEComInterfaces[] = {
    {InterfaceType::Modbus, nullptr, comm_interface::Modbus, nullptr, &becom_rs485_port},
    {InterfaceType::Rs485, nullptr, comm_interface::RS485, nullptr, &becom_rs485_port},
    {InterfaceType::CanNative, "Inverter CAN (Native)", comm_interface::CanNative, &becom_can_bus},
    {InterfaceType::CanMcp2517fd, "CAN FD Battery 1 (MCP2518)", comm_interface::CanFdAddonMcp2518, &becom_fd1_bus},
    {InterfaceType::CanMcp2517fd, "CAN FD Battery 2 (MCP2518)", comm_interface::CanFdAddonMcp2518_2, &becom_fd2_bus},
};
static_assert(has_type(make_interface_list(kBEComInterfaces), InterfaceType::CanNative),
              "every board table needs a native CAN descriptor");

class BEComHal : public Esp32Hal {
 public:
  const char* name() { return "BECom"; }

  // Pins to be latched across a reset/OTA reboot (RTC-capable pins only): BMS_POWER is GPIO1
  virtual std::vector<gpio_num_t> reset_hold_pins() { return {GPIO_NUM_1}; }

  // Automatic precharging
  // HIA4V1_PIN mapped to Battery 1 Pre-Chage Contactor Pin
  virtual gpio_num_t HIA4V1_PIN() { return GPIO_NUM_45; }
  // INVERTER_DISCONNECT_CONTACTOR_PIN mapped to Battery 1 Positive Contactor Pin
  virtual gpio_num_t INVERTER_DISCONNECT_CONTACTOR_PIN() { return GPIO_NUM_47; }

  // SMA CAN contactor pin
  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_PIN() { return GPIO_NUM_3; }

  // LED
  virtual gpio_num_t LED_PIN() { return GPIO_NUM_5; }
  // Equipment stop pin
  // Mapped to and internal allways low pin, in hardware v1
  virtual gpio_num_t EQUIPMENT_STOP_PIN() { return GPIO_NUM_46; }

  // Battery wake up pins
  virtual gpio_num_t WUP_PIN1() { return GPIO_NUM_2; }
  virtual gpio_num_t WUP_PIN2() { return GPIO_NUM_41; }

  // Momentary push-button that can be long-pressed at runtime to start the Wi-Fi AP.
  virtual gpio_num_t AP_BUTTON_PIN() { return GPIO_NUM_0; }

  virtual SwitchedOutputList switched_outputs() { return make_switched_output_list(kBEComOutputs); }

  InterfaceList interfaces() { return make_interface_list(kBEComInterfaces); }
};

/* ----- Error checks below, don't change (can't be moved to separate file) ----- */
#ifndef HW_CONFIGURED
#define HW_CONFIGURED
#else
#error Multiple HW defined! Please select a single HW
#endif

#endif
