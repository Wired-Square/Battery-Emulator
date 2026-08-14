#include <gtest/gtest.h>

#include "../../Software/src/devboard/utils/events.h"
#include "../../Software/src/devboard/webserver/events_api.h"
#include "../../Software/src/lib/bblanchon-ArduinoJson/ArduinoJson.h"

#include "Arduino.h"  // set_millis64

namespace {
JsonDocument parse(const String& json) {
  JsonDocument doc;
  EXPECT_FALSE(deserializeJson(doc, json.c_str()));
  return doc;
}
}  // namespace

TEST(EventsApi, EmptyWhenNoOccurrences) {
  reset_all_events();
  auto doc = parse(build_events_json());
  EXPECT_EQ(doc["events"].size(), 0u);
}

TEST(EventsApi, NewestFirstWithFields) {
  reset_all_events();
  set_millis64(1000);
  set_event(EVENT_TASK_OVERRUN, 7);
  set_millis64(5000);
  set_event(EVENT_THERMAL_RUNAWAY, 3);
  set_millis64(8000);

  auto doc = parse(build_events_json());
  ASSERT_EQ(doc["events"].size(), 2u);

  auto newest = doc["events"][0];
  EXPECT_STREQ(newest["type"], get_event_enum_string(EVENT_THERMAL_RUNAWAY));
  EXPECT_STREQ(newest["level"], get_event_level_string(EVENT_THERMAL_RUNAWAY));
  EXPECT_EQ(newest["millis_ago"].as<uint64_t>(), 3000u);
  EXPECT_EQ(newest["count"].as<int>(), 1);
  EXPECT_EQ(newest["data"].as<int>(), 3);

  auto older = doc["events"][1];
  EXPECT_STREQ(older["type"], get_event_enum_string(EVENT_TASK_OVERRUN));
  EXPECT_EQ(older["millis_ago"].as<uint64_t>(), 7000u);
}

TEST(EventsApi, CountAccumulatesAndLatestDataWins) {
  reset_all_events();
  set_millis64(1000);
  set_event(EVENT_TASK_OVERRUN, 1);
  set_event(EVENT_TASK_OVERRUN, 2);

  auto doc = parse(build_events_json());
  ASSERT_EQ(doc["events"].size(), 1u);
  EXPECT_EQ(doc["events"][0]["count"].as<int>(), 2);
  EXPECT_EQ(doc["events"][0]["data"].as<int>(), 2);
}
