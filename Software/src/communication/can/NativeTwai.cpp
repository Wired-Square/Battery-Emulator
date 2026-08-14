#include <Arduino.h>

#include <esp_private/periph_ctrl.h>

#include "../../datalayer/datalayer.h"
#include "../../devboard/hal/hal.h"
#include "../../devboard/utils/logging.h"
#include "../../lib/pierremolinaro-acan-esp32/ACAN_ESP32.h"
#include "NativeTwai.h"

namespace {

struct ControllerWiring {
  ACAN_ESP32* driver;  // nullptr = controller absent on this target
  periph_module_t reset_module;
  const char* pin_allocator;
  const char* log_name;
};

// The vendored driver exposes the second controller (and the TWAI0/TWAI1
// module split) only on the ESP32-C6.
#ifdef CONFIG_IDF_TARGET_ESP32C6
constexpr ControllerWiring kControllers[] = {
    {&ACAN_ESP32::can, PERIPH_TWAI0_MODULE, "CAN", "Native Can"},
    {&ACAN_ESP32::can1, PERIPH_TWAI1_MODULE, "CAN2", "Native Can 2"},
};
#else
constexpr ControllerWiring kControllers[] = {
    {&ACAN_ESP32::can, PERIPH_TWAI_MODULE, "CAN", "Native Can"},
    {nullptr, PERIPH_TWAI_MODULE, "CAN2", "Native Can 2"},
};
#endif
static_assert(sizeof(kControllers) / sizeof(kControllers[0]) == static_cast<size_t>(TwaiController::Twai1) + 1,
              "one wiring row per TwaiController id");

const ControllerWiring& wiring_for(TwaiController controller) {
  return kControllers[static_cast<size_t>(controller)];
}

}  // namespace

bool NativeTwai::init_hw() {
  const ControllerWiring& wiring = wiring_for(controller_);
  if (wiring.driver == nullptr) {
    // A descriptor bound this bus on a target without the controller: a
    // board-definition error, so fail init rather than alias controller 0.
    return false;
  }

  if (pins_.se != GPIO_NUM_NC) {
    if (!esp32hal->alloc_pins(wiring.pin_allocator, pins_.se)) {
      return false;
    }
    pinMode(pins_.se, OUTPUT);
    digitalWrite(pins_.se, LOW);
  }

  if (!esp32hal->alloc_pins(wiring.pin_allocator, pins_.tx, pins_.rx)) {
    return false;
  }

  const uint32_t errorCode = begin_driver(speed(), pins_.tx, pins_.rx);
  if (errorCode != 0) {
    logging.print("Error ");
    logging.print(wiring.log_name);
    logging.print(": 0x");
    logging.println(errorCode, HEX);
    return false;
  }

  logging.print(wiring.log_name);
  logging.println(" ok");
  logging.print("Bit Rate prescaler: ");
  logging.println(settings_->mBitRatePrescaler);
  logging.print("Time Segment 1:     ");
  logging.println(settings_->mTimeSegment1);
  logging.print("Time Segment 2:     ");
  logging.println(settings_->mTimeSegment2);
  logging.print("RJW:                ");
  logging.println(settings_->mRJW);
  logging.print("Triple Sampling:    ");
  logging.println(settings_->mTripleSampling ? "yes" : "no");
  logging.print("Actual bit rate:    ");
  logging.print(settings_->actualBitRate());
  logging.println(" bit/s");
  logging.print("Exact bit rate ?    ");
  logging.println(settings_->exactBitRate() ? "yes" : "no");
  logging.print("Sample point:       ");
  logging.print(settings_->samplePointFromBitStart());
  logging.println("%");
  return true;
}

// Reinitialisable: some batteries switch bus speed at runtime.
uint32_t NativeTwai::begin_driver(CAN_Speed new_speed, gpio_num_t tx_pin, gpio_num_t rx_pin) {
  // TODO: check whether this is necessary? It seems to help with
  // reinitialization.
  periph_module_reset(wiring_for(controller_).reset_module);

  if (settings_ != nullptr) {
    delete settings_;
  }

  // A new settings object each time (the constructor does the bitrate calcs)
  settings_ = new ACAN_ESP32_Settings((int)new_speed * 1000UL);
  settings_->mRequestedCANMode = ACAN_ESP32_Settings::NormalMode;
  settings_->mTxPin = tx_pin;
  settings_->mRxPin = rx_pin;

  return wiring_for(controller_).driver->begin(*settings_);
}

void NativeTwai::receive() {
  CANMessage frame;
  const ControllerWiring& wiring = wiring_for(controller_);
  if (wiring.driver->available()) {
    if (wiring.driver->receive(frame)) {
      CAN_frame rx_frame;
      rx_frame.ID = frame.id;
      rx_frame.ext_ID = frame.ext;
      rx_frame.DLC = frame.len;
      rx_frame.FD = false;
      for (uint8_t i = 0; i < frame.len && i < sizeof(frame.data); i++) {
        rx_frame.data.u8[i] = frame.data[i];
      }
      dispatch_frame(rx_frame);
    }
  }
}

bool NativeTwai::transmit_frame(const CAN_frame& tx_frame) {
  CANMessage frame;
  frame.id = tx_frame.ID;
  frame.ext = tx_frame.ext_ID;
  frame.len = tx_frame.DLC;
  for (uint8_t i = 0; i < frame.len; i++) {
    frame.data[i] = tx_frame.data.u8[i];
  }
  if (!wiring_for(controller_).driver->tryToSend(frame)) {
    datalayer.system.info.can_native_send_fail = true;
    return false;
  }
  return true;
}

bool NativeTwai::change_speed(CAN_Speed new_speed) {
  if (settings_ == nullptr) {
    return false;
  }
  // Pin args are read before begin_driver replaces settings_.
  const uint32_t errorCode = begin_driver(new_speed, settings_->mTxPin, settings_->mRxPin);
  if (errorCode != 0) {
    logging.print("Error ");
    logging.print(wiring_for(controller_).log_name);
    logging.print(": 0x");
    logging.println(errorCode, HEX);
    return false;
  }
  return true;
}

void NativeTwai::stop() {
  wiring_for(controller_).driver->end();
}

void NativeTwai::restart() {
  if (settings_ != nullptr) {
    wiring_for(controller_).driver->begin(*settings_);
  }
}
