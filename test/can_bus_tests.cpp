#include <gtest/gtest.h>

#include "../Software/src/communication/can/CanBus.h"
#include "../Software/src/communication/can/CanReceiver.h"
#include "../Software/src/communication/can/comm_can.h"

#include "Arduino.h"

namespace {

class FakeBus : public CanBus {
 public:
  FakeBus() : CanBus(CAN_LOG_ID_NONE) {}
  void receive() override {}
  bool transmit_frame(const CAN_frame&) override { return true; }
  void inject(CAN_frame& frame) { dispatch_frame(frame); }
  CAN_Speed resolved_speed() { return speed(); }
  bool init_result = true;
  bool restart_result = true;
  int init_hw_calls = 0;
  int retune_calls = 0;
  int stop_calls = 0;
  int restart_calls = 0;
  CAN_Speed last_retune{};
  CAN_Speed last_restart{};

 protected:
  bool init_hw() override {
    init_hw_calls++;
    return init_result;
  }
  bool retune_hw(CAN_Speed new_speed) override {
    retune_calls++;
    last_retune = new_speed;
    return true;
  }
  void stop_hw() override { stop_calls++; }
  bool restart_hw(CAN_Speed new_speed) override {
    restart_calls++;
    last_restart = new_speed;
    return restart_result;
  }
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
  bus.register_receiver(&receiver, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS);
  EXPECT_TRUE(bus.has_receivers());
}

TEST(CanBusTest, AgreedFixedDemandsResolveRegardlessOfOrder) {
  FakeBus bus;
  CountingReceiver charger, battery;
  bus.register_receiver(&charger, CanRole::Charger, CAN_Speed::CAN_SPEED_250KBPS);
  bus.register_receiver(&battery, CanRole::Battery, CAN_Speed::CAN_SPEED_250KBPS);
  EXPECT_TRUE(bus.init());
  EXPECT_EQ(bus.resolution(), CanResolveError::None);
  EXPECT_EQ(bus.resolved_speed(), CAN_Speed::CAN_SPEED_250KBPS)
      << "resolution is a function of the demand set, not of who registered first";
}

TEST(CanBusTest, DisagreeingFixedSpeedsFaultWithoutTouchingHardware) {
  FakeBus bus;
  CountingReceiver charger, battery;
  bus.register_receiver(&charger, CanRole::Charger, CAN_Speed::CAN_SPEED_500KBPS);
  bus.register_receiver(&battery, CanRole::Battery, CAN_Speed::CAN_SPEED_250KBPS);
  EXPECT_FALSE(bus.init());
  EXPECT_EQ(bus.resolution(), CanResolveError::SpeedConflict)
      << "a 250k battery sharing an interface with a 500k charger is invalid configuration, not arbitration";
  EXPECT_EQ(bus.init_hw_calls, 0) << "no controller may be programmed with an unvalidated bitrate";
  EXPECT_FALSE(bus.initialized());
}

TEST(CanBusTest, LoneVariableDemandResolvesAtItsDeclaredSpeed) {
  FakeBus bus;
  CountingReceiver battery;
  bus.register_receiver(&battery, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS, CanSpeedMode::Variable);
  EXPECT_TRUE(bus.init());
  EXPECT_EQ(bus.resolved_speed(), CAN_Speed::CAN_SPEED_500KBPS);
}

TEST(CanBusTest, VariableDemandWithAnyCompanyIsRefused) {
  FakeBus bus;
  CountingReceiver battery, inverter;
  bus.register_receiver(&battery, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS, CanSpeedMode::Variable);
  bus.register_receiver(&inverter, CanRole::Inverter, CAN_Speed::CAN_SPEED_500KBPS);
  EXPECT_FALSE(bus.init());
  EXPECT_EQ(bus.resolution(), CanResolveError::VariableShared)
      << "a variable-speed device owns its bus: its wake dips would deafen any co-resident even at a matching "
         "nominal speed";
  EXPECT_EQ(bus.init_hw_calls, 0);
}

TEST(CanBusTest, SecondBatteryOnOneBusIsRefused) {
  FakeBus bus;
  CountingReceiver first, second;
  bus.register_receiver(&first, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS);
  bus.register_receiver(&second, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS);
  EXPECT_FALSE(bus.init());
  EXPECT_EQ(bus.resolution(), CanResolveError::MultipleBatteries)
      << "no driver tolerates two packs on one bus; overlapping IDs would decode one pack twice and can transmit "
         "conflicting contactor frames";
  EXPECT_EQ(bus.init_hw_calls, 0);
}

TEST(CanBusTest, InverterAndChargerNeverConflictByRole) {
  FakeBus bus;
  CountingReceiver battery, inverter, charger, shunt;
  bus.register_receiver(&battery, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS);
  bus.register_receiver(&inverter, CanRole::Inverter, CAN_Speed::CAN_SPEED_500KBPS);
  bus.register_receiver(&charger, CanRole::Charger, CAN_Speed::CAN_SPEED_500KBPS);
  bus.register_receiver(&shunt, CanRole::Shunt, CAN_Speed::CAN_SPEED_500KBPS);
  EXPECT_TRUE(bus.init()) << "only a second battery is a role conflict; other roles share freely at one speed";
}

TEST(CanBusTest, EmptyDemandSetStaysUnresolved) {
  FakeBus bus;
  EXPECT_FALSE(bus.init());
  EXPECT_EQ(bus.resolution(), CanResolveError::Unresolved);
  EXPECT_EQ(bus.init_hw_calls, 0);
}

TEST(CanBusTest, ChangeSpeedIsBlockedWhileStopped) {
  FakeBus bus;
  CountingReceiver battery;
  bus.register_receiver(&battery, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS, CanSpeedMode::Variable);
  ASSERT_TRUE(bus.init());
  bus.stop();
  EXPECT_FALSE(bus.change_speed(CAN_Speed::CAN_SPEED_100KBPS))
      << "a retune during a safety pause would silently re-begin the paused driver";
  EXPECT_EQ(bus.retune_calls, 0);
  bus.restart();
  EXPECT_TRUE(bus.change_speed(CAN_Speed::CAN_SPEED_100KBPS));
  EXPECT_EQ(bus.last_retune, CAN_Speed::CAN_SPEED_100KBPS);
}

TEST(CanBusTest, RestartReturnsToTheResolvedSpeed) {
  FakeBus bus;
  CountingReceiver battery;
  bus.register_receiver(&battery, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS, CanSpeedMode::Variable);
  ASSERT_TRUE(bus.init());
  ASSERT_TRUE(bus.change_speed(CAN_Speed::CAN_SPEED_100KBPS));
  bus.stop();
  bus.restart();
  EXPECT_EQ(bus.restart_calls, 1);
  EXPECT_EQ(bus.last_restart, CAN_Speed::CAN_SPEED_500KBPS)
      << "a safety pause during a wake dip must not strand the bus at the dip speed";
}

TEST(CanBusTest, ChargerAndShuntOnlyBusResolvesAtTheirDeclaredSpeed) {
  FakeBus bus;
  CountingReceiver charger, shunt;
  bus.register_receiver(&charger, CanRole::Charger, CAN_Speed::CAN_SPEED_500KBPS);
  bus.register_receiver(&shunt, CanRole::Shunt, CAN_Speed::CAN_SPEED_500KBPS);
  EXPECT_TRUE(bus.init()) << "a bus with no battery is still a valid configuration";
  EXPECT_EQ(bus.resolved_speed(), CAN_Speed::CAN_SPEED_500KBPS);
}

TEST(CanBusTest, FailedRestartKeepsTheBusStopped) {
  FakeBus bus;
  CountingReceiver battery;
  bus.register_receiver(&battery, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS, CanSpeedMode::Variable);
  ASSERT_TRUE(bus.init());
  bus.stop();
  bus.restart_result = false;
  bus.restart();
  EXPECT_FALSE(bus.change_speed(CAN_Speed::CAN_SPEED_100KBPS))
      << "a bus whose re-begin failed must not accept retunes as if it were running";
  bus.restart_result = true;
  bus.restart();
  EXPECT_EQ(bus.restart_calls, 2) << "a failed restart must stay retryable";
  EXPECT_TRUE(bus.change_speed(CAN_Speed::CAN_SPEED_100KBPS));
}

TEST(CanBusTest, RestartWithoutStopIsRefused) {
  FakeBus bus;
  CountingReceiver battery;
  bus.register_receiver(&battery, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS);
  ASSERT_TRUE(bus.init());
  bus.restart();
  EXPECT_EQ(bus.restart_calls, 0) << "an unpaired restart would re-begin a live controller";
}

TEST(CanBusTest, StopAndRestartAreNoOpsBeforeInit) {
  FakeBus bus;
  bus.stop();
  bus.restart();
  EXPECT_EQ(bus.stop_calls, 0);
  EXPECT_EQ(bus.restart_calls, 0);
}

TEST(CanBusTest, ConfigInvalidEventDataPacksBusAndReason) {
  EXPECT_EQ(can_config_invalid_event_data(CAN_LOG_ID_MCP2517FD, CanResolveError::SpeedConflict), 0x32)
      << "high nibble carries the bus log id, low nibble the CanResolveError";
  EXPECT_EQ(can_config_invalid_event_data(CAN_LOG_ID_NATIVE, CanResolveError::MultipleBatteries), 0x04);
}

TEST(CanBusTest, DispatchFansOutToAllReceivers) {
  FakeBus bus;
  CountingReceiver r1, r2;
  bus.register_receiver(&r1, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS);
  bus.register_receiver(&r2, CanRole::Inverter, CAN_Speed::CAN_SPEED_500KBPS);
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
  CountingReceiver receiver;
  bus.register_receiver(&receiver, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS);
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
  bus.register_receiver(&receiver, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS);
  set_millis64(1000);
  CAN_frame frame = {};
  frame.ID = 0x123;
  bus.inject(frame);
  EXPECT_TRUE(bus.recently_received(50));
}

TEST(CanBusTest, ActivityExpiresAfterTheHoldWindow) {
  FakeBus bus;
  CountingReceiver receiver;
  bus.register_receiver(&receiver, CanRole::Battery, CAN_Speed::CAN_SPEED_500KBPS);
  set_millis64(2000);
  CAN_frame frame = {};
  bus.inject(frame);
  set_millis64(2049);
  EXPECT_TRUE(bus.recently_received(50));
  set_millis64(2051);
  EXPECT_FALSE(bus.recently_received(50));
}
