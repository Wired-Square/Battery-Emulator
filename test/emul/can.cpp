#include "../../Software/src/communication/Transmitter.h"
#include "../../Software/src/communication/can/Mcp2515.h"
#include "../../Software/src/communication/can/Mcp2517fd.h"
#include "../../Software/src/communication/can/NativeTwai.h"
#include "../../Software/src/communication/can/comm_can.h"
#include "can_recorder.h"

static std::vector<CAN_frame> g_recorded;

std::vector<CAN_frame>& recorded_frames() {
  return g_recorded;
}

void reset_recorded_frames() {
  g_recorded.clear();
}

void transmit_can_frame_to_interface(const CAN_frame* tx_frame, const InterfaceDescriptor* interface) {
  g_recorded.push_back(*tx_frame);
}

void register_can_receiver(CanReceiver* receiver, const InterfaceDescriptor* interface, CanRole role, CAN_Speed speed,
                           CanSpeedMode mode) {
  if (interface != nullptr && interface->can_bus != nullptr) {
    interface->can_bus->register_receiver(receiver, role, speed, mode);
  }
}

bool change_can_speed(const InterfaceDescriptor* interface, CAN_Speed speed) {
  return true;
}

void stop_can() {}

void restart_can() {}

void register_transmitter(Transmitter* transmitter) {}

void dump_can_frame(CAN_frame& frame, const InterfaceDescriptor* interface, frameDirection msgDir) {}

void log_can_frame(const CAN_frame& frame, uint8_t log_id, frameDirection msgDir) {}

bool NativeTwai::init_hw() {
  return true;
}
void NativeTwai::receive() {}
bool NativeTwai::transmit_frame(const CAN_frame&) {
  return true;
}
bool NativeTwai::retune_hw(CAN_Speed) {
  return true;
}
void NativeTwai::stop_hw() {}
bool NativeTwai::restart_hw(CAN_Speed) {
  return true;
}

bool Mcp2515::init_hw() {
  return true;
}
void Mcp2515::receive() {}
bool Mcp2515::transmit_frame(const CAN_frame&) {
  return true;
}
bool Mcp2515::retune_hw(CAN_Speed) {
  return true;
}
void Mcp2515::stop_hw() {}
bool Mcp2515::restart_hw(CAN_Speed) {
  return true;
}

bool Mcp2517fd::init_hw() {
  return true;
}
void Mcp2517fd::receive() {}
bool Mcp2517fd::transmit_frame(const CAN_frame&) {
  return true;
}
bool Mcp2517fd::retune_hw(CAN_Speed) {
  return true;
}
void Mcp2517fd::stop_hw() {}
bool Mcp2517fd::restart_hw(CAN_Speed) {
  return true;
}
