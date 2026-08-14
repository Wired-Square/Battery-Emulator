#include <Arduino.h>

#include "../../datalayer/datalayer.h"
#include "../../devboard/hal/hal.h"
#include "../../devboard/utils/logging.h"
#include "../../lib/mcp2515_lite/mcp2515_lite.h"
#include "Mcp2515.h"
#include "utils.h"

namespace {
// The event data byte carries no diagnostic; it is frozen at 1 for
// event-log comparability across firmware versions.
constexpr uint8_t MCP2515_INIT_FAILURE_EVENT_DATA = 1;
}  // namespace

bool Mcp2515::init_hw() {
  if (!esp32hal->alloc_pins(pin_owner_, pins_.cs, pins_.irq, pins_.sck, pins_.miso, pins_.mosi)) {
    return false;
  }

  logging.println("Dual CAN Bus (ESP32+MCP2515) selected");

  if (pins_.rst != GPIO_NUM_NC) {
    pinMode(pins_.rst, OUTPUT);
    digitalWrite(pins_.rst, HIGH);
    delay(100);
    digitalWrite(pins_.rst, LOW);
    delay(100);
    digitalWrite(pins_.rst, HIGH);
    delay(100);
  }

  spi_ = new SPIClass(spi_bus_);
  spi_->begin(pins_.sck, pins_.miso, pins_.mosi);
  driver_ = new MCP2515_Lite(*spi_, pins_.cs, pins_.irq);

  if (quartz_frequency_ == MCP2515_FREQ_AUTODETECT) {
    quartz_frequency_ = driver_->autodetectOscillatorFrequency();
  }

  if (!driver_->begin({(int)speed() * 1000UL, quartz_frequency_})) {
    logging.println("MCP2515 CAN init failed");
    set_event(EVENT_CANMCP2515_INIT_FAILURE, MCP2515_INIT_FAILURE_EVENT_DATA);
    return false;
  }
  logging.println("MCP2515 CAN ok");
  return true;
}

void Mcp2515::receive() {
  MCP2515_Lite_Frame rx_frame;
  CAN_frame full_frame;

  int count = 0;
  while (count++ < CAN_MAX_RX_FRAMES_PER_POLL && driver_->receiveFrame(rx_frame)) {
    copy_mcp2515_lite_frame_to_can_frame(rx_frame, full_frame);
    dispatch_frame(full_frame);
  }
}

bool Mcp2515::transmit_frame(const CAN_frame& tx_frame) {
  MCP2515_Lite_Frame mcp2515_frame;
  copy_can_frame_to_mcp2515_lite_frame(tx_frame, mcp2515_frame);

  if (!driver_->sendFrame(mcp2515_frame)) {
    datalayer.system.info.can_2515_send_fail = true;
    return false;
  }
  return true;
}

bool Mcp2515::change_speed(CAN_Speed new_speed) {
  if (driver_ == nullptr) {
    return false;
  }
  driver_->changeSpeed({(int)new_speed * 1000UL, quartz_frequency_});
  return true;
}

void Mcp2515::stop() {
  if (driver_ != nullptr) {
    driver_->pause(true);
  }
}

void Mcp2515::restart() {
  if (driver_ != nullptr) {
    driver_->pause(false);
  }
}
