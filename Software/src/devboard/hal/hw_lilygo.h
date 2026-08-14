#ifndef __HW_LILYGO_H__
#define __HW_LILYGO_H__

#include <Arduino.h>
#include <SPI.h>

#include "../../communication/can/Mcp2515.h"
#include "../../communication/can/Mcp2517fd.h"
#include "../../communication/can/NativeTwai.h"
#include "../../communication/rs485/Rs485Port.h"
#include "hal.h"
#include "GpioOutput.h"

#include "../utils/types.h"

inline NativeTwai lilygo_can_bus(CAN_LOG_ID_NATIVE, TwaiController::Twai0, {GPIO_NUM_27, GPIO_NUM_26, GPIO_NUM_23});
inline Mcp2515 lilygo_mcp2515_bus(CAN_LOG_ID_MCP2515, VSPI, "CAN",
                                  {GPIO_NUM_12, GPIO_NUM_34, GPIO_NUM_5, GPIO_NUM_18, GPIO_NUM_35, GPIO_NUM_NC},
                                  MCP2515_FREQ_AUTODETECT);
inline Mcp2517fd lilygo_mcp2517fd_bus(CAN_LOG_ID_MCP2517FD, HSPI, "CANFD",
                                      {GPIO_NUM_12, GPIO_NUM_34, GPIO_NUM_5, GPIO_NUM_18, GPIO_NUM_35, GPIO_NUM_NC,
                                       GPIO_NUM_NC},
                                      MCP2517_OSC_AUTODETECT, MCP2517_CLKODIV_DIV10, Mcp2517fdSettingsSlot::Fd1);
inline Rs485Port lilygo_rs485_port(Serial2, UART_NUM_2, {GPIO_NUM_22, GPIO_NUM_21, GPIO_NUM_NC});

inline GpioOutput lilygo_positive_contactor(GPIO_NUM_32, kLedcChannelPositive);
inline GpioOutput lilygo_negative_contactor(GPIO_NUM_33, kLedcChannelNegative);
inline GpioOutput lilygo_precharge(GPIO_NUM_25);
inline GpioOutput lilygo_bms_power_18(GPIO_NUM_18);
inline GpioOutput lilygo_bms_power_25(GPIO_NUM_25);
inline GpioOutput lilygo_second_battery_contactors(GPIO_NUM_15);

inline constexpr SwitchedOutputBinding kLilyGoOutputs[] = {
    {OutputRole::PositiveContactor, &lilygo_positive_contactor},
    {OutputRole::NegativeContactor, &lilygo_negative_contactor},
    {OutputRole::Precharge, &lilygo_precharge},
    {OutputRole::BmsPower, &lilygo_bms_power_18},
    {OutputRole::SecondBatteryContactors, &lilygo_second_battery_contactors},
};
inline constexpr SwitchedOutputBinding kLilyGoOutputsBmsPower25[] = {
    {OutputRole::PositiveContactor, &lilygo_positive_contactor},
    {OutputRole::NegativeContactor, &lilygo_negative_contactor},
    {OutputRole::Precharge, &lilygo_precharge},
    {OutputRole::BmsPower, &lilygo_bms_power_25},
    {OutputRole::SecondBatteryContactors, &lilygo_second_battery_contactors},
};

inline constexpr GpioOptionChoice kLilyGoGpioOpt2Choices[] = {
    {0, "Pin 18", nullptr, 0, 0},
    {1, "Pin 25", nullptr, 0, 1},
};

inline constexpr PinAssignment kLilyGoOpt3Enable05Pins[] = {
    {GpioPinRole::InverterContactorEnable, GPIO_NUM_5},
};
inline constexpr PinAssignment kLilyGoOpt3Enable33Pins[] = {
    {GpioPinRole::InverterContactorEnable, GPIO_NUM_33},
};
inline constexpr GpioOptionChoice kLilyGoGpioOpt3Choices[] = {
    {0, "Pin 5", kLilyGoOpt3Enable05Pins, 1, 0},
    {1, "Pin 33", kLilyGoOpt3Enable33Pins, 1, 0},
};

