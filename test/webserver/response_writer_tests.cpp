#include <gtest/gtest.h>

#include <string>

#include "../../Software/src/battery/BATTERIES.h"
#include "../../Software/src/datalayer/datalayer.h"
#include "../../Software/src/devboard/webserver/cellmonitor_api.h"
#include "../../Software/src/devboard/webserver/json_response_writer.h"
#include "../../Software/src/lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "../response_writer_recorder.h"

namespace {

// A mismatch here is a wire-format change no other test would catch.
void expect_same_as_document(const JsonDocument& doc, const ResponseProducer& producer) {
  std::string expected;
  serializeJson(doc, expected);
  EXPECT_EQ(std::string(render_json(producer).c_str()), expected);
}

}  // namespace

TEST(JsonResponseWriter, MatchesDocumentForScalars) {
  JsonDocument doc;
  doc["t"] = true;
  doc["f"] = false;
  doc["zero"] = 0;
  doc["neg"] = -2147483648;
  doc["big"] = static_cast<int64_t>(9007199254740993LL);
  doc["exact_float"] = 412.5;
  doc["third"] = 1.0 / 3.0;
  doc["tiny"] = 1e-12;
  doc["huge"] = 1e30;
  doc["float_src"] = 0.1f;
  doc["double_src"] = 0.1;
  doc["text"] = "plain";
  doc["quoted"] = "he said \"hi\"\n\tand\\left";
  doc["empty"] = "";
  doc["missing"] = static_cast<const char*>(nullptr);

  expect_same_as_document(doc, [](ResponseWriter& out) {
    out.begin_object();
    out.field("t", true);
    out.field("f", false);
    out.field("zero", 0);
    out.field("neg", -2147483648);
    out.field("big", static_cast<int64_t>(9007199254740993LL));
    out.field("exact_float", 412.5);
    out.field("third", 1.0 / 3.0);
    out.field("tiny", 1e-12);
    out.field("huge", 1e30);
    out.field("float_src", 0.1f);
    out.field("double_src", 0.1);
    out.field("text", "plain");
    out.field("quoted", "he said \"hi\"\n\tand\\left");
    out.field("empty", "");
    out.field("missing", static_cast<const char*>(nullptr));
    out.end_object();
  });
}

TEST(JsonResponseWriter, MatchesDocumentForNesting) {
  JsonDocument doc;
  doc["empty_object"].to<JsonObject>();
  doc["empty_array"].to<JsonArray>();
  JsonArray rows = doc["rows"].to<JsonArray>();
  JsonArray first = rows.add<JsonArray>();
  first.add(1);
  first.add<JsonVariant>();
  first.add("two");
  JsonObject nested = rows.add<JsonObject>();
  nested["deep"]["deeper"]["value"] = 7;
  rows.add<JsonArray>();

  expect_same_as_document(doc, [](ResponseWriter& out) {
    out.begin_object();
    out.begin_object("empty_object");
    out.end_object();
    out.begin_array("empty_array");
    out.end_array();
    out.begin_array("rows");
    out.begin_array();
    out.element(1);
    out.null_element();
    out.element("two");
    out.end_array();
    out.begin_object();
    out.begin_object("deep");
    out.begin_object("deeper");
    out.field("value", 7);
    out.end_object();
    out.end_object();
    out.end_object();
    out.begin_array();
    out.end_array();
    out.end_array();
    out.end_object();
  });
}

TEST(JsonResponseWriter, WritesLengthDelimitedSliceWithoutCopying) {
  const char buffer[] = "first\nsecond\n";
  JsonDocument doc;
  JsonArray lines = doc["lines"].to<JsonArray>();
  lines.add("first");
  lines.add("second");

  expect_same_as_document(doc, [&buffer](ResponseWriter& out) {
    out.begin_object();
    out.begin_array("lines");
    out.element(buffer, 5);
    out.element(buffer + 6, 6);
    out.end_array();
    out.end_object();
  });
}

TEST(JsonResponseWriter, SpansBufferBoundaries) {
  const std::string long_text(JsonResponseWriter::kBufferBytes * 3 + 7, 'x');
  JsonDocument doc;
  doc["long"] = long_text;

  expect_same_as_document(doc, [&long_text](ResponseWriter& out) {
    out.begin_object();
    out.field("long", long_text.c_str());
    out.end_object();
  });
}

TEST(ResponseWriterBackends, CellmonitorRendersThroughANonJsonBackend) {
  datalayer = DataLayer();
  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    batteries[slot] = nullptr;
  }
  auto& pack = datalayer.battery.pack[0];
  pack.info.number_of_cells = 2;
  pack.status.cell_voltages_mV[0] = 3300;
  pack.status.cell_voltages_mV[1] = 3301;
  pack.status.cell_balancing_status[0] = true;
  pack.status.cell_balancing_status[1] = false;

  RecordingResponseWriter recorder;
  write_cellmonitor(recorder);

  EXPECT_EQ(recorder.entries,
            (std::vector<std::string>{"batteries[0].slot=0", "batteries[0].cells[0]=3300",
                                      "batteries[0].cells[1]=3301", "batteries[0].balancing[0]=true",
                                      "batteries[0].balancing[1]=false", "batteries[0].balancing_active=false",
                                      "batteries[0].balancing_pending=false"}));
}
