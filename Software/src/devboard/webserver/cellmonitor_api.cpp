#include "cellmonitor_api.h"

#include <cmath>

#include "../../battery/BATTERIES.h"
#include "../../battery/Battery.h"
#include "../../datalayer/datalayer.h"

namespace {

// The driver reports progress before the readings, but the entry carries
// "values" ahead of them, so emitting progress on arrival would reorder the
// document. Three held-back scalars let the readings stream through unchanged.
class StreamingCellSeriesWriter : public CellSeriesWriter {
 public:
  explicit StreamingCellSeriesWriter(ResponseWriter& out) : out_(out) {}

  void series(const char* id, const char* label, const char* unit, CellSeriesKind kind, uint8_t decimals,
              uint32_t revision) override {
    close();
    out_.begin_object();
    out_.field("id", id);
    out_.field("label", label);
    out_.field("unit", unit);
    out_.field("kind", kind == CellSeriesKind::Counter ? "counter" : "gauge");
    out_.field("decimals", decimals);
    out_.field("revision", revision);
    out_.begin_array("values");
    decimals_ = decimals;
    open_ = true;
    state_ = CellSeriesState::Unread;
    read_ = 0;
    expected_ = 0;
  }

  void progress(CellSeriesState state, uint8_t read, uint8_t expected) override {
    if (!open_) {
      return;
    }
    state_ = state;
    read_ = read;
    expected_ = expected;
    reported_ = true;
  }

  void value(float reading) override {
    if (!open_) {
      return;
    }
    if (decimals_ == 0) {
      out_.element(static_cast<int32_t>(std::lround(reading)));
    } else {
      out_.element(reading);
    }
  }

  void unknown() override {
    if (open_) {
      out_.null_element();
    }
  }

  void close() {
    if (!open_) {
      return;
    }
    out_.end_array();
    if (reported_) {
      out_.field("state", name_for_state(state_));
      out_.field("read", read_);
      out_.field("expected", expected_);
    }
    out_.end_object();
    open_ = false;
    reported_ = false;
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

  ResponseWriter& out_;
  uint8_t decimals_ = 0;
  bool open_ = false;
  bool reported_ = false;
  CellSeriesState state_ = CellSeriesState::Unread;
  uint8_t read_ = 0;
  uint8_t expected_ = 0;
};

void emit_battery(ResponseWriter& out, uint8_t slot, Battery* battery, uint8_t cell_count,
                  const uint16_t* cell_voltages_mV, const bool* cell_balancing,
                  balancing_status_enum balancing_status) {
  out.begin_object();
  out.field("slot", slot);
  out.begin_array("cells");
  for (uint8_t i = 0; i < cell_count; i++) {
    // A zero reading is an unpopulated slot, not a real 0 mV cell.
    if (cell_voltages_mV[i] == 0) {
      continue;
    }
    out.element(cell_voltages_mV[i]);
  }
  out.end_array();
  out.begin_array("balancing");
  for (uint8_t i = 0; i < cell_count; i++) {
    if (cell_voltages_mV[i] == 0) {
      continue;
    }
    out.element(cell_balancing[i]);
  }
  out.end_array();
  out.field("balancing_active", balancing_status == BALANCING_STATUS_ACTIVE);
  out.field("balancing_pending", balancing_status == BALANCING_STATUS_BLOCKED);
  out.begin_array("series");
  StreamingCellSeriesWriter writer(out);
  if (battery != nullptr) {
    battery->write_cell_series(writer);
  }
  writer.close();
  out.end_array();
  out.end_object();
}
}  // namespace

void write_cellmonitor(ResponseWriter& out) {
  out.begin_object();
  out.begin_array("batteries");
  for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
    if (slot > 0 && batteries[slot] == nullptr) {
      continue;
    }
    const auto& pack = datalayer.battery.pack[slot];
    emit_battery(out, slot, batteries[slot], pack.info.number_of_cells, pack.status.cell_voltages_mV,
                 pack.status.cell_balancing_status, pack.status.balancing_status);
  }
  out.end_array();
  out.end_object();
}
