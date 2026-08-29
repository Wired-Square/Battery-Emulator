#include "json_response_writer.h"

#include <cstring>

#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"

namespace {

class WriterSink {
 public:
  explicit WriterSink(JsonResponseWriter* out) : out_(out) {}

  size_t write(uint8_t c) {
    out_->put(static_cast<char>(c));
    return 1;
  }

  size_t write(const uint8_t* data, size_t len) {
    out_->put(reinterpret_cast<const char*>(data), len);
    return len;
  }

 private:
  JsonResponseWriter* out_;
};

using Formatter = ArduinoJson::detail::TextFormatter<WriterSink>;

Formatter formatter_for(JsonResponseWriter* out) {
  return Formatter(WriterSink(out));
}

class StringSink : public ResponseSink {
 public:
  void write(const char* text, size_t len) override {
    out.concat(text, static_cast<unsigned int>(len));
  }

  String out;
};

}  // namespace

void JsonResponseWriter::put(char c) {
  if (used_ == kBufferBytes) {
    flush();
  }
  buffer_[used_++] = c;
}

void JsonResponseWriter::put(const char* text, size_t len) {
  while (len > 0) {
    if (used_ == kBufferBytes) {
      flush();
    }
    const size_t room = kBufferBytes - used_;
    const size_t chunk = len < room ? len : room;
    std::memcpy(buffer_ + used_, text, chunk);
    used_ += chunk;
    text += chunk;
    len -= chunk;
  }
}

void JsonResponseWriter::flush() {
  if (used_ > 0) {
    sink_.write(buffer_, used_);
    used_ = 0;
  }
}

uint16_t JsonResponseWriter::depth_bit() const {
  return static_cast<uint16_t>(1u << (depth_ < kMaxDepth ? depth_ : kMaxDepth));
}

void JsonResponseWriter::separate(const char* key) {
  const uint16_t bit = depth_bit();
  if (first_mask_ & bit) {
    first_mask_ = static_cast<uint16_t>(first_mask_ & ~bit);
  } else {
    put(',');
  }
  if (key != nullptr) {
    formatter_for(this).writeString(key);
    put(':');
  }
}

void JsonResponseWriter::begin_object(const char* key) {
  separate(key);
  put('{');
  depth_++;
  first_mask_ = static_cast<uint16_t>(first_mask_ | depth_bit());
}

void JsonResponseWriter::end_object() {
  if (depth_ > 0) {
    depth_--;
  }
  put('}');
}

void JsonResponseWriter::begin_array(const char* key) {
  separate(key);
  put('[');
  depth_++;
  first_mask_ = static_cast<uint16_t>(first_mask_ | depth_bit());
}

void JsonResponseWriter::end_array() {
  if (depth_ > 0) {
    depth_--;
  }
  put(']');
}

void JsonResponseWriter::field(const char* key, bool value) {
  separate(key);
  formatter_for(this).writeBoolean(value);
}

void JsonResponseWriter::field(const char* key, int64_t value) {
  separate(key);
  formatter_for(this).writeInteger(value);
}

void JsonResponseWriter::field(const char* key, double value) {
  separate(key);
  // ArduinoJson demotes a double that survives a float round trip and prints it
  // with 6 decimals rather than 9; matching that keeps the bytes identical.
  const float demoted = static_cast<float>(value);
  if (static_cast<double>(demoted) == value) {
    formatter_for(this).writeFloat(demoted);
  } else {
    formatter_for(this).writeFloat(value);
  }
}

void JsonResponseWriter::field(const char* key, const char* value) {
  if (value == nullptr) {
    return null_field(key);
  }
  separate(key);
  formatter_for(this).writeString(value);
}

void JsonResponseWriter::field(const char* key, const char* value, size_t len) {
  if (value == nullptr) {
    return null_field(key);
  }
  separate(key);
  formatter_for(this).writeString(value, len);
}

void JsonResponseWriter::null_field(const char* key) {
  separate(key);
  put("null", 4);
}

String render_json(const ResponseProducer& producer) {
  StringSink sink;
  {
    JsonResponseWriter writer(sink);
    producer(writer);
  }
  return sink.out;
}
