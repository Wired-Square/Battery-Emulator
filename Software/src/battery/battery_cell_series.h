#ifndef BATTERY_CELL_SERIES_H
#define BATTERY_CELL_SERIES_H

#include <stdint.h>

#include "../system_settings.h"

// Gauge reads as a level; Counter accumulates, so a client can show what a cell
// gained between two readings.
enum class CellSeriesKind : uint8_t { Gauge, Counter };

enum class CellSeriesState : uint8_t { Unread, Pending, Reading, Complete, Partial, Failed };

// Abstract, not JSON-backed, for the same reason as AdvancedStatusWriter:
// holding a JsonArray here would pull ArduinoJson into every driver.
//
// A driver opens a series, then appends exactly one entry per cell in cell
// order, using unknown() for a cell it has no reading for. `revision` changes
// only when the underlying data does, so a client can tell a fresh reading from
// a repaint of the one it already has.
class CellSeriesWriter {
 public:
  virtual ~CellSeriesWriter() = default;

  virtual void series(const char* id, const char* label, const char* unit, CellSeriesKind kind, uint8_t decimals,
                      uint32_t revision) = 0;
  virtual void progress(CellSeriesState state, uint8_t read, uint8_t expected) = 0;
  virtual void value(float reading) = 0;
  virtual void unknown() = 0;
};

// How one series presents. A driver declares this once, next to the code that
// fills the buffer.
struct CellSeriesInfo {
  const char* id;
  const char* label;
  const char* unit;
  CellSeriesKind kind;
  uint8_t decimals;
};

// Readings for a scan that visits cells one at a time, for drivers that poll a
// value per cell. Tracks which cells answered, so a cell that genuinely read
// zero stays distinct from one that never answered.
class CellSeriesBuffer {
 public:
  static_assert(MAX_AMOUNT_CELLS <= 255, "cell cursors are uint8_t");
  static constexpr uint8_t kMaxCells = MAX_AMOUNT_CELLS;

  // Discards the previous readings and opens a new scan. `revision` is
  // deliberately left alone until finish(): a client must keep showing the
  // reading it has while the next one is still being collected.
  void begin(uint8_t expected_cells) {
    for (uint8_t i = 0; i < kMaxCells; i++) {
      values_[i] = 0;
    }
    for (uint8_t i = 0; i < kValidBytes; i++) {
      valid_[i] = 0;
    }
    expected_ = expected_cells < kMaxCells ? expected_cells : kMaxCells;
    received_ = 0;
    state_ = CellSeriesState::Reading;
  }

  void set(uint8_t cell, uint16_t reading) {
    if (cell >= expected_) {
      return;
    }
    if (!cell_valid(cell)) {
      received_++;
    }
    values_[cell] = reading;
    valid_[cell / 8] |= static_cast<uint8_t>(1U << (cell % 8));
  }

  // Ends the scan: everything answered is Complete, some is Partial, none is
  // Failed. Moving revision here is what tells a client the reading is new.
  void finish() {
    state_ = received_ == expected_ ? CellSeriesState::Complete
             : received_           ? CellSeriesState::Partial
                                   : CellSeriesState::Failed;
    revision_++;
  }

  bool cell_valid(uint8_t cell) const {
    return cell < kMaxCells && (valid_[cell / 8] & static_cast<uint8_t>(1U << (cell % 8)));
  }
  uint16_t value(uint8_t cell) const { return cell < kMaxCells ? values_[cell] : 0; }
  uint8_t expected() const { return expected_; }
  uint8_t received() const { return received_; }
  uint32_t revision() const { return revision_; }
  CellSeriesState state() const { return state_; }

  void publish(CellSeriesWriter& out, const CellSeriesInfo& info) const { publish(out, info, state_); }

  // Takes `state` for a driver that knows the scan is queued but has not begun,
  // which the buffer cannot know: nothing has called begin() yet.
  void publish(CellSeriesWriter& out, const CellSeriesInfo& info, CellSeriesState state) const {
    out.series(info.id, info.label, info.unit, info.kind, info.decimals, revision_);
    out.progress(state, received_, expected_);
    for (uint8_t cell = 0; cell < expected_; cell++) {
      if (cell_valid(cell)) {
        out.value(values_[cell]);
      } else {
        out.unknown();
      }
    }
  }

 private:
  static constexpr uint8_t kValidBytes = (kMaxCells + 7) / 8;

  uint16_t values_[kMaxCells] = {0};
  uint8_t valid_[kValidBytes] = {0};
  uint32_t revision_ = 0;
  uint8_t expected_ = 0;
  uint8_t received_ = 0;
  CellSeriesState state_ = CellSeriesState::Unread;
};

#endif