inline constexpr PinAssignment kLilyGoOpt4SdPins[] = {
    {GpioPinRole::SdMiso, GPIO_NUM_2},
    {GpioPinRole::SdMosi, GPIO_NUM_15},
    {GpioPinRole::SdSclk, GPIO_NUM_14},
    {GpioPinRole::SdCs, GPIO_NUM_13},
    {GpioPinRole::DisplaySda, kGpioPinNotConnected},
    {GpioPinRole::DisplayScl, kGpioPinNotConnected},
};
inline constexpr PinAssignment kLilyGoOpt4DisplayPins[] = {
    {GpioPinRole::SdMiso, kGpioPinNotConnected},
    {GpioPinRole::SdMosi, kGpioPinNotConnected},
    {GpioPinRole::SdSclk, kGpioPinNotConnected},
    {GpioPinRole::SdCs, kGpioPinNotConnected},
    {GpioPinRole::DisplaySda, GPIO_NUM_15},
    {GpioPinRole::DisplayScl, GPIO_NUM_14},
};
inline constexpr GpioOptionChoice kLilyGoGpioOpt4Choices[] = {
    {0, "µSD Card", kLilyGoOpt4SdPins, 6, 0},
#ifndef SMALL_FLASH_DEVICE
    {1, "I2C Display (SSD1306)", kLilyGoOpt4DisplayPins, 6, 0},
#else
    {1, nullptr, kLilyGoOpt4DisplayPins, 6, 0},
#endif
};

inline constexpr GpioOptionGroup kLilyGoGpioOptions[] = {
    {"GPIOOPT2", "BMS Power pin", kLilyGoGpioOpt2Choices, 2, 0},
    {"GPIOOPT3", "SMA enable pin", kLilyGoGpioOpt3Choices, 2, 0},
    {"GPIOOPT4", "µSD Slot", kLilyGoGpioOpt4Choices, 2, 0},
};
// switched_outputs() reads variant by group index, so group 0 must stay GPIOOPT2.
static_assert(kLilyGoGpioOptions[0].choices == kLilyGoGpioOpt2Choices);
static_assert(gpio_choices_valid(kLilyGoGpioOptions[0]));
static_assert(gpio_choices_valid(kLilyGoGpioOptions[1]));
static_assert(gpio_choices_valid(kLilyGoGpioOptions[2]));
static_assert(gpio_choices_cover_same_roles(kLilyGoGpioOptions[0]));
static_assert(gpio_choices_cover_same_roles(kLilyGoGpioOptions[1]));
static_assert(gpio_choices_cover_same_roles(kLilyGoGpioOptions[2]));
static_assert(gpio_groups_roles_disjoint(make_gpio_option_catalog(kLilyGoGpioOptions)));

// RS-485 transceiver enables and the 5 V boost, driven high at bring-up.
static constexpr gpio_num_t kLilyGoRs485EnPin = GPIO_NUM_17;
static constexpr gpio_num_t kLilyGoRs485SePin = GPIO_NUM_19;
static constexpr gpio_num_t kLilyGo5vEnPin = GPIO_NUM_16;

inline constexpr InterfaceDescriptor kLilyGoInterfaces[] = {
    {InterfaceType::Modbus, nullptr, comm_interface::Modbus, nullptr, &lilygo_rs485_port},
    {InterfaceType::Rs485, nullptr, comm_interface::RS485, nullptr, &lilygo_rs485_port},
    {InterfaceType::CanNative, nullptr, comm_interface::CanNative, &lilygo_can_bus},
    {InterfaceType::CanMcp2515, nullptr, comm_interface::CanAddonMcp2515, &lilygo_mcp2515_bus},
    {InterfaceType::CanMcp2517fd, nullptr, comm_interface::CanFdAddonMcp2518, &lilygo_mcp2517fd_bus},
};
static_assert(has_type(make_interface_list(kLilyGoInterfaces), InterfaceType::CanNative),
              "every board table needs a native CAN descriptor");

class LilyGoHal : public Esp32Hal {
 public:
  const char* name() { return "LilyGo T-CAN485"; }

