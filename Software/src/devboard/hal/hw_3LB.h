#ifndef __HW_3LB_H__
#define __HW_3LB_H__

#include <SPI.h>

#include "../../communication/can/Mcp2515.h"
#include "../../communication/can/Mcp2517fd.h"
#include "../../communication/can/NativeTwai.h"
#include "../../communication/rs485/Rs485Port.h"
#include "hal.h"
#include "GpioOutput.h"

inline NativeTwai three_lb_can_bus(CAN_LOG_ID_NATIVE, TwaiController::Twai0, {GPIO_NUM_27, GPIO_NUM_26, GPIO_NUM_NC});
inline Mcp2515 three_lb_mcp2515_bus(CAN_LOG_ID_MCP2515, VSPI, "CAN",
                                    {GPIO_NUM_12, GPIO_NUM_34, GPIO_NUM_5, GPIO_NUM_18, GPIO_NUM_35, GPIO_NUM_NC},
                                    MCP2515_FREQ_AUTODETECT);
inline Mcp2517fd three_lb_mcp2517fd_bus(CAN_LOG_ID_MCP2517FD, HSPI, "CANFD",
                                        {GPIO_NUM_17, GPIO_NUM_39, GPIO_NUM_23, GPIO_NUM_21, GPIO_NUM_34, GPIO_NUM_NC,
                                         GPIO_NUM_NC},
                                        MCP2517_OSC_AUTODETECT, MCP2517_CLKODIV_DIV10, Mcp2517fdSettingsSlot::Fd1);
inline Rs485Port three_lb_rs485_port(Serial2, UART_NUM_2, {GPIO_NUM_1, GPIO_NUM_3, GPIO_NUM_NC});

inline GpioOutput three_lb_positive_contactor(GPIO_NUM_32, kLedcChannelPositive);
inline GpioOutput three_lb_negative_contactor(GPIO_NUM_33, kLedcChannelNegative);
inline GpioOutput three_lb_precharge(GPIO_NUM_25);
inline GpioOutput three_lb_bms_power(GPIO_NUM_2);
inline GpioOutput three_lb_second_battery_contactors(GPIO_NUM_13);

inline constexpr SwitchedOutputBinding kThreeLBOutputs[] = {
    {OutputRole::PositiveContactor, &three_lb_positive_contactor},
    {OutputRole::NegativeContactor, &three_lb_negative_contactor},
    {OutputRole::Precharge, &three_lb_precharge},
    {OutputRole::BmsPower, &three_lb_bms_power},
    {OutputRole::SecondBatteryContactors, &three_lb_second_battery_contactors},
};

inline constexpr InterfaceDescriptor kThreeLBInterfaces[] = {
    {InterfaceType::Modbus, nullptr, comm_interface::Modbus, nullptr, &three_lb_rs485_port},
    {InterfaceType::Rs485, nullptr, comm_interface::RS485, nullptr, &three_lb_rs485_port},
    {InterfaceType::CanNative, nullptr, comm_interface::CanNative, &three_lb_can_bus},
    {InterfaceType::CanMcp2515, nullptr, comm_interface::CanAddonMcp2515, &three_lb_mcp2515_bus},
    {InterfaceType::CanMcp2517fd, nullptr, comm_interface::CanFdAddonMcp2518, &three_lb_mcp2517fd_bus},
};
static_assert(has_type(make_interface_list(kThreeLBInterfaces), InterfaceType::CanNative),
              "every board table needs a native CAN descriptor");

class ThreeLBHal : public Esp32Hal {
 public:
  const char* name() { return "3LB board"; }

  // CHAdeMO support pin dependencies
  virtual gpio_num_t CHADEMO_PIN_2() { return GPIO_NUM_12; }
  virtual gpio_num_t CHADEMO_PIN_10() { return GPIO_NUM_5; }
  virtual gpio_num_t CHADEMO_PIN_7() { return GPIO_NUM_34; }
  virtual gpio_num_t CHADEMO_PIN_4() { return GPIO_NUM_35; }
  virtual gpio_num_t CHADEMO_LOCK() { return GPIO_NUM_18; }
  virtual gpio_num_t CHADEMO_CT_PIN() { return GPIO_NUM_25; }

  // Automatic precharging
  virtual gpio_num_t HIA4V1_PIN() { return GPIO_NUM_25; }
  virtual gpio_num_t INVERTER_DISCONNECT_CONTACTOR_PIN() { return GPIO_NUM_32; }

  // SMA CAN contactor pins
  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_PIN() { return GPIO_NUM_36; }
  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_LED_PIN() { return GPIO_NUM_NC; }

  // LED
  virtual gpio_num_t LED_PIN() { return GPIO_NUM_4; }
  // Equipment stop pin
  virtual gpio_num_t EQUIPMENT_STOP_PIN() { return GPIO_NUM_35; }

  // Battery wake up pins
  virtual gpio_num_t WUP_PIN1() { return GPIO_NUM_25; }
  virtual gpio_num_t WUP_PIN2() { return GPIO_NUM_32; }

  virtual SwitchedOutputList switched_outputs() { return make_switched_output_list(kThreeLBOutputs); }

  InterfaceList interfaces() { return make_interface_list(kThreeLBInterfaces); }
};

#endif
