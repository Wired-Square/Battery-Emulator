#ifndef DALY_BMS_H
#define DALY_BMS_H

#include "BatterySlotContext.h"
#include "RS485Battery.h"

class DalyBms : public RS485Battery {
 public:
  DalyBms(const BatterySlotContext& ctx) : RS485Battery(ctx.can_interface) { datalayer_battery = ctx.datalayer; }
  void setup();
  void update_values();
  void transmit_rs485(unsigned long currentMillis);
  void receive();

  static DeviceSettingList settings();

 private:
  DATALAYER_BATTERY_TYPE* datalayer_battery;
  void decode_packet(uint8_t command, uint8_t data[8]);
  int baud_rate() { return 9600; }
};

#endif
