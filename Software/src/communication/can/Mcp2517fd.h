#ifndef _MCP2517FD_H_
#define _MCP2517FD_H_

#include "CanBus.h"

class ACAN2517FD;
class ACAN2517FDSettings;

inline constexpr uint32_t MCP2517_OSC_AUTODETECT = 0;
// MCP2517 CLKO divider register values; UNSET skips the register write,
// leaving the library default (divide by 10).
inline constexpr int MCP2517_CLKODIV_DIV1 = 0b00;
inline constexpr int MCP2517_CLKODIV_DIV10 = 0b11;
inline constexpr int MCP2517_CLKODIV_UNSET = -1;

// Which pair of NVM settings (CANFDASCAN/CANFD2ASCAN and their events) the
// chip obeys — a settings binding, not a wiring fact.
enum class Mcp2517fdSettingsSlot : uint8_t { Fd1, Fd2 };

struct Mcp2517fdPins {
  gpio_num_t sck;  // same-bus siblings carry identical sck/sdo/sdi so either init order creates the bus
  gpio_num_t sdo;  // chip data-out = MCU MISO
  gpio_num_t sdi;  // chip data-in = MCU MOSI
  gpio_num_t cs;
  gpio_num_t irq;  // single-INT wiring; GPIO_NUM_NC when int0/int1 are wired
  gpio_num_t int0;
  gpio_num_t int1;
};

class Mcp2517fd : public CanBus {
 public:
  Mcp2517fd(uint8_t log_id, uint8_t spi_bus, const char* pin_owner, Mcp2517fdPins pins, uint32_t osc_hz, int clkodiv,
            Mcp2517fdSettingsSlot slot)
      : CanBus(log_id),
        spi_bus_(spi_bus),
        pin_owner_(pin_owner),
        pins_(pins),
        osc_hz_(osc_hz),
        clkodiv_(clkodiv),
        slot_(slot) {}
  void receive() override;
  bool transmit_frame(const CAN_frame& frame) override;
  void run_driver_isr();

 protected:
  bool init_hw() override;
  bool retune_hw(CAN_Speed new_speed) override;
  void stop_hw() override;
  bool restart_hw(CAN_Speed new_speed) override;

 private:
  bool begin_at(CAN_Speed new_speed);
  ACAN2517FD* driver_ = nullptr;
  ACAN2517FDSettings* settings_ = nullptr;
  uint8_t spi_bus_;
  const char* pin_owner_;
  Mcp2517fdPins pins_;
  uint32_t osc_hz_;
  int clkodiv_;
  Mcp2517fdSettingsSlot slot_;
  void (*isr_)() = nullptr;
};

#endif
