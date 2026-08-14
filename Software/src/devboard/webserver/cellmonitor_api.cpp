#include "cellmonitor_api.h"
#include "web_json.h"

#include <string>

#include "../../battery/BATTERIES.h"
#include "../../datalayer/datalayer.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"

namespace {

void emit_battery(JsonArray entries, uint8_t cell_count, const uint16_t* cell_voltages_mV,
                  const bool* cell_balancing, balancing_status_enum balancing_status) {
  JsonObject entry = entries.add<JsonObject>();
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

  const auto& b1 = datalayer.battery.pack[0];
  emit_battery(entries, b1.info.number_of_cells, b1.status.cell_voltages_mV, b1.status.cell_balancing_status,
               b1.status.balancing_status);
  if (batteries[1]) {
    const auto& b2 = datalayer.battery.pack[1];
    emit_battery(entries, b2.info.number_of_cells, b2.status.cell_voltages_mV, b2.status.cell_balancing_status,
                 b2.status.balancing_status);
  }
  if (batteries[2]) {
    const auto& b3 = datalayer.battery.pack[2];
    emit_battery(entries, b3.info.number_of_cells, b3.status.cell_voltages_mV, b3.status.cell_balancing_status,
                 b3.status.balancing_status);
  }
  return serialise_doc(doc);
}
