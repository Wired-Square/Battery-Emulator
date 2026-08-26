#ifndef BATTERY_ADVANCED_STATUS_H
#define BATTERY_ADVANCED_STATUS_H

#include <WString.h>
#include <stdint.h>
#include <vector>

enum class AdvancedFieldKind { KeyValue, Table };

enum class AdvancedSeverity : uint8_t { Normal, Warning };

// A single rendered element. KeyValue = one labelled value (with optional unit).
// Table = a grid: `columns` headers over `rows` of equal-length string cells.
//
// A table may name a `catalogue` of descriptions for its coded values. The client
// fetches it and appends a description column, matching row i on `row_keys[i]`.
// The key is separate from the displayed cell because a code's display form may
// carry detail the catalogue does not key on.
struct AdvancedField {
  AdvancedFieldKind kind = AdvancedFieldKind::KeyValue;
  String label;
  String value;
  String unit;
  std::vector<String> columns;
  std::vector<std::vector<String>> rows;
  String catalogue;
  std::vector<String> row_keys;
  AdvancedSeverity severity = AdvancedSeverity::Normal;
};

struct AdvancedSection {
  String title;  // "" for an untitled/leading group
  std::vector<AdvancedField> fields;
};

struct BatteryAdvancedStatus {
  std::vector<AdvancedSection> sections;
};

// Convenience builder keeps driver conversions terse and uniform.
inline AdvancedField kv(String label, String value, String unit = "",
                        AdvancedSeverity severity = AdvancedSeverity::Normal) {
  AdvancedField f;
  f.kind = AdvancedFieldKind::KeyValue;
  f.label = label;
  f.value = value;
  f.unit = unit;
  f.severity = severity;
  return f;
}

#endif
