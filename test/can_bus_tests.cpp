#include <gtest/gtest.h>

#include "../Software/src/communication/can/CanBus.h"
#include "../Software/src/communication/can/CanReceiver.h"

#include "Arduino.h"

namespace {

class FakeBus : public CanBus {
 public:
  FakeBus() : CanBus(CAN_LOG_ID_NONE) {}
  void receive() override {}
  bool transmit_frame(const CAN_frame&) override { return true; }
  void inject(CAN_frame& frame) { dispatch_frame(frame); }
  CAN_Speed requested_speed() { return speed(); }
  bool init_result = true;

 protected:
  bool init_hw() override { return init_result; }
};

class CountingReceiver : public CanReceiver {
 public:
  void receive_can_frame(CAN_frame* frame) override {
    received++;
    last_id = frame->ID;
  }
  int received = 0;
  uint32_t last_id = 0;
};

}  // namespace

TEST(CanBusTest, RegistrationMakesBusActive) {
  FakeBus bus;
  CountingReceiver receiver;
  EXPECT_FALSE(bus.has_receivers());
  bus.register_receiver(&receiver, CAN_Speed::CAN_SPEED_500KBPS);
  EXPECT_TRUE(bus.has_receivers());
}

TEST(CanBusTest, FirstRegisteredSpeedWins) {
  // Matches the retired multimap behaviour: init used the first
  // registration's speed for the interface.
  FakeBus bus;
  CountingReceiver r1, r2;
  bus.register_receiver(&r1, CAN_Speed::CAN_SPEED_250KBPS);
  bus.register_receiver(&r2, CAN_Speed::CAN_SPEED_500KBPS);
  EXPECT_EQ(bus.requested_speed(), CAN_Speed::CAN_SPEED_250KBPS);
}

TEST(CanBusTest, SpeedDefaultsTo500k) {
  FakeBus bus;
  EXPECT_EQ(bus.requested_speed(), CAN_Speed::CAN_SPEED_500KBPS);
}

TEST(CanBusTest, DispatchFansOutToAllReceivers) {
  FakeBus bus;
  CountingReceiver r1, r2;
  bus.register_receiver(&r1, CAN_Speed::CAN_SPEED_500KBPS);
  bus.register_receiver(&r2, CAN_Speed::CAN_SPEED_500KBPS);
  CAN_frame frame = {};
  frame.ID = 0x123;
  bus.inject(frame);
  EXPECT_EQ(r1.received, 1);
  EXPECT_EQ(r2.received, 1);
  EXPECT_EQ(r1.last_id, 0x123u);
  EXPECT_EQ(r2.last_id, 0x123u);
}

TEST(CanBusTest, InitOutcomeIsRecorded) {
  FakeBus bus;
  EXPECT_FALSE(bus.initialized());
  EXPECT_TRUE(bus.init());
  EXPECT_TRUE(bus.initialized());
  bus.init_result = false;
  EXPECT_FALSE(bus.init());
  EXPECT_FALSE(bus.initialized());
}

TEST(CanBusTest, NeverReceivedBusIsInactive) {
  FakeBus bus;
  EXPECT_FALSE(bus.recently_received(50));
}

TEST(CanBusTest, DispatchMarksRecentActivity) {
  FakeBus bus;
  CountingReceiver receiver;
  bus.register_receiver(&receiver, CAN_Speed::CAN_SPEED_500KBPS);
  set_millis64(1000);
  CAN_frame frame = {};
  frame.ID = 0x123;
  bus.inject(frame);
  EXPECT_TRUE(bus.recently_received(50));
}

TEST(CanBusTest, ActivityExpiresAfterTheHoldWindow) {
  FakeBus bus;
  CountingReceiver receiver;
  bus.register_receiver(&receiver, CAN_Speed::CAN_SPEED_500KBPS);
  set_millis64(2000);
  CAN_frame frame = {};
  bus.inject(frame);
  set_millis64(2049);
  EXPECT_TRUE(bus.recently_received(50));
  set_millis64(2051);
  EXPECT_FALSE(bus.recently_received(50));
}
