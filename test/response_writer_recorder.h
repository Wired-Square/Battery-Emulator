#ifndef TEST_RESPONSE_WRITER_RECORDER_H
#define TEST_RESPONSE_WRITER_RECORDER_H

#include <string>
#include <vector>

#include "../Software/src/devboard/webserver/response_writer.h"

// A second, deliberately non-JSON backend: flattens the document to
// "wifi.ssid=home" / "batteries[0].cells[1]=3301" lines.
class RecordingResponseWriter : public ResponseWriter {
 public:
  std::vector<std::string> entries;

  void begin_object(const char* key = nullptr) override { open(key); }
  void end_object() override { stack_.pop_back(); }
  void begin_array(const char* key = nullptr) override { open(key); }
  void end_array() override { stack_.pop_back(); }

  void field(const char* key, bool value) override { record(key, value ? "true" : "false"); }
  void field(const char* key, int64_t value) override { record(key, std::to_string(value)); }
  void field(const char* key, double value) override { record(key, std::to_string(value)); }
  void field(const char* key, const char* value) override {
    record(key, value == nullptr ? "null" : std::string(value));
  }
  void field(const char* key, const char* value, size_t len) override {
    record(key, value == nullptr ? "null" : std::string(value, len));
  }
  void null_field(const char* key) override { record(key, "null"); }

  using ResponseWriter::element;
  using ResponseWriter::field;

 private:
  struct Frame {
    std::string prefix;
    size_t index = 0;
  };

  std::string path(const char* key) {
    if (stack_.empty() && key == nullptr) {
      return std::string();
    }
    const std::string prefix = stack_.empty() ? std::string() : stack_.back().prefix;
    if (key != nullptr) {
      return prefix.empty() ? std::string(key) : prefix + "." + key;
    }
    const size_t index = stack_.empty() ? 0 : stack_.back().index++;
    return prefix + "[" + std::to_string(index) + "]";
  }

  void open(const char* key) { stack_.push_back({path(key), 0}); }

  void record(const char* key, const std::string& value) { entries.push_back(path(key) + "=" + value); }

  std::vector<Frame> stack_;
};

#endif
