#ifndef __HW_DFROBOT_EDGE101_H__
#define __HW_DFROBOT_EDGE101_H__

#include "../../communication/can/NativeTwai.h"
#include "../../communication/rs485/Rs485Port.h"
#include "hal.h"

/*
DFRobot Edge101 IOT Controller (SKU DFR0886)
ESP32-WROOM-32E, 16 MB flash, no PSRAM. USB-serial via CH9102F.
Isolated CAN (TJA1050), isolated RS485 (TPT75176H), microSD (SPI),
IP101GRI Ethernet PHY (RMII), PCF8563T RTC (I2C 0x51).
Pin map: see arduino-esp32 PR #12742 variants/dfrobot_edge101/pins_arduino.h.
*/

// Native CAN via TJA1050 (galvanically isolated)
inline NativeTwai dfrobot_can_bus(CAN_LOG_ID_NATIVE, TwaiController::Twai0,
                                  {GPIO_NUM_32, GPIO_NUM_35, GPIO_NUM_NC});
// RS485 via TPT75176H (galvanically isolated), DE and /RE are tied together
inline Rs485Port dfrobot_rs485_port(Serial2, UART_NUM_2, {GPIO_NUM_17, GPIO_NUM_36, GPIO_NUM_16});

inline constexpr InterfaceDescriptor kDFRobotEdge101Interfaces[] = {
    {InterfaceType::Modbus, nullptr, comm_interface::Modbus, nullptr, &dfrobot_rs485_port},
    {InterfaceType::Rs485, nullptr, comm_interface::RS485, nullptr, &dfrobot_rs485_port},
    {InterfaceType::CanNative, nullptr, comm_interface::CanNative, &dfrobot_can_bus},
};
static_assert(has_type(make_interface_list(kDFRobotEdge101Interfaces), InterfaceType::CanNative),
              "every board table needs a native CAN descriptor");

class DFRobotEdge101Hal : public Esp32Hal {
 public:
  const char* name() { return "DFRobot Edge101 IOT Controller"; }

  virtual bool configure_rs485_half_duplex(Rs485Port& port) {
    return rs485_configure_half_duplex(port.uart_num(), port.pins());
  }

  InterfaceList interfaces() { return make_interface_list(kDFRobotEdge101Interfaces); }

  // microSD (SPI)
#ifdef SDCARD
  uint8_t SD_SPI_BUS() override { return VSPI; }
  virtual gpio_num_t SD_MOSI_PIN() { return GPIO_NUM_12; }
  virtual gpio_num_t SD_MISO_PIN() { return GPIO_NUM_39; }
  virtual gpio_num_t SD_SCLK_PIN() { return GPIO_NUM_14; }
  virtual gpio_num_t SD_CS_PIN() { return GPIO_NUM_5; }
#endif  // SDCARD

  // User LED
  virtual gpio_num_t LED_PIN() { return GPIO_NUM_15; }

  // User button — GPIO 38 is input-only on ESP32, no internal pull-up available; board has external pull-up
  virtual gpio_num_t AP_BUTTON_PIN() { return GPIO_NUM_38; }

};

#define HalClass DFRobotEdge101Hal

/* ----- Error checks below, don't change (can't be moved to separate file) ----- */
#ifndef HW_CONFIGURED
#define HW_CONFIGURED
#else
#error Multiple HW defined! Please select a single HW
#endif

#endif