  virtual void board_init() {
    if (!alloc_pins("RS485", kLilyGoRs485EnPin, kLilyGoRs485SePin, kLilyGo5vEnPin)) {
      return;
    }
    pinMode(kLilyGoRs485EnPin, OUTPUT);
    digitalWrite(kLilyGoRs485EnPin, HIGH);
    pinMode(kLilyGoRs485SePin, OUTPUT);
    digitalWrite(kLilyGoRs485SePin, HIGH);
    pinMode(kLilyGo5vEnPin, OUTPUT);
    digitalWrite(kLilyGo5vEnPin, HIGH);
  }

  // CHAdeMO support pin dependencies
  virtual gpio_num_t CHADEMO_PIN_2() { return GPIO_NUM_12; }
  virtual gpio_num_t CHADEMO_PIN_10() { return GPIO_NUM_5; }
  virtual gpio_num_t CHADEMO_PIN_7() { return GPIO_NUM_34; }
  virtual gpio_num_t CHADEMO_PIN_4() { return GPIO_NUM_35; }
  virtual gpio_num_t CHADEMO_LOCK() { return GPIO_NUM_18; }
  virtual gpio_num_t CHADEMO_CT_PIN() { return GPIO_NUM_15; }  // ADC2_CH3

  // Pins to be latched across a reset/OTA reboot (RTC-capable pins only): BMS_POWER can be GPIO25
  virtual std::vector<gpio_num_t> reset_hold_pins() { return {GPIO_NUM_25}; }

  // Automatic precharging
  virtual gpio_num_t HIA4V1_PIN() { return GPIO_NUM_25; }
  virtual gpio_num_t INVERTER_DISCONNECT_CONTACTOR_PIN() { return GPIO_NUM_32; }

  GpioOptionCatalog gpio_options() override { return make_gpio_option_catalog(kLilyGoGpioOptions); }

  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_PIN() {
    return pin_for(GpioPinRole::InverterContactorEnable, GPIO_NUM_NC);
  }

#ifdef SDCARD
  uint8_t SD_SPI_BUS() override { return VSPI; }
#endif  // SDCARD

  // SD card
  virtual gpio_num_t SD_MISO_PIN() { return pin_for(GpioPinRole::SdMiso, GPIO_NUM_NC); }
  virtual gpio_num_t SD_MOSI_PIN() { return pin_for(GpioPinRole::SdMosi, GPIO_NUM_NC); }
  virtual gpio_num_t SD_SCLK_PIN() { return pin_for(GpioPinRole::SdSclk, GPIO_NUM_NC); }
  virtual gpio_num_t SD_CS_PIN() { return pin_for(GpioPinRole::SdCs, GPIO_NUM_NC); }

  // LED
  virtual gpio_num_t LED_PIN() { return GPIO_NUM_4; }

  // Equipment stop pin
  virtual gpio_num_t EQUIPMENT_STOP_PIN() { return GPIO_NUM_35; }

  // Battery wake up pins
  virtual gpio_num_t WUP_PIN1() { return GPIO_NUM_25; }
  virtual gpio_num_t WUP_PIN2() { return GPIO_NUM_32; }

  // Momentary push-button that can be long-pressed at runtime to start the Wi-Fi AP.
  virtual gpio_num_t AP_BUTTON_PIN() { return GPIO_NUM_0; }

#ifndef SMALL_FLASH_DEVICE
  // i2c display
  virtual gpio_num_t DISPLAY_SDA_PIN() { return pin_for(GpioPinRole::DisplaySda, GPIO_NUM_NC); }
  virtual gpio_num_t DISPLAY_SCL_PIN() { return pin_for(GpioPinRole::DisplayScl, GPIO_NUM_NC); }
#endif  // SMALL_FLASH_DEVICE

  virtual SwitchedOutputList switched_outputs() {
    return gpio_output_variant(0) == 0 ? make_switched_output_list(kLilyGoOutputs)
                                       : make_switched_output_list(kLilyGoOutputsBmsPower25);
  }

  InterfaceList interfaces() { return make_interface_list(kLilyGoInterfaces); }
};

/* ----- Error checks below, don't change (can't be moved to separate file) ----- */
#ifndef HW_CONFIGURED
#define HW_CONFIGURED
#else
#error Multiple HW defined! Please select a single HW
#endif

#endif
