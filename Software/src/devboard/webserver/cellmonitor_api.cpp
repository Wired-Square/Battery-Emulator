#include "cellmonitor_api.h"
#include "web_json.h"

#include <string>

#include "../../battery/BATTERIES.h"
#include "../../datalayer/datalayer.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"

namespace {

void emit_battery(JsonArray entries, uint8_t slot, uint8_t cell_count, const uint16_t* cell_voltages_mV,
                  const bool* cell_balancing, balancing_status_enum balancing_status) {
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
    emit_battery(entries, slot, pack.info.number_of_cells, pack.status.cell_voltages_mV,
                 pack.status.cell_balancing_status, pack.status.balancing_status);
  }
  return serialise_doc(doc);
}
