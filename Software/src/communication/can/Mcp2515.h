#ifndef _MCP2515_H_
#define _MCP2515_H_

#include "CanBus.h"

class MCP2515_Lite;
class SPIClass;

inline constexpr uint32_t MCP2515_FREQ_AUTODETECT = 0;

struct Mcp2515Pins {
  gpio_num_t sck;
  gpio_num_t miso;
  gpio_num_t mosi;
  gpio_num_t cs;
  gpio_num_t irq;
  // Reset line; GPIO_NUM_NC when not wired.
  gpio_num_t rst;
};

// Spare SPI buses are VSPI/HSPI on the original ESP32 and HSPI/FSPI on every
// later target; each board names its bus explicitly.
class Mcp2515 : public CanBus {
 public:
  Mcp2515(uint8_t log_id, uint8_t spi_bus, const char* pin_owner, Mcp2515Pins pins, uint32_t quartz_hz)
      : CanBus(log_id), spi_bus_(spi_bus), pin_owner_(pin_owner), pins_(pins), quartz_frequency_(quartz_hz) {}
  void receive() override;
  bool transmit_frame(const CAN_frame& frame) override;

 protected:
  bool init_hw() override;
  bool retune_hw(CAN_Speed new_speed) override;
  void stop_hw() override;
  bool restart_hw(CAN_Speed new_speed) override;

 private:
  MCP2515_Lite* driver_ = nullptr;
  SPIClass* spi_ = nullptr;
  uint8_t spi_bus_;
  const char* pin_owner_;
  Mcp2515Pins pins_;
  uint32_t quartz_frequency_;
};

#endif
