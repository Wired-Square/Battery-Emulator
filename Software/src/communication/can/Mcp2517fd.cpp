#include <Arduino.h>

#include <algorithm>

#include "../../datalayer/datalayer.h"
#include "../../devboard/hal/hal.h"
#include "../../devboard/utils/logging.h"
#include "../../lib/pierremolinaro-ACAN2517FD/ACAN2517FD.h"
#include "Mcp2517fd.h"
#include "comm_can.h"

namespace {

// ACAN2517FD's "no pin attached" sentinel.
constexpr uint8_t ACAN2517FD_NO_PIN = 255;
constexpr uint32_t MCP2517_OSC_20MHZ = 20000000;

// Bounded by the two settings slots (Fd1/Fd2) — a board table can bind at
// most two FD chips, each on one bus.
constexpr size_t kMaxMcp2517fdInstances = 2;

// A same-bus id shares one SPIClass regardless of which chip initialises
// first; mismatched pins on one bus id is a board-definition error.
struct SpiBusEntry {
  uint8_t bus;
  gpio_num_t sck;
  SPIClass* spi;
};
SpiBusEntry spi_registry[kMaxMcp2517fdInstances] = {};

SPIClass* spi_for_bus(uint8_t bus, const char* owner, gpio_num_t sck, gpio_num_t sdo, gpio_num_t sdi) {
  for (SpiBusEntry& entry : spi_registry) {
    if (entry.spi != nullptr && entry.bus == bus) {
      if (entry.sck != sck) {
        DEBUG_PRINTF("MCP2517 SPI bus %u pin mismatch\n", bus);
        return nullptr;
      }
      return entry.spi;
    }
  }
  if (!esp32hal->alloc_pins(owner, sck, sdo, sdi)) {
    return nullptr;
  }
  for (SpiBusEntry& entry : spi_registry) {
    if (entry.spi == nullptr) {
      entry.bus = bus;
      entry.sck = sck;
      entry.spi = new SPIClass(bus);
      entry.spi->begin(sck, sdo, sdi);
      return entry.spi;
    }
  }
  return nullptr;
}

// ACAN2517FD::begin takes a captureless function pointer; slots bind the
// board-owned instances to trampolines at init time.
Mcp2517fd* isr_instances[kMaxMcp2517fdInstances] = {};
void isr_slot0() {
  isr_instances[0]->run_driver_isr();
}
void isr_slot1() {
  isr_instances[1]->run_driver_isr();
}
using Mcp2517fdIsr = void (*)();
constexpr Mcp2517fdIsr kIsrTrampolines[kMaxMcp2517fdInstances] = {isr_slot0, isr_slot1};

Mcp2517fdIsr claim_isr_slot(Mcp2517fd* instance) {
  for (size_t i = 0; i < kMaxMcp2517fdInstances; i++) {
    if (isr_instances[i] == instance || isr_instances[i] == nullptr) {
      isr_instances[i] = instance;
      return kIsrTrampolines[i];
    }
  }
  return nullptr;
}

}  // namespace

void Mcp2517fd::run_driver_isr() {
  driver_->isr();
}

bool Mcp2517fd::init_hw() {
  SPIClass* spi = spi_for_bus(spi_bus_, pin_owner_, pins_.sck, pins_.sdo, pins_.sdi);
  if (spi == nullptr) {
    return false;
  }

  if (!esp32hal->alloc_pins(pin_owner_, pins_.cs)) {
    return false;
  }
  if (pins_.irq != GPIO_NUM_NC) {
    if (!esp32hal->alloc_pins(pin_owner_, pins_.irq)) {
      return false;
    }
  } else {
    if (!esp32hal->alloc_pins(pin_owner_, pins_.int0, pins_.int1)) {
      return false;
    }
  }

  driver_ = new ACAN2517FD(pins_.cs, *spi, pins_.irq != GPIO_NUM_NC ? pins_.irq : ACAN2517FD_NO_PIN,
                           pins_.int0 != GPIO_NUM_NC ? pins_.int0 : ACAN2517FD_NO_PIN,
                           pins_.int1 != GPIO_NUM_NC ? pins_.int1 : ACAN2517FD_NO_PIN);

  if (slot_ == Mcp2517fdSettingsSlot::Fd1) {
    logging.println("CAN FD add-on (ESP32+MCP2517) selected");
  } else {
    logging.println("CAN FD add-on 2 (ESP32+MCP2517) selected");
  }

  isr_ = claim_isr_slot(this);
  if (isr_ == nullptr) {
    return false;
  }

  return begin_at(speed());
}

