#ifndef __HW_LILYGO2CAN_H__
#define __HW_LILYGO2CAN_H__

#include <SPI.h>

#include "../../communication/can/Mcp2515.h"
#include "../../communication/can/Mcp2517fd.h"
#include "../../communication/can/NativeTwai.h"
#include "../../communication/rs485/Rs485Port.h"
#include "hal.h"
#include "GpioOutput.h"

#include "../utils/types.h"

/*
The 2CAN has four GPIOs on the top (43/44 and 1/2 on each of the SH-1mm
connectors) and 21 on the bottom (on the 2x13 header). GPIO35-37 aren't usable
if opi PSRAM is enabled.

43:RS485_TX
44:RS485_RX

1:WUP1/BMS_POWER/ESTOP
2:WUP2/LED

3.3V                   GND
  5V                   GND
 x35:LED                39:INT
  38:SCK                42:SDI
 x37:SDO                41:nCS
 x36:ESTOP              40
  16                     4:BAT3_CTRS
  15                     5:BAT2_CTRS
  45                    48:POS_CTR
  47:                   21:PRECHARGE
  14:HV_INV_DIS/[WUP2]  17:NEG_CTR
  18:HV_PRE/[WUP1]     GND
  46:INV_CTR_EN          3:BMS_POWER

*/

inline NativeTwai lilygo2can_can_bus(CAN_LOG_ID_NATIVE, TwaiController::Twai0, {GPIO_NUM_7, GPIO_NUM_6, GPIO_NUM_NC});
inline Mcp2515 lilygo2can_mcp2515_bus(CAN_LOG_ID_MCP2515, HSPI, "CAN",
                                      {GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_11, GPIO_NUM_10, GPIO_NUM_8, GPIO_NUM_9},
                                      16000000);
inline Mcp2517fd lilygo2can_builtin_fd_bus(CAN_LOG_ID_MCP2517FD, FSPI, "CANFD",
                                           {GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_11, GPIO_NUM_10, GPIO_NUM_NC, GPIO_NUM_9,
                                            GPIO_NUM_3},
                                           40000000, MCP2517_CLKODIV_DIV10, Mcp2517fdSettingsSlot::Fd1);
inline Mcp2517fd lilygo2can_addon_fd_bus(CAN_LOG_ID_MCP2517FD, FSPI, "CANFD",
                                         {GPIO_NUM_38, GPIO_NUM_37, GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_39, GPIO_NUM_NC,
                                          GPIO_NUM_NC},
                                         MCP2517_OSC_AUTODETECT, MCP2517_CLKODIV_DIV10, Mcp2517fdSettingsSlot::Fd1);
inline Mcp2517fd lilygo2can_addon_fd2_bus(CAN_LOG_ID_MCP2517FD_2, HSPI, "CANFD2",
                                          {GPIO_NUM_38, GPIO_NUM_37, GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_39, GPIO_NUM_NC,
                                           GPIO_NUM_NC},
                                          MCP2517_OSC_AUTODETECT, MCP2517_CLKODIV_UNSET, Mcp2517fdSettingsSlot::Fd2);
// Note: these are used by the bootloader UART, so there'll be some bootloader
// chatter sent down the RS485 bus when the chip starts up. Hopefully this
// won't matter (it'll be 115200 baud, so much higher than the normal Modbus
// 9600 baud) - if it's a problem we can burn fuses to disable it.
inline Rs485Port lilygo2can_rs485_port(Serial2, UART_NUM_2, {GPIO_NUM_43, GPIO_NUM_44, GPIO_NUM_NC});

inline GpioOutput lilygo2can_positive_contactor(GPIO_NUM_48, kLedcChannelPositive);
inline GpioOutput lilygo2can_negative_contactor(GPIO_NUM_17, kLedcChannelNegative);
inline GpioOutput lilygo2can_precharge(GPIO_NUM_21);
inline GpioOutput lilygo2can_bms_power_3(GPIO_NUM_3);
inline GpioOutput lilygo2can_bms_power_2(GPIO_NUM_2);
// GPIO3 is used as INT1 on a T-2CAN FD (MCP2518), so use GPIO45 instead.
inline GpioOutput lilygo2can_bms_power_45(GPIO_NUM_45);
inline GpioOutput lilygo2can_second_battery_contactors(GPIO_NUM_5);
inline GpioOutput lilygo2can_third_battery_contactors(GPIO_NUM_4);

