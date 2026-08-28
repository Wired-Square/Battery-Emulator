#ifndef TEST_ADVANCED_STATUS_RECORDER_H
#define TEST_ADVANCED_STATUS_RECORDER_H

#include <initializer_list>
#include <string>
#include <vector>

#include "../Software/src/battery/battery_advanced_status.h"

struct RecordingWriter : AdvancedStatusWriter {
  struct Field {
    bool is_table = false;
    std::string label, value, unit, catalogue;
    bool warned = false;
    std::vector<std::string> columns, row_keys;
    std::vector<std::vector<std::string>> rows;
  };
  struct Section {
    std::string title;
    std::vector<Field> fields;
  };
  std::vector<Section> sections;

  void section(const char* title = "") override { sections.push_back({title, {}}); }

  void kv(const char* label, const String& value, const char* unit = "",
          AdvancedSeverity severity = AdvancedSeverity::Normal) override {
    kv(label, value.c_str(), unit, severity);
  }

  void kv(const char* label, const char* value, const char* unit = "",
          AdvancedSeverity severity = AdvancedSeverity::Normal) override {
    Field f;
    f.label = label;
    f.value = value;
    f.unit = unit;
    f.warned = severity == AdvancedSeverity::Warning;
    sections.back().fields.push_back(f);
  }

  void table(const char* label, std::initializer_list<const char*> columns,
             const char* catalogue = nullptr) override {
    Field f;
    f.is_table = true;
    f.label = label;
    if (catalogue != nullptr) f.catalogue = catalogue;
    for (const char* c : columns) f.columns.push_back(c);
    sections.back().fields.push_back(f);
  }

  void row_begin(const char* key = nullptr) override {
    Field& t = sections.back().fields.back();
    t.rows.emplace_back();
    if (key != nullptr) t.row_keys.push_back(key);
  }

  void cell(const String& text) override {
    Field& t = sections.back().fields.back();
    t.rows.back().push_back(text.c_str());
  }

  void row_end() override {}
};

#endif
