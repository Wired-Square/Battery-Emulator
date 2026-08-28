#include "cellmonitor_api.h"
#include "web_json.h"

#include <cmath>
#include <string>

#include "../../battery/BATTERIES.h"
#include "../../battery/Battery.h"
#include "../../datalayer/datalayer.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"

namespace {

class JsonCellSeriesWriter : public CellSeriesWriter {
 public:
  explicit JsonCellSeriesWriter(JsonArray series) : series_(series) {}

  void series(const char* id, const char* label, const char* unit, CellSeriesKind kind, uint8_t decimals,
              uint32_t revision) override {
    JsonObject entry = series_.add<JsonObject>();
    entry["id"] = std::string(id);
    entry["label"] = std::string(label);
    entry["unit"] = std::string(unit);
    entry["kind"] = kind == CellSeriesKind::Counter ? "counter" : "gauge";
    entry["decimals"] = decimals;
    entry["revision"] = revision;
    decimals_ = decimals;
    values_ = entry["values"].to<JsonArray>();
    entry_ = entry;
  }

  void progress(CellSeriesState state, uint8_t read, uint8_t expected) override {
    if (entry_.isNull()) {
      return;
    }
    entry_["state"] = name_for_state(state);
    entry_["read"] = read;
    entry_["expected"] = expected;
  }

  void value(float reading) override {
    if (values_.isNull()) {
      return;
    }
    if (decimals_ == 0) {
      values_.add(static_cast<int32_t>(std::lround(reading)));
    } else {
      values_.add(reading);
    }
  }

  void unknown() override {
    if (!values_.isNull()) {
      values_.add<JsonVariant>();
    }
  }

 private:
  static const char* name_for_state(CellSeriesState state) {
    switch (state) {
      case CellSeriesState::Pending:
        return "pending";
      case CellSeriesState::Reading:
        return "reading";
      case CellSeriesState::Complete:
        return "complete";
      case CellSeriesState::Partial:
        return "partial";
      case CellSeriesState::Failed:
        return "failed";
      case CellSeriesState::Unread:
        break;
    }
    return "unread";
  }

  JsonArray series_;
  JsonObject entry_;
  JsonArray values_;
  uint8_t decimals_ = 0;
};

void emit_battery(JsonArray entries, uint8_t slot, Battery* battery, uint8_t cell_count,
                  const uint16_t* cell_voltages_mV, const bool* cell_balancing,
                  balancing_status_enum balancing_status) {
  JsonObject entry = entries.add<JsonObject>();
  entry["slot"] = slot;
  JsonArray cells = entry["cells"].to<JsonArray>();
  JsonArray balancing = entry["balancing"].to<JsonArray>();
  for (uint8_t i = 0; i < cell_count; i++) {
    // A zero reading is an unpopulated slot, not a real 0 mV cell.
    if (cell_voltages_mV[i] == 0) {
      continue;
    }
    cells.add(cell_voltages_mV[i]);
    balancing.add(cell_balancing[i]);
  }
  entry["balancing_active"] = balancing_status == BALANCING_STATUS_ACTIVE;
  entry["balancing_pending"] = balancing_status == BALANCING_STATUS_BLOCKED;
  JsonCellSeriesWriter writer(entry["series"].to<JsonArray>());
  if (battery != nullptr) {
    battery->write_cell_series(writer);
  }
}
}  // namespace

String build_cellmonitor_json() {
  JsonDocument doc;
  JsonArray entries = doc["batteries"].to<JsonArray>();

  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    if (slot > 0 && batteries[slot] == nullptr) {
      continue;
    }
    const auto& pack = datalayer.battery.pack[slot];
    emit_battery(entries, slot, batteries[slot], pack.info.number_of_cells, pack.status.cell_voltages_mV,
                 pack.status.cell_balancing_status, pack.status.balancing_status);
  }
  return serialise_doc(doc);
}
