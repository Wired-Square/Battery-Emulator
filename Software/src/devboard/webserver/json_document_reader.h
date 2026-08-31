#ifndef JSON_DOCUMENT_READER_H
#define JSON_DOCUMENT_READER_H

#include <cstddef>
#include <cstdint>

#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "document_reader.h"

class JsonDocumentReader : public DocumentReader {
 public:
  JsonDocumentReader() = default;
  // scalar_map names the object value() reads from; null means the root itself.
  explicit JsonDocumentReader(JsonVariantConst root, const char* scalar_map = nullptr);

  DocumentValue value(const char* key) const override;
  bool has_rows(const char* path) const override;
  size_t rows(const char* path) const override;
  DocumentValue row_value(const char* path, size_t row, const char* key) const override;

 private:
  JsonVariantConst resolve(const char* path) const;

  JsonVariantConst root_;
  JsonObjectConst scalars_;
};

class JsonRequestDocument {
 public:
  bool parse(uint8_t* body, size_t length, const char* scalar_map);

  const DocumentReader& reader() const { return reader_; }

 private:
  JsonDocument document_;
  JsonDocumentReader reader_;
};

#endif
