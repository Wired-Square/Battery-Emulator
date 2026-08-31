#include <gtest/gtest.h>

#include <Arduino.h>

#include "../Software/src/communication/rs485/Rs485Port.h"
#include "../Software/src/communication/rs485/comm_rs485.h"
#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_lilygo.h"

namespace {
class FakeRs485Receiver : public Rs485Receiver {
 public:
  int receive_count = 0;
  void receive() override { receive_count++; }
};
}  // namespace

class Rs485PortTest : public testing::Test {
 public:
  void SetUp() override {
    set_millis64(1000);
    Serial2.rx_queue.clear();
  }
  Rs485Port port{Serial2, UART_NUM_2, {GPIO_NUM_22, GPIO_NUM_21, GPIO_NUM_NC}};
};

TEST_F(Rs485PortTest, PollWithoutReceiversIsNoOp) {
  Serial2.rx_queue.push_back(0x42);
  port.poll();
  EXPECT_FALSE(port.recently_received(50));
}

TEST_F(Rs485PortTest, PollDispatchesToRegisteredReceivers) {
  FakeRs485Receiver a;
  FakeRs485Receiver b;
  port.register_receiver(&a);
  port.register_receiver(&b);
  EXPECT_TRUE(port.has_receivers());
  port.poll();
  EXPECT_EQ(a.receive_count, 1);
  EXPECT_EQ(b.receive_count, 1);
}

TEST_F(Rs485PortTest, PollStampsActivityWhenBytesPending) {
  FakeRs485Receiver a;
  port.register_receiver(&a);
  port.poll();
  EXPECT_FALSE(port.recently_received(50));
  Serial2.rx_queue.push_back(0x42);
  port.poll();
  EXPECT_TRUE(port.recently_received(50));
}

TEST_F(Rs485PortTest, ActivityWindowExpires) {
  port.mark_activity();
  EXPECT_TRUE(port.recently_received(50));
  set_millis64(1049);
  EXPECT_TRUE(port.recently_received(50));
  set_millis64(1050);
  EXPECT_FALSE(port.recently_received(50));
}

namespace {
// A DE-wired port needs the board to program half-duplex; these stand in for a
// board that can and a board that cannot.
class DeCapableHal : public LilyGoHal {
 public:
  bool configure_rs485_half_duplex(Rs485Port&) override { return true; }
};

class DeRefusingHal : public LilyGoHal {
 public:
  bool configure_rs485_half_duplex(Rs485Port&) override { return false; }
};

struct HalSwap {
  Esp32Hal* saved = esp32hal;
  explicit HalSwap(Esp32Hal* hal) { esp32hal = hal; }
  ~HalSwap() { esp32hal = saved; }
};
}  // namespace

TEST_F(Rs485PortTest, PortWithoutDeOpensWithoutAskingTheBoard) {
  DeRefusingHal hal;
  HalSwap swap(&hal);
  Rs485Port no_de{Serial2, UART_NUM_2, {GPIO_NUM_22, GPIO_NUM_21, GPIO_NUM_NC}};
  EXPECT_TRUE(no_de.begin("test", 9600));
  EXPECT_TRUE(Serial2.begun);
}

TEST_F(Rs485PortTest, DeWiredPortIsRefusedWhenPreinitNeverClaimedThePin) {
  DeCapableHal hal;
  HalSwap swap(&hal);
  Serial2.begun = false;
  Rs485Port de_port{Serial2, UART_NUM_2, {GPIO_NUM_22, GPIO_NUM_21, GPIO_NUM_19}};
  // preinit() deliberately not called: the UART must never start.
  EXPECT_FALSE(de_port.begin("test", 9600));
  EXPECT_FALSE(Serial2.begun);
}

TEST_F(Rs485PortTest, DeWiredPortIsClosedAgainWhenTheBoardCannotProgramIt) {
  DeRefusingHal hal;
  HalSwap swap(&hal);
  Serial2.begun = false;
  Rs485Port de_port{Serial2, UART_NUM_2, {GPIO_NUM_22, GPIO_NUM_21, GPIO_NUM_19}};
  de_port.preinit();
  EXPECT_FALSE(de_port.begin("test", 9600));
  // Closed, so a caller that ignores the return cannot transmit into a
  // transceiver that never releases the bus.
  EXPECT_FALSE(Serial2.begun);
}
