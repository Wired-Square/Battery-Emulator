#ifndef TEST_FAKE_BATTERY_H
#define TEST_FAKE_BATTERY_H
#include "../datalayer/datalayer.h"
#include "BatterySlotContext.h"
#include "CanBattery.h"
#include "battery_command.h"

class TestFakeBattery : public CanBattery {
 public:
  TestFakeBattery(const BatterySlotContext& ctx) : CanBattery(ctx.can_interface) {
    datalayer_battery = ctx.datalayer;
    allows_contactor_closing = ctx.is_primary() ? ctx.contactor_flag : nullptr;
  }


  virtual void setup();
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);

  const std::vector<BatteryCommand>& get_commands() override { return commands_; }

 private:
  // Narrowing is safe: the descriptor bounds the value to the field's range.
  void set_fake_voltage(int32_t decivolts) { datalayer_battery->status.voltage_dV = static_cast<uint16_t>(decivolts); }

  std::vector<BatteryCommand> commands_{
      value_command(CMD_SET_FAKE_VOLTAGE, [this](int32_t decivolts) { set_fake_voltage(decivolts); }),
  };

  DATALAYER_BATTERY_TYPE* datalayer_battery;
  // If not null, this battery decides when the contactor can be closed and writes the value here.
  bool* allows_contactor_closing;

  static const int MAX_CELL_DEVIATION_MV = 9999;

  static const int NUMBER_OF_CELLS = 96;
  // Random spread applied on top of the evenly divided pack voltage, per cell
  static const int CELL_SPREAD_MV = 20;
  // Simulated balancing starts once the calculated SOC is above this level
  static const uint16_t BALANCING_START_SOC_PPTT = 8500;  // 85.00%
};

#endif
