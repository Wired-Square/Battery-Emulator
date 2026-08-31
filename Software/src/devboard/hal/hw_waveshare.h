#ifndef __HW_WAVESHARE_H__
#define __HW_WAVESHARE_H__

#include <SPI.h>

#include "../../communication/can/Mcp2517fd.h"
#include "../../communication/can/NativeTwai.h"
#include "../../communication/rs485/Rs485Port.h"
#include "hal.h"
#include "GpioOutput.h"

inline NativeTwai waveshare_can_bus(CAN_LOG_ID_NATIVE, TwaiController::Twai0, {GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_NC});
inline Mcp2517fd waveshare_mcp2517fd_bus(CAN_LOG_ID_MCP2517FD, FSPI, "CANFD",
                                         {GPIO_NUM_10, GPIO_NUM_12, GPIO_NUM_11, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_NC,
                                          GPIO_NUM_NC},
                                         MCP2517_OSC_AUTODETECT, MCP2517_CLKODIV_DIV10, Mcp2517fdSettingsSlot::Fd1);
// SP3485 DE and /RE are tied together on this board.
// HIGH = transmit, LOW = receive.
inline Rs485Port waveshare_rs485_port(Serial2, UART_NUM_2, {GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_21});

inline GpioOutput waveshare_positive_contactor(GPIO_NUM_3, kLedcChannelPositive);
inline GpioOutput waveshare_negative_contactor(GPIO_NUM_4, kLedcChannelNegative);
inline GpioOutput waveshare_precharge(GPIO_NUM_5);
inline GpioOutput waveshare_bms_power(GPIO_NUM_6);
inline GpioOutput waveshare_second_battery_contactors(GPIO_NUM_8);

inline constexpr SwitchedOutputBinding kWaveshareOutputs[] = {
    {OutputRole::PositiveContactor, &waveshare_positive_contactor},
    {OutputRole::NegativeContactor, &waveshare_negative_contactor},
    {OutputRole::Precharge, &waveshare_precharge},
    {OutputRole::BmsPower, &waveshare_bms_power},
    {OutputRole::SecondBatteryContactors, &waveshare_second_battery_contactors},
};

inline constexpr PinAssignment kWaveshareOpt6LedPins[] = {
    {GpioPinRole::Led, GPIO_NUM_2},
    {GpioPinRole::DisplaySda, kGpioPinNotConnected},
    {GpioPinRole::DisplayScl, kGpioPinNotConnected},
};
inline constexpr PinAssignment kWaveshareOpt6DisplayPins[] = {
    {GpioPinRole::Led, kGpioPinNotConnected},
    {GpioPinRole::DisplaySda, GPIO_NUM_1},
    {GpioPinRole::DisplayScl, GPIO_NUM_2},
};
inline constexpr GpioOptionChoice kWaveshareGpioOpt6Choices[] = {
    {0, "Status LED (GPIO2)", kWaveshareOpt6LedPins, 3, 0},
#ifndef SMALL_FLASH_DEVICE
    {1, "I2C Display SSD1306 (GPIO1=SDA, GPIO2=SCL)", kWaveshareOpt6DisplayPins, 3, 0},
#else
    {1, nullptr, kWaveshareOpt6DisplayPins, 3, 0},
#endif
};
inline constexpr GpioOptionGroup kWaveshareGpioOptions[] = {
    {"GPIOOPT6", "GPIO 1/2 function", kWaveshareGpioOpt6Choices, 2, 0},
};
static_assert(gpio_choices_valid(kWaveshareGpioOptions[0]));
static_assert(gpio_choices_cover_same_roles(kWaveshareGpioOptions[0]));
static_assert(gpio_groups_roles_disjoint(make_gpio_option_catalog(kWaveshareGpioOptions)));

inline constexpr InterfaceDescriptor kWaveshareInterfaces[] = {
    {InterfaceType::Modbus, nullptr, comm_interface::Modbus, nullptr, &waveshare_rs485_port},
    {InterfaceType::Rs485, nullptr, comm_interface::RS485, nullptr, &waveshare_rs485_port},
    {InterfaceType::CanNative, nullptr, comm_interface::CanNative, &waveshare_can_bus},
    {InterfaceType::CanMcp2517fd, nullptr, comm_interface::CanFdAddonMcp2518, &waveshare_mcp2517fd_bus},
};
static_assert(has_type(make_interface_list(kWaveshareInterfaces), InterfaceType::CanNative),
              "every board table needs a native CAN descriptor");

class WaveshareS3Rs485CanHal : public Esp32Hal {
 public:
  const char* name() { return "Waveshare ESP32-S3-RS485-CAN"; }

  GpioOptionCatalog gpio_options() override { return make_gpio_option_catalog(kWaveshareGpioOptions); }

  virtual gpio_num_t LED_PIN() { return pin_for(GpioPinRole::Led, GPIO_NUM_NC); }

  // Pins to be latched across a reset/OTA reboot (RTC-capable pins only): BMS_POWER is GPIO6
  virtual std::vector<gpio_num_t> reset_hold_pins() { return {GPIO_NUM_6}; }

  // Equipment stop pin
  virtual gpio_num_t EQUIPMENT_STOP_PIN() { return GPIO_NUM_7; }

  // Battery wake up pins
  virtual gpio_num_t WUP_PIN1() { return GPIO_NUM_8; }
  virtual gpio_num_t WUP_PIN2() { return GPIO_NUM_9; }  //Collides with SMA contactor input

  // Momentary push-button that can be long-pressed at runtime to start the Wi-Fi AP.
  virtual gpio_num_t AP_BUTTON_PIN() { return GPIO_NUM_0; }

  // Automatic precharging
  virtual gpio_num_t HIA4V1_PIN() { return GPIO_NUM_5; }
  virtual gpio_num_t INVERTER_DISCONNECT_CONTACTOR_PIN() { return GPIO_NUM_3; }

  // SMA CAN contactor pins (collides with WUP2)
  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_PIN() { return GPIO_NUM_9; }

#ifndef SMALL_FLASH_DEVICE
  // i2c display
  virtual gpio_num_t DISPLAY_SDA_PIN() { return pin_for(GpioPinRole::DisplaySda, GPIO_NUM_NC); }
  virtual gpio_num_t DISPLAY_SCL_PIN() { return pin_for(GpioPinRole::DisplayScl, GPIO_NUM_NC); }
#endif  // SMALL_FLASH_DEVICE

  virtual SwitchedOutputList switched_outputs() { return make_switched_output_list(kWaveshareOutputs); }

  virtual bool configure_rs485_half_duplex(Rs485Port& port) {
    return rs485_configure_half_duplex(port.uart_num(), port.pins());
  }

  InterfaceList interfaces() { return make_interface_list(kWaveshareInterfaces); }
};

/* ----- Error checks below, don't change (can't be moved to separate file) ----- */
#ifndef HW_CONFIGURED
#define HW_CONFIGURED
#else
#error Multiple HW defined! Please select a single HW
#endif

#endif
