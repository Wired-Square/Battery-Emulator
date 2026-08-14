#ifndef JAGUAR_IPACE_BATTERY_H
#define JAGUAR_IPACE_BATTERY_H

#include "BatterySlotContext.h"
#include "CanBattery.h"

class JaguarIpaceBattery : public CanBattery {
 public:
  JaguarIpaceBattery(const BatterySlotContext& ctx) : CanBattery(ctx.can_interface) { datalayer_battery = ctx.datalayer; }
  virtual void setup(void);
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);

 private:
  DATALAYER_BATTERY_TYPE* datalayer_battery;
  static const int MAX_PACK_VOLTAGE_DV = 4546;  //5000 = 500.0V
  static const int MIN_PACK_VOLTAGE_DV = 3370;
  static const int MAX_CELL_DEVIATION_MV = 250;
  static const int MAX_CELL_VOLTAGE_MV = 4250;  //Battery is put into emergency stop if one cell goes over this value
  static const int MIN_CELL_VOLTAGE_MV = 2700;  //Battery is put into emergency stop if one cell goes below this value
};

#endif
