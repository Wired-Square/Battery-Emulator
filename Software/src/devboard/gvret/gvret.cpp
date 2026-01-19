/**
 * GVRET TCP Server Implementation
 *
 * Binary-only GVRET protocol for SavvyCAN/CANdor compatibility.
 * Streams CAN frames to connected clients over TCP.
 */

#include "gvret.h"

#ifndef SMALL_FLASH_DEVICE

#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include "../../communication/can/comm_can.h"
#include "../../lib/mathieucarbou-AsyncTCPSock/src/AsyncTCP.h"
#include "../utils/logging.h"

// Settings (loaded from NVM)
bool gvret_enabled = false;
uint16_t gvret_port = GVRET_DEFAULT_PORT;

// Server state
static AsyncServer* gvret_server = nullptr;
static std::vector<AsyncClient*> gvret_clients;
static uint32_t gvret_start_time = 0;

// Client state tracking
struct ClientState {
  AsyncClient* client;
  bool binary_mode;
  uint8_t handshake_count;  // Count of 0xE7 bytes received
};
static std::vector<ClientState> client_states;

// Forward declarations
static void send_device_info(AsyncClient* client);
static void send_can_config(AsyncClient* client, uint8_t bus);
static void send_bus_count(AsyncClient* client);
static void send_time_sync(AsyncClient* client);
static void send_keepalive_response(AsyncClient* client);

/**
 * Find client state by client pointer
 */
static ClientState* find_client_state(AsyncClient* client) {
  for (auto& state : client_states) {
    if (state.client == client) {
      return &state;
    }
  }
  return nullptr;
}

/**
 * Encode CAN frame in GVRET binary format
 * Format: F1 00 timestamp(4) id(4) len|bus data[] 00
 */
static size_t encode_gvret_frame(uint8_t* buffer, const CAN_frame* frame, CAN_Interface interface) {
  size_t idx = 0;
  uint32_t timestamp = micros() - gvret_start_time;
  uint32_t id = frame->ID;

  // Set extended ID flag in bit 31
  if (frame->ext_ID) {
    id |= 0x80000000;
  }

  buffer[idx++] = GVRET_START_BYTE;
  buffer[idx++] = GVRET_CMD_CAN_FRAME;

  // Timestamp (4 bytes, little-endian)
  buffer[idx++] = timestamp & 0xFF;
  buffer[idx++] = (timestamp >> 8) & 0xFF;
  buffer[idx++] = (timestamp >> 16) & 0xFF;
  buffer[idx++] = (timestamp >> 24) & 0xFF;

  // ID (4 bytes, little-endian)
  buffer[idx++] = id & 0xFF;
  buffer[idx++] = (id >> 8) & 0xFF;
  buffer[idx++] = (id >> 16) & 0xFF;
  buffer[idx++] = (id >> 24) & 0xFF;

  // Bus (upper nibble) and DLC (lower nibble)
  uint8_t bus = static_cast<uint8_t>(interface);
  uint8_t dlc = frame->DLC > 8 ? 8 : frame->DLC;  // Cap at 8 for classic CAN
  buffer[idx++] = (bus << 4) | (dlc & 0x0F);

  // Data bytes
  for (uint8_t i = 0; i < dlc; i++) {
    buffer[idx++] = frame->data.u8[i];
  }

  // End marker
  buffer[idx++] = 0x00;

  return idx;
}

/**
 * Send device info response (command 0x07)
 */
static void send_device_info(AsyncClient* client) {
  uint8_t response[16];
  size_t idx = 0;

  response[idx++] = GVRET_START_BYTE;
  response[idx++] = GVRET_CMD_GET_DEVICE_INFO;

  // Device type: Custom value for Battery Emulator
  response[idx++] = 0x10;  // Device type

  // Number of CAN buses (native + possible addon)
  response[idx++] = 2;

  // Firmware version (1.0.0)
  response[idx++] = 1;  // Major
  response[idx++] = 0;  // Minor
  response[idx++] = 0;  // Build

  // Single wire mode supported
  response[idx++] = 0;  // Not supported

  // End marker
  response[idx++] = 0x00;

  if (client->connected() && client->canSend()) {
    client->write((const char*)response, idx);
  }
}

