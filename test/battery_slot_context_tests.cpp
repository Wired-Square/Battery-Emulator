#include <gtest/gtest.h>

#include "../Software/src/battery/BATTERIES.h"
#include "../Software/src/battery/BatterySlotContext.h"
#include "../Software/src/datalayer/datalayer.h"
#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_lilygo.h"

class BatterySlotContextTest : public testing::Test {
 public:
  void SetUp() override { init_hal(); }
};

TEST_F(BatterySlotContextTest, PrimarySlotBindsToPrimaryGlobals) {
  BatterySlotContext c0 = battery_slot_context(0);
  EXPECT_EQ(c0.slot, 0);
  EXPECT_TRUE(c0.is_primary());
  EXPECT_EQ(c0.datalayer, &datalayer.battery.pack[0]);
  EXPECT_EQ(c0.contactor_flag, &datalayer.system.status.battery_allows_contactor_closing);
}

TEST_F(BatterySlotContextTest, SecondSlotBindsToSecondGlobals) {
  BatterySlotContext c1 = battery_slot_context(1);
  EXPECT_EQ(c1.slot, 1);
  EXPECT_FALSE(c1.is_primary());
  EXPECT_EQ(c1.datalayer, &datalayer.battery.pack[1]);
  EXPECT_EQ(c1.contactor_flag, &datalayer.system.status.battery2_allowed_contactor_closing);
}

TEST_F(BatterySlotContextTest, ThirdSlotBindsToThirdGlobals) {
  BatterySlotContext c2 = battery_slot_context(2);
  EXPECT_EQ(c2.slot, 2);
  EXPECT_FALSE(c2.is_primary());
  EXPECT_EQ(c2.datalayer, &datalayer.battery.pack[2]);
  EXPECT_EQ(c2.contactor_flag, &datalayer.system.status.battery3_allowed_contactor_closing);
}

// The host build compiles HW_LILYGO, whose HAL is default-constructible, so
// the wakeup_pin(size_t) indexing can be verified against the real board
// pins rather than deferred to on-target testing.
TEST(WakeupPin, IndexesLilyGoWupPins) {
  LilyGoHal hal;
  EXPECT_EQ(hal.wakeup_pin(0), hal.WUP_PIN1());
  EXPECT_EQ(hal.wakeup_pin(1), hal.WUP_PIN2());
  EXPECT_EQ(hal.wakeup_pin(2), GPIO_NUM_NC);
}