inline constexpr SwitchedOutputBinding kLilyGo2CanOutputs[] = {
    {OutputRole::PositiveContactor, &lilygo2can_positive_contactor},
    {OutputRole::NegativeContactor, &lilygo2can_negative_contactor},
    {OutputRole::Precharge, &lilygo2can_precharge},
    {OutputRole::BmsPower, &lilygo2can_bms_power_3},
    {OutputRole::SecondBatteryContactors, &lilygo2can_second_battery_contactors},
    {OutputRole::ThirdBatteryContactors, &lilygo2can_third_battery_contactors},
};
inline constexpr SwitchedOutputBinding kLilyGo2CanOutputsFd[] = {
    {OutputRole::PositiveContactor, &lilygo2can_positive_contactor},
    {OutputRole::NegativeContactor, &lilygo2can_negative_contactor},
    {OutputRole::Precharge, &lilygo2can_precharge},
    {OutputRole::BmsPower, &lilygo2can_bms_power_45},
    {OutputRole::SecondBatteryContactors, &lilygo2can_second_battery_contactors},
    {OutputRole::ThirdBatteryContactors, &lilygo2can_third_battery_contactors},
};
inline constexpr SwitchedOutputBinding kLilyGo2CanOutputsEstopBms[] = {
    {OutputRole::PositiveContactor, &lilygo2can_positive_contactor},
    {OutputRole::NegativeContactor, &lilygo2can_negative_contactor},
    {OutputRole::Precharge, &lilygo2can_precharge},
    {OutputRole::BmsPower, &lilygo2can_bms_power_2},
    {OutputRole::SecondBatteryContactors, &lilygo2can_second_battery_contactors},
    {OutputRole::ThirdBatteryContactors, &lilygo2can_third_battery_contactors},
};

// Non-default choices relocate WUP1/WUP2 off the top port rather than disabling them.
inline constexpr PinAssignment kLilyGo2CanOpt1WupPins[] = {
    {GpioPinRole::Wup1, GPIO_NUM_1},
    {GpioPinRole::Wup2, GPIO_NUM_2},
    {GpioPinRole::EquipmentStop, GPIO_NUM_36},
    {GpioPinRole::DisplaySda, kGpioPinNotConnected},
    {GpioPinRole::DisplayScl, kGpioPinNotConnected},
};
inline constexpr PinAssignment kLilyGo2CanOpt1DisplayPins[] = {
    {GpioPinRole::Wup1, GPIO_NUM_18},
    {GpioPinRole::Wup2, GPIO_NUM_14},
    {GpioPinRole::EquipmentStop, GPIO_NUM_36},
    {GpioPinRole::DisplaySda, GPIO_NUM_1},
    {GpioPinRole::DisplayScl, GPIO_NUM_2},
};
inline constexpr PinAssignment kLilyGo2CanOpt1EstopPins[] = {
    {GpioPinRole::Wup1, GPIO_NUM_18},
    {GpioPinRole::Wup2, GPIO_NUM_14},
    {GpioPinRole::EquipmentStop, GPIO_NUM_1},
    {GpioPinRole::DisplaySda, kGpioPinNotConnected},
    {GpioPinRole::DisplayScl, kGpioPinNotConnected},
};
inline constexpr GpioOptionChoice kLilyGo2CanGpioOpt1Choices[] = {
    {0, "WUP1 / WUP2", kLilyGo2CanOpt1WupPins, 5, 0},
#ifndef SMALL_FLASH_DEVICE
    {1, "I2C Display (SSD1306)", kLilyGo2CanOpt1DisplayPins, 5, 0},
#else
    {1, nullptr, kLilyGo2CanOpt1DisplayPins, 5, 0},
#endif
    {2, "E-Stop / BMS Power", kLilyGo2CanOpt1EstopPins, 5, 1},
};
inline constexpr GpioOptionGroup kLilyGo2CanGpioOptions[] = {
    {"GPIOOPT1", "Configurable port", kLilyGo2CanGpioOpt1Choices, 3, 0},
};
static_assert(gpio_choices_valid(kLilyGo2CanGpioOptions[0]));
static_assert(gpio_choices_cover_same_roles(kLilyGo2CanGpioOptions[0]));
static_assert(gpio_groups_roles_disjoint(make_gpio_option_catalog(kLilyGo2CanGpioOptions)));