/**
 * Send CAN bus configuration (command 0x06)
 */
static void send_can_config(AsyncClient* client, uint8_t bus) {
  uint8_t response[16];
  size_t idx = 0;

  response[idx++] = GVRET_START_BYTE;
  response[idx++] = GVRET_CMD_GET_CAN_CONFIG;

  // Bus number
  response[idx++] = bus;

  // Speed (500kbps = 500000, little-endian 4 bytes)
  uint32_t speed = 500000;
  response[idx++] = speed & 0xFF;
  response[idx++] = (speed >> 8) & 0xFF;
  response[idx++] = (speed >> 16) & 0xFF;
  response[idx++] = (speed >> 24) & 0xFF;

  // Enabled flag
  response[idx++] = 1;

  // Listen only flag
  response[idx++] = 0;

  // End marker
  response[idx++] = 0x00;

  if (client->connected() && client->canSend()) {
    client->write((const char*)response, idx);
  }
}

/**
 * Send bus count response (command 0x0C)
 */
static void send_bus_count(AsyncClient* client) {
  uint8_t response[8];
  size_t idx = 0;

  response[idx++] = GVRET_START_BYTE;
  response[idx++] = GVRET_CMD_GET_BUS_COUNT;

  // Number of CAN buses
  response[idx++] = 2;

  // End marker
  response[idx++] = 0x00;

  if (client->connected() && client->canSend()) {
    client->write((const char*)response, idx);
  }
}

/**
 * Send time sync response (command 0x01)
 */
static void send_time_sync(AsyncClient* client) {
  uint8_t response[12];
  size_t idx = 0;
  uint32_t timestamp = micros() - gvret_start_time;

  response[idx++] = GVRET_START_BYTE;
  response[idx++] = GVRET_CMD_TIME_SYNC;

  // Timestamp (4 bytes, little-endian)
  response[idx++] = timestamp & 0xFF;
  response[idx++] = (timestamp >> 8) & 0xFF;
  response[idx++] = (timestamp >> 16) & 0xFF;
  response[idx++] = (timestamp >> 24) & 0xFF;

  // End marker
  response[idx++] = 0x00;

  if (client->connected() && client->canSend()) {
    client->write((const char*)response, idx);
  }
}

/**
 * Send keepalive response (command 0x09)
 */
static void send_keepalive_response(AsyncClient* client) {
  uint8_t response[8];
  size_t idx = 0;

  response[idx++] = GVRET_START_BYTE;
  response[idx++] = GVRET_CMD_KEEPALIVE;

  // Echo some data
  response[idx++] = 0xDE;
  response[idx++] = 0xAD;

  // End marker
  response[idx++] = 0x00;

  if (client->connected() && client->canSend()) {
    client->write((const char*)response, idx);
  }
}

/**
 * Process incoming data from client
 */
static void process_client_data(AsyncClient* client, uint8_t* data, size_t len) {
  ClientState* state = find_client_state(client);
  if (!state) {
    return;
  }

  for (size_t i = 0; i < len; i++) {
    uint8_t byte = data[i];

    // Handle binary mode handshake (E7 E7)
    if (byte == GVRET_BINARY_MODE_ENABLE) {
      state->handshake_count++;
      if (state->handshake_count >= 2) {
        state->binary_mode = true;
        state->handshake_count = 0;
        logging.println("GVRET: Client entered binary mode");
      }
      continue;
    }

    // Reset handshake counter on non-E7 byte
    state->handshake_count = 0;

    // Only process commands in binary mode
    if (!state->binary_mode) {
      continue;
    }

    // Look for command start byte
    if (byte == GVRET_START_BYTE && (i + 1) < len) {
      uint8_t cmd = data[i + 1];

      switch (cmd) {
        case GVRET_CMD_GET_DEVICE_INFO:
          send_device_info(client);
          break;

        case GVRET_CMD_GET_CAN_CONFIG:
          // Send config for both buses
          send_can_config(client, 0);
          send_can_config(client, 1);
          break;

        case GVRET_CMD_GET_BUS_COUNT:
          send_bus_count(client);
          break;

        case GVRET_CMD_TIME_SYNC:
          send_time_sync(client);
          break;

        case GVRET_CMD_KEEPALIVE:
          send_keepalive_response(client);
          break;

        default:
          // Unknown command, ignore
          break;
      }

      i++;  // Skip the command byte we just processed
    }
  }
}