bool Mcp2517fd::begin_at(CAN_Speed new_speed) {
  if (settings_ != nullptr) {
    delete settings_;
  }

  ACAN2517FDSettings::Oscillator osc_freq =
      (osc_hz_ == MCP2517_OSC_AUTODETECT
           ? ACAN2517FDSettings::OSC_AUTODETECT
           : (osc_hz_ == MCP2517_OSC_20MHZ ? ACAN2517FDSettings::OSC_20MHz : ACAN2517FDSettings::OSC_40MHz));
  auto bitRate = (int)new_speed * 1000UL;
  settings_ = new ACAN2517FDSettings(osc_freq, bitRate, DataBitRateFactor::x4);

  if (clkodiv_ != MCP2517_CLKODIV_UNSET) {
    // Clock output divider (some hardware clocks the second CAN FD add-on off it)
    settings_->mCLKOPin = static_cast<ACAN2517FDSettings::CLKOpin>(clkodiv_);
  }

  const bool as_can = slot_ == Mcp2517fdSettingsSlot::Fd1 ? use_canfd_as_can : use_canfd2_as_can;
  settings_->mRequestedMode = as_can ? ACAN2517FDSettings::Normal20B : ACAN2517FDSettings::NormalFD;

  const uint32_t errorCode = driver_->begin(*settings_, isr_);
  driver_->poll();
  if (errorCode != 0) {
    if (slot_ == Mcp2517fdSettingsSlot::Fd1) {
      logging.print("CAN-FD Configuration error 0x");
    } else {
      logging.print("CAN-FD 2 Configuration error 0x");
    }
    logging.println(errorCode, HEX);
    set_event(EVENT_CANMCP2518FD_INIT_FAILURE, (uint8_t)errorCode);
    return false;
  }
  return true;
}

bool Mcp2517fd::retune_hw(CAN_Speed new_speed) {
  driver_->end();
  return begin_at(new_speed);
}

void Mcp2517fd::receive() {
  CANFDMessage message;
  int count = 0;
  while (driver_->available() && count++ < CAN_MAX_RX_FRAMES_PER_POLL) {
    driver_->receive(message);

    CAN_frame rx_frame;
    rx_frame.ID = message.id;
    rx_frame.ext_ID = message.ext;
    rx_frame.DLC = message.len;
    rx_frame.FD = (message.type == CANFDMessage::CANFD_NO_BIT_RATE_SWITCH ||
                   message.type == CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH);
    memcpy(rx_frame.data.u8, message.data, std::min(rx_frame.DLC, (uint8_t)sizeof(rx_frame.data.u8)));
    dispatch_frame(rx_frame);
  }
}

bool Mcp2517fd::transmit_frame(const CAN_frame& tx_frame) {
  CANFDMessage message;
  if (tx_frame.FD) {
    message.type = CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH;
  } else {
    message.type = CANFDMessage::CAN_DATA;
  }
  message.id = tx_frame.ID;
  message.ext = tx_frame.ext_ID;
  message.len = tx_frame.DLC;
  memcpy(message.data, tx_frame.data.u8, std::min(tx_frame.DLC, (uint8_t)sizeof(message.data)));

  if (!driver_->tryToSend(message)) {
    datalayer.system.info.can_2518_send_fail = true;
    return false;
  }
  return true;
}

void Mcp2517fd::stop_hw() {
  driver_->end();
}

bool Mcp2517fd::restart_hw(CAN_Speed new_speed) {
  return begin_at(new_speed);
}
