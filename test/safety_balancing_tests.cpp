#include <gtest/gtest.h>

#include "../Software/src/datalayer/datalayer.h"
#include "../Software/src/devboard/safety/safety.h"
#include "../Software/src/devboard/utils/events.h"
#include "../Software/src/system_settings.h"

#include "Arduino.h"

namespace {

class BalancingTimeoutTest : public testing::Test {
 protected:
  // balancing_start_time_ms is its own "not started" sentinel, so a clock at 0 never latches.
  static constexpr uint64_t kBootTimeMs = 1000;

  void SetUp() override {
    datalayer = DataLayer();
    reset_all_events();
    set_millis64(kBootTimeMs);
  }

  static DATALAYER_BATTERY_SETTINGS_TYPE& settings(uint8_t slot) { return datalayer.battery_slot(slot).settings; }

  static void run_to_timeout(uint8_t slot) {
    update_machineryprotection();
    set_millis64(kBootTimeMs + settings(slot).balancing_max_time_ms + 1);
    update_machineryprotection();
  }
};

}  // namespace

TEST_F(BalancingTimeoutTest, CapturesStartTimeOnTheFirstTick) {
  set_millis64(12345);
  settings(0).user_requests_balancing = true;

  update_machineryprotection();

  EXPECT_EQ(settings(0).balancing_start_time_ms, 12345u);
  EXPECT_TRUE(settings(0).user_requests_balancing);
}

TEST_F(BalancingTimeoutTest, ClearsRequestOnPrimarySlot) {
  settings(0).user_requests_balancing = true;

  run_to_timeout(0);

  EXPECT_FALSE(settings(0).user_requests_balancing);
  EXPECT_EQ(settings(0).balancing_start_time_ms, 0u);
}

TEST_F(BalancingTimeoutTest, ClearsRequestOnEverySlot) {
  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    SetUp();
    settings(slot).user_requests_balancing = true;

    run_to_timeout(slot);

    EXPECT_FALSE(settings(slot).user_requests_balancing) << "slot " << static_cast<int>(slot) << " never timed out";
    EXPECT_EQ(settings(slot).balancing_start_time_ms, 0u) << "slot " << static_cast<int>(slot);
  }
}

TEST_F(BalancingTimeoutTest, SlotsTimeOutIndependently) {
  settings(0).user_requests_balancing = true;
  settings(1).user_requests_balancing = true;
  settings(1).balancing_max_time_ms = settings(0).balancing_max_time_ms / 2;

  update_machineryprotection();
  set_millis64(kBootTimeMs + settings(1).balancing_max_time_ms + 1);
  update_machineryprotection();

  EXPECT_FALSE(settings(1).user_requests_balancing) << "the short deadline should have expired";
  EXPECT_TRUE(settings(0).user_requests_balancing) << "the long deadline was ended by the other slot";

  set_millis64(kBootTimeMs + settings(0).balancing_max_time_ms + 1);
  update_machineryprotection();

  EXPECT_FALSE(settings(0).user_requests_balancing);
}

TEST_F(BalancingTimeoutTest, LeavesSlotsThatNeverRequestedBalancingAlone) {
  settings(1).user_requests_balancing = true;

  run_to_timeout(1);

  EXPECT_EQ(settings(0).balancing_start_time_ms, 0u);
  EXPECT_EQ(settings(2).balancing_start_time_ms, 0u);
  EXPECT_FALSE(settings(0).user_requests_balancing);
  EXPECT_FALSE(settings(2).user_requests_balancing);
}
