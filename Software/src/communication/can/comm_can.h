#ifndef _COMM_CAN_H_
#define _COMM_CAN_H_

#include "../../devboard/utils/types.h"
#include "../../system_settings.h"
#include "CanBus.h"
#include "../../devboard/hal/interface_descriptor.h"

extern bool use_canfd_as_can;
extern bool use_canfd2_as_can;
extern uint16_t user_selected_CAN_ID_cutoff_filter;

void dump_can_frame(CAN_frame& frame, const InterfaceDescriptor* interface, frameDirection msgDir);
void transmit_can_frame_to_interface(const CAN_frame* tx_frame, const InterfaceDescriptor* interface);

// Shared frame-log sink (USB print, web dump, SD buffer). log_id is the
// SavvyCAN bus slot (CAN_LOG_ID_* in CanBus.h).
void log_can_frame(const CAN_frame& frame, uint8_t log_id, frameDirection msgDir);

// Format CAN logs to the given buffer. Returns the number of bytes written, or
// 0 if the buffer is too small.
// Null-terminates the buffer if len > 0, but the return value does not include the NUL.
size_t format_can_frame(char* buffer, size_t len, const CAN_frame& frame, uint8_t log_id, frameDirection msgDir);

class CanReceiver;

typedef struct {
  const InterfaceDescriptor* battery[kMaxBatterySlots];
  const InterfaceDescriptor* inverter;
  const InterfaceDescriptor* charger;
  const InterfaceDescriptor* shunt;
} CAN_Configuration;

extern volatile CAN_Configuration can_config;

void register_can_receiver(CanReceiver* receiver, const InterfaceDescriptor* interface, CanRole role, CAN_Speed speed,
                           CanSpeedMode mode = CanSpeedMode::Fixed);

inline constexpr uint8_t CAN_CONFIG_EVENT_BUS_SHIFT = 4;
constexpr uint8_t can_config_invalid_event_data(uint8_t log_id, CanResolveError reason) {
  return static_cast<uint8_t>(log_id << CAN_CONFIG_EVENT_BUS_SHIFT) | static_cast<uint8_t>(reason);
}

// Event-data byte for interface-scoped events; preserves the legacy
// interface numbering via the bus log id.
inline uint8_t can_event_interface_id(const InterfaceDescriptor* interface) {
  return (interface != nullptr && interface->can_bus != nullptr) ? interface->can_bus->log_id() : CAN_LOG_ID_NONE;
}

/**
 * @brief Initializes all CAN interfaces requested earlier by other modules (see register_can_receiver)
 *
 * @param[in] void
 *
 * @return true if CAN interfaces were initialized successfully, false otherwise.
 */
bool init_CAN();

/**
 * @brief Receive CAN messages from all interfaces. Respective CanReceivers are called.
 *
 * @param[in] void
 *
 * @return void
 */
void receive_can();

// Stop/pause CAN communication for all interfaces
void stop_can();

// Restart CAN communication for all interfaces
void restart_can();

// Change the speed of the CAN interface. Returns true if successful.
bool change_can_speed(const InterfaceDescriptor* interface, CAN_Speed speed);

#endif
