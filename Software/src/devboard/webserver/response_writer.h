#ifndef RESPONSE_WRITER_H
#define RESPONSE_WRITER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

class ResponseSink {
 public:
  virtual ~ResponseSink() = default;
  virtual void write(const char* text, size_t len) = 0;
};

// Forward-only response assembly. A null key means "element of the enclosing
// array".
class ResponseWriter {
 public:
  virtual ~ResponseWriter() = default;

  virtual void begin_object(const char* key = nullptr) = 0;
  virtual void end_object() = 0;
  virtual void begin_array(const char* key = nullptr) = 0;
  virtual void end_array() = 0;

  virtual void field(const char* key, bool value) = 0;
  virtual void field(const char* key, int64_t value) = 0;
  virtual void field(const char* key, double value) = 0;
  virtual void field(const char* key, const char* value) = 0;
  virtual void field(const char* key, const char* value, size_t len) = 0;
  virtual void null_field(const char* key) = 0;

  template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
  void field(const char* key, T value) {
    field(key, static_cast<int64_t>(value));
  }

  void element(bool value) { field(nullptr, value); }
  void element(double value) { field(nullptr, value); }
  void element(const char* value) { field(nullptr, value); }
  void element(const char* value, size_t len) { field(nullptr, value, len); }
  void null_element() { null_field(nullptr); }

  template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
  void element(T value) {
    field(nullptr, static_cast<int64_t>(value));
  }
};

using ResponseProducer = std::function<void(ResponseWriter&)>;

#endif
