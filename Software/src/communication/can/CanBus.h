#ifndef _CAN_BUS_H_
#define _CAN_BUS_H_

#include <stddef.h>
#include <stdint.h>

#include <soc/gpio_num.h>
#include <vector>

#include "../../devboard/utils/types.h"

class CanReceiver;

enum class CAN_Speed {
  CAN_SPEED_100KBPS = 100,
  CAN_SPEED_125KBPS = 125,
  CAN_SPEED_200KBPS = 200,
  CAN_SPEED_250KBPS = 250,
  CAN_SPEED_500KBPS = 500,
  CAN_SPEED_800KBPS = 800,
  CAN_SPEED_1000KBPS = 1000
};

enum class CanRole : uint8_t { Battery, Inverter, Charger, Shunt };

enum class CanSpeedMode : uint8_t { Fixed, Variable };

struct CanDemand {
  CanReceiver* receiver;
  CanRole role;
  CAN_Speed speed;
  CanSpeedMode mode;
};

enum class CanResolveError : uint8_t { None, Unresolved, SpeedConflict, VariableShared, MultipleBatteries };

// Frame-log/event slots preserve the retired legacy interface numbering so
// SavvyCAN logs and event data stay comparable across firmware versions.
// Slot 1 (the retired CAN-FD-native alias) is deliberately unassigned: the
// physical MCP2517FD it aliased logs as slot 3. Ids from
// CAN_LOG_ID_BOARD_BASE up are board-assigned in the board's own header:
// unique within that board, stable across its firmware versions.
inline constexpr uint8_t CAN_LOG_ID_NATIVE = 0;
inline constexpr uint8_t CAN_LOG_ID_MCP2515 = 2;
inline constexpr uint8_t CAN_LOG_ID_MCP2517FD = 3;
inline constexpr uint8_t CAN_LOG_ID_MCP2517FD_2 = 4;
inline constexpr uint8_t CAN_LOG_ID_NONE = 5;
inline constexpr uint8_t CAN_LOG_ID_BOARD_BASE = 6;

inline constexpr int CAN_MAX_RX_FRAMES_PER_POLL = 16;

// One physical CAN controller. Receiver registration and frame routing are
// keyed by these objects; boards bind interface descriptors to them.
class CanBus {
 public:
  explicit CanBus(uint8_t log_id) : log_id_(log_id) {}

  bool init() {
    resolution_ = resolve();
    if (resolution_ != CanResolveError::None) {
      return false;
    }
    initialized_ = init_hw();
    return initialized_;
  }
  virtual void receive() = 0;
  virtual bool transmit_frame(const CAN_frame& frame) = 0;
  bool change_speed(CAN_Speed speed) {
    if (!initialized_ || stopped_) {
      return false;
    }
    return retune_hw(speed);
  }
  void stop() {
    if (!initialized_) {
      return;
    }
    stopped_ = true;
    stop_hw();
  }
  void restart() {
    if (!initialized_ || !stopped_) {
      return;
    }
    stopped_ = !restart_hw(speed_);
  }

  void register_receiver(CanReceiver* receiver, CanRole role, CAN_Speed speed,
                         CanSpeedMode mode = CanSpeedMode::Fixed);
  bool has_receivers() const { return !demands_.empty(); }
  bool initialized() const { return initialized_; }
  CanResolveError resolution() const { return resolution_; }
  uint8_t log_id() const { return log_id_; }
  bool recently_received(uint32_t hold_ms) const;

 protected:
  virtual bool init_hw() = 0;
  virtual bool retune_hw(CAN_Speed speed) = 0;
  virtual void stop_hw() = 0;
  virtual bool restart_hw(CAN_Speed speed) = 0;
  void dispatch_frame(CAN_frame& frame);
  CAN_Speed speed() const { return speed_; }

 private:
  CanResolveError resolve();
  std::vector<CanDemand> demands_;
  CAN_Speed speed_{};
  CanResolveError resolution_ = CanResolveError::Unresolved;
  uint8_t log_id_;
  bool initialized_ = false;
  bool stopped_ = false;
  uint32_t last_rx_ms_ = 0;
};

#endif
