#include "SPI.h"

std::vector<std::vector<uint8_t>> SPIClass::frames;
std::deque<std::vector<uint8_t>> SPIClass::rx_queue;

void SPIClass::transferBytes(const uint8_t* tx, uint8_t* rx, uint32_t size) {
  frames.emplace_back(tx, tx + size);
  if (!rx_queue.empty()) {
    const std::vector<uint8_t>& response = rx_queue.front();
    for (uint32_t i = 0; i < size && i < response.size(); i++) {
      rx[i] = response[i];
    }
    rx_queue.pop_front();
    return;
  }
  // Default idle response carries valid odd parity (bit 0 set for an
  // otherwise-zero frame) so unscripted reads don't fail the parity check.
  for (uint32_t i = 0; i < size; i++) {
    rx[i] = 0;
  }
  if (size == 3) {
    rx[2] = 0x01;
  }
}
