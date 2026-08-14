#pragma once

#include <cstdint>
#include <deque>
#include <vector>

// Arbitrary bus indices
#define HSPI 1
#define VSPI 2
#define FSPI 3

#define MSBFIRST 1
#define SPI_MODE0 0

class SPISettings {
 public:
  SPISettings() {}
  SPISettings(uint32_t clock, uint8_t bit_order, uint8_t data_mode) {}
};

class SPIClass {
 public:
  explicit SPIClass(uint8_t bus = HSPI) {}
  void begin(int sck = -1, int miso = -1, int mosi = -1, int ss = -1) {}
  void end() {}
  void beginTransaction(SPISettings settings) {}
  void endTransaction() {}
  void transferBytes(const uint8_t* tx, uint8_t* rx, uint32_t size);

  // Shared capture/script state: tests drive one device at a time.
  static std::vector<std::vector<uint8_t>> frames;
  static std::deque<std::vector<uint8_t>> rx_queue;
};