// AsyncTCP callback handlers
static void onClientData(void* arg, AsyncClient* client, void* data, size_t len) {
  process_client_data(client, static_cast<uint8_t*>(data), len);
}

static void onClientDisconnect(void* arg, AsyncClient* client) {
  logging.printf("GVRET: Client disconnected: %s\n", client->remoteIP().toString().c_str());

  // Remove from client list
  for (auto it = gvret_clients.begin(); it != gvret_clients.end(); ++it) {
    if (*it == client) {
      gvret_clients.erase(it);
      break;
    }
  }

  // Remove client state
  for (auto it = client_states.begin(); it != client_states.end(); ++it) {
    if (it->client == client) {
      client_states.erase(it);
      break;
    }
  }

  delete client;
}

static void onClientError(void* arg, AsyncClient* client, int8_t error) {
  logging.printf("GVRET: Client error: %d\n", error);
}

static void onClientTimeout(void* arg, AsyncClient* client, uint32_t time) {
  logging.println("GVRET: Client timeout");
  client->close();
}

static void onNewClient(void* arg, AsyncClient* client) {
  if (gvret_clients.size() >= GVRET_MAX_CLIENTS) {
    logging.println("GVRET: Max clients reached, rejecting connection");
    client->close();
    delete client;
    return;
  }

  logging.printf("GVRET: Client connected: %s\n", client->remoteIP().toString().c_str());

  client->onData(onClientData, nullptr);
  client->onDisconnect(onClientDisconnect, nullptr);
  client->onError(onClientError, nullptr);
  client->onTimeout(onClientTimeout, nullptr);
  client->setNoDelay(true);
  client->setAckTimeout(5000);

  gvret_clients.push_back(client);

  // Initialize client state
  ClientState state = {client, false, 0};
  client_states.push_back(state);
}

// Public API implementation
bool init_gvret(void) {
  if (!gvret_enabled) {
    return false;
  }

  if (gvret_server != nullptr) {
    return true;  // Already initialized
  }

  gvret_server = new AsyncServer(gvret_port);
  gvret_server->onClient(onNewClient, nullptr);
  gvret_server->setNoDelay(true);
  gvret_server->begin();

  gvret_start_time = micros();

  logging.printf("GVRET: Server started on port %d\n", gvret_port);
  return true;
}

void gvret_loop(void) {
  // AsyncServer handles everything asynchronously
  // This function exists for potential future periodic maintenance
}

void stop_gvret(void) {
  if (gvret_server != nullptr) {
    // Close all client connections
    for (auto client : gvret_clients) {
      if (client->connected()) {
        client->close();
      }
      delete client;
    }
    gvret_clients.clear();
    client_states.clear();

    gvret_server->end();
    delete gvret_server;
    gvret_server = nullptr;

    logging.println("GVRET: Server stopped");
  }
}

void gvret_send_can_frame(const CAN_frame* frame, CAN_Interface interface, frameDirection direction) {
  if (gvret_clients.empty()) {
    return;
  }

  uint8_t buffer[GVRET_TX_BUFFER_SIZE];
  size_t len = encode_gvret_frame(buffer, frame, interface);

  // Send to all connected clients in binary mode
  for (auto& state : client_states) {
    if (state.binary_mode && state.client->connected() && state.client->canSend()) {
      state.client->write((const char*)buffer, len);
    }
  }
}

uint8_t gvret_client_count(void) {
  uint8_t count = 0;
  for (auto& state : client_states) {
    if (state.binary_mode) {
      count++;
    }
  }
  return count;
}

#else  // SMALL_FLASH_DEVICE stubs

bool gvret_enabled = false;
uint16_t gvret_port = GVRET_DEFAULT_PORT;

bool init_gvret(void) {
  return false;
}
void gvret_loop(void) {}
void stop_gvret(void) {}
void gvret_send_can_frame(const CAN_frame* frame, CAN_Interface interface, frameDirection direction) {
  (void)frame;
  (void)interface;
  (void)direction;
}
uint8_t gvret_client_count(void) {
  return 0;
}

#endif  // SMALL_FLASH_DEVICE
