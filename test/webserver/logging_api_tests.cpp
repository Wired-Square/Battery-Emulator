#include <gtest/gtest.h>
#include <cstring>
#include "../../Software/src/datalayer/datalayer.h"
#include "../../Software/src/devboard/webserver/logging_api.h"
#include "../../Software/src/devboard/webserver/json_response_writer.h"

static String canlog_json() {
  return render_json(write_canlog);
}

static String debug_json() {
  return render_json(write_debug);
}
#include "../../Software/src/lib/bblanchon-ArduinoJson/ArduinoJson.h"

// comm_can.cpp is not linked into the host target; provide the symbol the
// builder references (mirrors the host stand-in pattern in tests.cpp).
uint16_t user_selected_CAN_ID_cutoff_filter = 0;

namespace {
void seed_buffer(const char* text) {
  auto& info = datalayer.system.info;
  std::memset(info.logged_can_messages, 0, sizeof(info.logged_can_messages));
  std::strncpy(info.logged_can_messages, text, sizeof(info.logged_can_messages) - 1);
  info.logged_can_messages_offset = 0;
}
}  // namespace

TEST(LoggingApi, CanlogSplitsBufferOnNewlines) {
  seed_buffer("0x100 aa bb\n0x200 cc dd\n");
  datalayer.system.info.can_logging_active = true;
  datalayer.system.info.CAN_SD_logging_active = false;
  user_selected_CAN_ID_cutoff_filter = 42;

  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, canlog_json().c_str()));
  EXPECT_TRUE(doc["active"].as<bool>());
  EXPECT_FALSE(doc["sd"].as<bool>());
  EXPECT_EQ(doc["cutoff"].as<int>(), 42);
  ASSERT_EQ(doc["lines"].size(), 2u);
  EXPECT_STREQ(doc["lines"][0], "0x100 aa bb");
  EXPECT_STREQ(doc["lines"][1], "0x200 cc dd");
}

TEST(LoggingApi, CanlogEmptyBufferYieldsNoLines) {
  seed_buffer("");
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, canlog_json().c_str()));
  EXPECT_EQ(doc["lines"].size(), 0u);
}

TEST(LoggingApi, CanlogDropsTrailingUnterminatedFragment) {
  // A frame is written incrementally; a read that races a half-written frame
  // must not show the partial. Legacy split only on complete '\n'-terminated
  // lines.
  seed_buffer("0x100 aa\n0x200 bb");  // no trailing newline
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, canlog_json().c_str()));
  ASSERT_EQ(doc["lines"].size(), 1u);
  EXPECT_STREQ(doc["lines"][0], "0x100 aa");
}

TEST(LoggingApi, CanlogPreservesEmptyLines) {
  seed_buffer("a\n\nb\n");
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, canlog_json().c_str()));
  ASSERT_EQ(doc["lines"].size(), 3u);
  EXPECT_STREQ(doc["lines"][0], "a");
  EXPECT_STREQ(doc["lines"][1], "");
  EXPECT_STREQ(doc["lines"][2], "b");
}

TEST(LoggingApi, DebugShowsNewlineLessTailBeforeHead) {
  // Wrapped, and the older tail past the write head has no newline. Legacy
  // still emitted that fragment; it must not be dropped.
  auto& info = datalayer.system.info;
  std::memset(info.logged_can_messages, 0, sizeof(info.logged_can_messages));
  std::strcpy(info.logged_can_messages, "new\n");            // head, terminator at [4]
  std::strcpy(info.logged_can_messages + 5, "oldfragment");  // tail, no newline
  info.logged_can_messages_offset = 4;

  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, debug_json().c_str()));
  ASSERT_EQ(doc["lines"].size(), 2u);
  EXPECT_STREQ(doc["lines"][0], "oldfragment");
  EXPECT_STREQ(doc["lines"][1], "new");
}

TEST(LoggingApi, DebugReadsWrappedRingBufferInOrder) {
  auto& info = datalayer.system.info;
  std::memset(info.logged_can_messages, 0, sizeof(info.logged_can_messages));
  // Simulate a wrap: newest head "new1\n" at [0], older tail after the offset.
  std::strcpy(info.logged_can_messages, "new1\n");
  const size_t tail_at = 100;
  std::strcpy(info.logged_can_messages + tail_at, "partial\nold1\nold2\n");
  info.logged_can_messages_offset = 5;  // 0 < offset < size-1 -> wrapped path

  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, debug_json().c_str()));
  // Tail (from the first clean boundary after offset) precedes the head.
  ASSERT_GE(doc["lines"].size(), 2u);
  EXPECT_STREQ(doc["lines"][0], "old1");
  EXPECT_STREQ(doc["lines"][doc["lines"].size() - 1], "new1");
}
