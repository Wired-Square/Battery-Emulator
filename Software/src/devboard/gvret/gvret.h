/**
 * GVRET TCP Server for Battery Emulator
 *
 * Provides a GVRET-compatible TCP server for streaming CAN frames to
 * SavvyCAN, CANdor, and other compatible applications.
 *
 * Protocol: Binary-only GVRET protocol
 * Default port: 23 (configurable)
 *
 * Usage:
 * 1. Enable GVRET in web settings
 * 2. Connect SavvyCAN/CANdor via Network Connection (GVRET)
 * 3. CAN frames will stream in real-time
 */

#ifndef __GVRET_H__
#define __GVRET_H__

#include <Arduino.h>
#include "../../devboard/utils/types.h"

// GVRET Protocol Constants
#define GVRET_START_BYTE 0xF1
#define GVRET_BINARY_MODE_ENABLE 0xE7

// GVRET Commands
#define GVRET_CMD_CAN_FRAME 0x00
#define GVRET_CMD_TIME_SYNC 0x01
#define GVRET_CMD_GET_DIG_IN 0x02
#define GVRET_CMD_GET_ANALOG 0x03
#define GVRET_CMD_SET_DIG_OUT 0x04
#define GVRET_CMD_SET_CAN_CONFIG 0x05
#define GVRET_CMD_GET_CAN_CONFIG 0x06
#define GVRET_CMD_GET_DEVICE_INFO 0x07
#define GVRET_CMD_SET_SWCAN 0x08
#define GVRET_CMD_KEEPALIVE 0x09
#define GVRET_CMD_SET_SYSTYPE 0x0A
#define GVRET_CMD_ECHO 0x0B
#define GVRET_CMD_GET_BUS_COUNT 0x0C

// Configuration
#define GVRET_DEFAULT_PORT 23
#define GVRET_MAX_CLIENTS 4
#define GVRET_TX_BUFFER_SIZE 32

// Settings (stored in NVM)
extern bool gvret_enabled;
extern uint16_t gvret_port;

/**
 * @brief Initialize GVRET TCP server
 * @return true if initialization successful
 */
bool init_gvret(void);

/**
 * @brief GVRET main loop function (currently unused, async handles everything)
 */
void gvret_loop(void);

/**
 * @brief Stop and cleanup GVRET server
 */
void stop_gvret(void);

/**
 * @brief Stream a CAN frame to all connected clients
 * @param frame The CAN frame to stream
 * @param interface The CAN interface the frame was received/sent on
 * @param direction RX or TX direction
 */
void gvret_send_can_frame(const CAN_frame* frame, CAN_Interface interface, frameDirection direction);

/**
 * @brief Get number of connected clients
 * @return Number of active client connections
 */
uint8_t gvret_client_count(void);

#endif  // __GVRET_H__
