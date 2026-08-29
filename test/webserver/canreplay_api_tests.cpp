#include <gtest/gtest.h>

#include "../../Software/src/datalayer/datalayer.h"
#include "../../Software/src/devboard/hal/hal.h"
#include "../../Software/src/devboard/webserver/canreplay_api.h"
#include "../../Software/src/devboard/webserver/json_response_writer.h"

static String canreplay_json(bool running, bool has_log) {
  return render_json([&](ResponseWriter& out) { write_canreplay(out, running, has_log); });
}
#include "../../Software/src/lib/bblanchon-ArduinoJson/ArduinoJson.h"

// The host target compiles HW_LILYGO: 5 interfaces, of which CanNative,
// CanMcp2515 and CanMcp2517fd are CAN-capable (indices 2, 3, 4).
class CanReplayApi : public testing::Test {
 public:
  void SetUp() override { init_hal(); }
};

TEST_F(CanReplayApi, ListsOnlyCanCapableInterfaces) {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, canreplay_json(false, false).c_str()));

  ASSERT_EQ(doc["interfaces"].size(), 3u);
  EXPECT_EQ(doc["interfaces"][0]["index"].as<int>(), 2);
  EXPECT_STREQ(doc["interfaces"][0]["name"], "CAN (Native)");
  EXPECT_EQ(doc["interfaces"][1]["index"].as<int>(), 3);
  EXPECT_EQ(doc["interfaces"][2]["index"].as<int>(), 4);
  EXPECT_FALSE(doc["running"].as<bool>());
  EXPECT_FALSE(doc["has_log"].as<bool>());
}

TEST_F(CanReplayApi, ReflectsSelectionAndRunState) {
  datalayer.system.info.can_replay_interface = 3;
  datalayer.system.info.loop_playback = true;

  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, canreplay_json(true, true).c_str()));

  EXPECT_EQ(doc["selected"].as<int>(), 3);
  EXPECT_TRUE(doc["running"].as<bool>());
  EXPECT_TRUE(doc["loop"].as<bool>());
  EXPECT_TRUE(doc["has_log"].as<bool>());
}
