#include <gtest/gtest.h>

#include <initializer_list>

#include "../../Software/src/battery/BATTERIES.h"
#include "../../Software/src/datalayer/datalayer.h"
#include "../../Software/src/devboard/webserver/cellmonitor_api.h"
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
}  // namespace

class CellMonitorApi : public testing::Test {
 public:
  void SetUp() override {
    datalayer = DataLayer();
    batteries[1] = nullptr;
    batteries[2] = nullptr;
  }
};

TEST_F(CellMonitorApi, SkipsUnreadCellsAndAlignsBalancing) {
  seed(datalayer.battery.pack[0], {3700, 3710, 0, 3690}, {false, true, false, false});

  auto doc = parse(build_cellmonitor_json());
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

  auto doc = parse(build_cellmonitor_json());
  EXPECT_TRUE(doc["batteries"][0]["balancing_active"].as<bool>());
}

TEST_F(CellMonitorApi, IncludesSecondBatteryOnlyWhenPresent) {
  seed(datalayer.battery.pack[0], {3700, 3710}, {false, false});
  seed(datalayer.battery.pack[1], {3600, 3620, 3610}, {false, false, false});

  auto without = parse(build_cellmonitor_json());
  EXPECT_EQ(without["batteries"].size(), 1u);

  // The builder only tests the pointer for presence; it never dereferences it.
  Battery* sentinel = reinterpret_cast<Battery*>(1);
  batteries[1] = sentinel;
  auto with = parse(build_cellmonitor_json());
  ASSERT_EQ(with["batteries"].size(), 2u);
  EXPECT_EQ(with["batteries"][1]["cells"].size(), 3u);
  batteries[1] = nullptr;
}