inline constexpr InterfaceDescriptor kLilyGo2CanInterfaces[] = {
    {InterfaceType::Modbus, "Modbus (Add-on)", comm_interface::Modbus, nullptr, &lilygo2can_rs485_port},
    {InterfaceType::Rs485, "RS485 (Add-on)", comm_interface::RS485, nullptr, &lilygo2can_rs485_port},
    {InterfaceType::CanNative, "CAN B (Native)", comm_interface::CanNative, &lilygo2can_can_bus},
    {InterfaceType::CanMcp2515, "CAN A (MCP2515)", comm_interface::CanAddonMcp2515, &lilygo2can_mcp2515_bus},
    {InterfaceType::CanMcp2517fd, nullptr, comm_interface::CanFdAddonMcp2518, &lilygo2can_addon_fd_bus},
};
inline constexpr InterfaceDescriptor kLilyGo2CanFdInterfaces[] = {
    {InterfaceType::Modbus, "Modbus (Add-on)", comm_interface::Modbus, nullptr, &lilygo2can_rs485_port},
    {InterfaceType::Rs485, "RS485 (Add-on)", comm_interface::RS485, nullptr, &lilygo2can_rs485_port},
    {InterfaceType::CanNative, "CAN B (Native)", comm_interface::CanNative, &lilygo2can_can_bus},
    {InterfaceType::CanMcp2517fd, "CAN FD A (MCP2518)", comm_interface::CanFdAddonMcp2518, &lilygo2can_builtin_fd_bus},
    {InterfaceType::CanMcp2517fd, nullptr, comm_interface::CanFdAddonMcp2518_2, &lilygo2can_addon_fd2_bus},
};
static_assert(has_type(make_interface_list(kLilyGo2CanInterfaces), InterfaceType::CanNative),
              "every board table needs a native CAN descriptor");
static_assert(has_type(make_interface_list(kLilyGo2CanFdInterfaces), InterfaceType::CanNative),
              "every board table needs a native CAN descriptor");

class LilyGo2CANHal : public Esp32Hal {
 public:
  const char* name() { return "LilyGo T_2CAN"; }

  // Pins to be latched across a reset/OTA reboot (RTC-capable pins only): BMS_POWER is either GPIO2, GPIO3, or GPIO45
  virtual std::vector<gpio_num_t> reset_hold_pins() { return {GPIO_NUM_2, is_fd() ? GPIO_NUM_45 : GPIO_NUM_3}; }

  // Automatic precharging
  virtual gpio_num_t HIA4V1_PIN() { return GPIO_NUM_18; }
  virtual gpio_num_t INVERTER_DISCONNECT_CONTACTOR_PIN() { return GPIO_NUM_14; }

  // SMA CAN contactor pin
  virtual gpio_num_t INVERTER_CONTACTOR_ENABLE_PIN() { return GPIO_NUM_46; }

  // LED
  virtual gpio_num_t LED_PIN() { return GPIO_NUM_35; }
  // CHAdeMO support pin dependencies
  virtual gpio_num_t CHADEMO_PIN_2() { return GPIO_NUM_16; }
  virtual gpio_num_t CHADEMO_PIN_10() { return GPIO_NUM_15; }
  virtual gpio_num_t CHADEMO_PIN_7() { return GPIO_NUM_47; }
  virtual gpio_num_t CHADEMO_PIN_4() { return GPIO_NUM_4; }
  virtual gpio_num_t CHADEMO_LOCK() { return GPIO_NUM_40; }
  virtual gpio_num_t CHADEMO_CT_PIN() { return GPIO_NUM_5; }  // ADC1_CH4

