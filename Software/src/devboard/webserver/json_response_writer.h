#ifndef JSON_RESPONSE_WRITER_H
#define JSON_RESPONSE_WRITER_H

#include <WString.h>

#include "response_writer.h"

// Emits, without a document, the byte stream ArduinoJson's compact serialiser
// would have produced.
class JsonResponseWriter : public ResponseWriter {
 public:
  static constexpr size_t kBufferBytes = 256;
  static constexpr uint8_t kMaxDepth = 12;

  explicit JsonResponseWriter(ResponseSink& sink) : sink_(sink) {}
  ~JsonResponseWriter() override { flush(); }

  void begin_object(const char* key = nullptr) override;
  void end_object() override;
  void begin_array(const char* key = nullptr) override;
  void end_array() override;

  void field(const char* key, bool value) override;
  void field(const char* key, int64_t value) override;
  void field(const char* key, double value) override;
  void field(const char* key, const char* value) override;
  void field(const char* key, const char* value, size_t len) override;
  void null_field(const char* key) override;

  using ResponseWriter::element;
  using ResponseWriter::field;

  void flush();
  void put(const char* text, size_t len);
  void put(char c);

 private:
  uint16_t depth_bit() const;
  void separate(const char* key);

  ResponseSink& sink_;
  char buffer_[kBufferBytes] = {};
  size_t used_ = 0;
  uint16_t first_mask_ = 0xFFFF;
  uint8_t depth_ = 0;
};

String render_json(const ResponseProducer& producer);

#endif
