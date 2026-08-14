#ifndef WIRE_H_
#define WIRE_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>

class TwoWire {
 public:
  // Arduino endTransmission() status codes.
  static constexpr uint8_t kSuccess = 0;
  static constexpr uint8_t kOtherError = 4;

  struct Transmission {
    uint8_t address = 0;
    std::vector<uint8_t> bytes;
  };

  bool begin(int sda = -1, int scl = -1) {
    begun = true;
    return true;
  }
  void beginTransmission(uint8_t address) {
    current.address = address;
    current.bytes.clear();
  }
  size_t write(uint8_t value) {
    current.bytes.push_back(value);
    return 1;
  }
  uint8_t endTransmission() {
    if (fail_writes) {
      return kOtherError;
    }
    transmissions.push_back(current);
    return kSuccess;
  }

  std::vector<Transmission> transmissions;
  bool begun = false;
  bool fail_writes = false;

 private:
  Transmission current;
};

extern TwoWire Wire;

#endif