  GpioOptionCatalog gpio_options() override { return make_gpio_option_catalog(kLilyGo2CanGpioOptions); }

#ifndef SMALL_FLASH_DEVICE
  // i2c display
  virtual gpio_num_t DISPLAY_SDA_PIN() { return pin_for(GpioPinRole::DisplaySda, GPIO_NUM_NC); }
  virtual gpio_num_t DISPLAY_SCL_PIN() { return pin_for(GpioPinRole::DisplayScl, GPIO_NUM_NC); }
#endif  // SMALL_FLASH_DEVICE

  // Equipment stop pin
  virtual gpio_num_t EQUIPMENT_STOP_PIN() { return pin_for(GpioPinRole::EquipmentStop, GPIO_NUM_NC); }

  // Battery wake up pins
  virtual gpio_num_t WUP_PIN1() { return pin_for(GpioPinRole::Wup1, GPIO_NUM_NC); }
  virtual gpio_num_t WUP_PIN2() { return pin_for(GpioPinRole::Wup2, GPIO_NUM_NC); }

  // Momentary push-button that can be long-pressed at runtime to start the Wi-Fi AP.
  virtual gpio_num_t AP_BUTTON_PIN() { return GPIO_NUM_0; }

  virtual SwitchedOutputList switched_outputs() {
    if (gpio_output_variant(0) != 0) {
      return make_switched_output_list(kLilyGo2CanOutputsEstopBms);
    }
    return is_fd() ? make_switched_output_list(kLilyGo2CanOutputsFd)
                   : make_switched_output_list(kLilyGo2CanOutputs);
  }

  InterfaceList interfaces() {
    return is_fd() ? make_interface_list(kLilyGo2CanFdInterfaces) : make_interface_list(kLilyGo2CanInterfaces);
  }

  bool is_fd() {
    // Return true if this is the MCP2518FD variant of the T-2CAN.
    // Takes 2ms on first call, will be fast thereafter.

    if (have_detected) {
      return has_mcp2518fd;
    }

    has_mcp2518fd = detect_fd();
    have_detected = true;
    return has_mcp2518fd;
  }

 private:
  bool have_detected = false;
  bool has_mcp2518fd = false;

  bool detect_fd() {
    // Detect whether this is the T-2CAN with MCP2515 (non-FD) or MCP2518FD (FD)
    // by assuming it is a MCP2515, attempting to reset and reading CANSTAT.

    const int IO9 = GPIO_NUM_9;
    const int CS = GPIO_NUM_10;
    const int MISO = GPIO_NUM_13;
    const int MOSI = GPIO_NUM_11;
    const int SCK = GPIO_NUM_12;

    pinMode(IO9, INPUT_PULLDOWN);        // Reset (if MCP2515)
    vTaskDelay(1 / portTICK_PERIOD_MS);  // Wait for reset
    pinMode(IO9, INPUT_PULLUP);          // Deassert reset

    pinMode(CS, OUTPUT);
    digitalWrite(CS, HIGH);              // Ensure CS is high to start with
    vTaskDelay(1 / portTICK_PERIOD_MS);  // Wait for chip to settle

    SPISettings _settings(100000, MSBFIRST, SPI_MODE0);
    SPIClass _spi(HSPI);
    _spi.begin(SCK, MISO, MOSI);

    // Read MCP2515 CANSTAT register
    const uint8_t tx_data[] = {0x03, 0x0E, 0x00};
    uint8_t rx_data[3] = {0};

    _spi.beginTransaction(_settings);
    digitalWrite(CS, LOW);
    _spi.transferBytes(tx_data, rx_data, 3);
    digitalWrite(CS, HIGH);
    _spi.endTransaction();

    pinMode(CS, INPUT);  // Set CS back to high impedance

    _spi.end();

    // MCP2515 will return 0x80, MCP2518FD will return something else.
    bool detected_fd = rx_data[2] != 0x80;

    if (detected_fd) {
      logging.printf("2CAN FD detected (ret=%d)\n", rx_data[2]);
    } else {
      logging.printf("2CAN non-FD detected (ret=%d)\n", rx_data[2]);
    }

    return detected_fd;
  }
};

/* ----- Error checks below, don't change (can't be moved to separate file) ----- */
#ifndef HW_CONFIGURED
#define HW_CONFIGURED
#else
#error Multiple HW defined! Please select a single HW
#endif

#endif
