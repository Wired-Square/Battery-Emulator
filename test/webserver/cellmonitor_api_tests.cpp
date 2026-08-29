#include <gtest/gtest.h>

#include <initializer_list>

#include "../../Software/src/battery/BATTERIES.h"
#include "../../Software/src/battery/Battery.h"
#include "../../Software/src/datalayer/datalayer.h"
#include "../../Software/src/devboard/webserver/cellmonitor_api.h"
#include "../../Software/src/devboard/webserver/json_response_writer.h"

static String cellmonitor_json() {
  return render_json(write_cellmonitor);
}
#include "../../Software/src/lib/bblanchon-ArduinoJson/ArduinoJson.h"

namespace {
JsonDocument parse(const String& json) {
  JsonDocument doc;
  EXPECT_FALSE(deserializeJson(doc, json.c_str()));
  return doc;
}

void seed(DATALAYER_BATTERY_TYPE& b, std::initializer_list<uint16_t> mv, std::initializer_list<bool> bal) {
  b.info.number_of_cells = static_cast<uint8_t>(mv.size());
  size_t i = 0;
  for (uint16_t v : mv) b.status.cell_voltages_mV[i++] = v;
  i = 0;
  for (bool x : bal) b.status.cell_balancing_status[i++] = x;
}
class SeriesBattery : public Battery {
 public:
  void setup() override {}
  void update_values() override {}
  const char* interface_name() override { return "test"; }

  void write_cell_series(CellSeriesWriter& out) override {
    out.series("balance_hours", "Cell balance time", "h", CellSeriesKind::Counter, 0, 7);
    out.progress(CellSeriesState::Partial, 2, 3);
    out.value(12);
    out.unknown();
    out.value(4);
  }
};
}  // namespace

class CellMonitorApi : public testing::Test {
 public:
  void SetUp() override {
    datalayer = DataLayer();
    for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++)
      batteries[slot] = nullptr;
  }
};

TEST_F(CellMonitorApi, SkipsUnreadCellsAndAlignsBalancing) {
  seed(datalayer.battery.pack[0], {3700, 3710, 0, 3690}, {false, true, false, false});

  auto doc = parse(cellmonitor_json());
  ASSERT_EQ(doc["batteries"].size(), 1u);
  auto cells = doc["batteries"][0]["cells"];
  auto bal = doc["batteries"][0]["balancing"];
  ASSERT_EQ(cells.size(), 3u);
  EXPECT_EQ(cells[0].as<int>(), 3700);
  EXPECT_EQ(cells[1].as<int>(), 3710);
  EXPECT_EQ(cells[2].as<int>(), 3690);
  ASSERT_EQ(bal.size(), 3u);
  EXPECT_TRUE(bal[1].as<bool>());
  EXPECT_FALSE(doc["batteries"][0]["balancing_active"].as<bool>());
}

TEST_F(CellMonitorApi, BalancingActiveFlag) {
  seed(datalayer.battery.pack[0], {3700}, {false});
  datalayer.battery.pack[0].status.balancing_status = BALANCING_STATUS_ACTIVE;

  auto doc = parse(cellmonitor_json());
  EXPECT_TRUE(doc["batteries"][0]["balancing_active"].as<bool>());
}

TEST_F(CellMonitorApi, IncludesSecondBatteryOnlyWhenPresent) {
  seed(datalayer.battery.pack[0], {3700, 3710}, {false, false});
  seed(datalayer.battery.pack[1], {3600, 3620, 3610}, {false, false, false});

  auto without = parse(cellmonitor_json());
  EXPECT_EQ(without["batteries"].size(), 1u);

  SeriesBattery second;
  batteries[1] = &second;
  auto with = parse(cellmonitor_json());
  ASSERT_EQ(with["batteries"].size(), 2u);
  EXPECT_EQ(with["batteries"][1]["cells"].size(), 3u);
  batteries[1] = nullptr;
}

TEST_F(CellMonitorApi, PublishesDriverCellSeries) {
  seed(datalayer.battery.pack[0], {3700, 3710, 3690}, {false, false, false});
  SeriesBattery primary;
  batteries[0] = &primary;

  auto doc = parse(cellmonitor_json());
  auto series = doc["batteries"][0]["series"];
  ASSERT_EQ(series.size(), 1u);
  EXPECT_STREQ(series[0]["id"], "balance_hours");
  EXPECT_STREQ(series[0]["unit"], "h");
  EXPECT_STREQ(series[0]["kind"], "counter");
  EXPECT_EQ(series[0]["revision"].as<uint32_t>(), 7u);
  EXPECT_STREQ(series[0]["state"], "partial");
  EXPECT_EQ(series[0]["read"].as<int>(), 2);
  EXPECT_EQ(series[0]["expected"].as<int>(), 3);
  auto values = series[0]["values"];
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values[0].as<int>(), 12);
  EXPECT_TRUE(values[1].isNull()) << "an unread cell must be null, not zero hours";
  EXPECT_EQ(values[2].as<int>(), 4);
  batteries[0] = nullptr;
}

TEST_F(CellMonitorApi, EmitsAnEmptySeriesListForADriverWithout) {
  seed(datalayer.battery.pack[0], {3700}, {false});
  auto doc = parse(cellmonitor_json());
  EXPECT_EQ(doc["batteries"][0]["series"].size(), 0u);
}
