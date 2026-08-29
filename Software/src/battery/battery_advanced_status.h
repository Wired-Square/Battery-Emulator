#ifndef BATTERY_ADVANCED_STATUS_H
#define BATTERY_ADVANCED_STATUS_H

#include <WString.h>
#include <stdint.h>
#include <initializer_list>

#include "translatable_label.h"

enum class AdvancedSeverity : uint8_t { Normal, Good, Warning, Critical, Muted };

struct AdvancedValue {
  String text;
  AdvancedSeverity severity = AdvancedSeverity::Normal;
};

inline AdvancedValue good_if(bool ok, const char* yes, const char* no,
                             AdvancedSeverity otherwise = AdvancedSeverity::Critical) {
  return {ok ? yes : no, ok ? AdvancedSeverity::Good : otherwise};
}

// Abstract, not JSON-backed: holding a JsonArray here would pull ArduinoJson
// into every driver that includes this header.
//
// A table may name a `catalogue` of descriptions for its coded values. The
// client fetches it and appends a description column, matching each row on the
// key given to row_begin(). The key is separate from the displayed cells
// because a code's display form may carry detail the catalogue does not key on.
class AdvancedStatusWriter {
 public:
  virtual ~AdvancedStatusWriter() = default;

  virtual void section(const char* title = "") = 0;

  virtual void kv(const char* label, const String& value, const char* unit = "",
                  AdvancedSeverity severity = AdvancedSeverity::Normal) = 0;
  virtual void kv(const char* label, const char* value, const char* unit = "",
                  AdvancedSeverity severity = AdvancedSeverity::Normal) = 0;

  void kv(const char* label, const AdvancedValue& value, const char* unit = "") {
    kv(label, value.text, unit, value.severity);
  }

  virtual void table(const char* label, std::initializer_list<const char*> columns,
                     const char* catalogue = nullptr) = 0;
  virtual void row_begin(const char* key = nullptr) = 0;
  virtual void cell(const String& text) = 0;
  virtual void row_end() = 0;
};

#endif
