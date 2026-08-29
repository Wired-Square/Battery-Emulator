#ifndef DOCUMENT_READER_H
#define DOCUMENT_READER_H

#include <cstddef>
#include <cstdint>

// Absent and Null stay distinct: on a password key an explicit null means
// "clear the stored secret" where an absent key means "preserve it", and
// collapsing the two wipes passwords.
enum class ValueKind : uint8_t { Absent, Null, Bool, Int, Uint, Float, String };

struct DocumentValue {
  ValueKind kind = ValueKind::Absent;
  bool boolean_value = false;
  int64_t integer = 0;
  double number = 0.0;
  const char* text = nullptr;

  bool absent() const { return kind == ValueKind::Absent; }
  bool cleared() const { return kind == ValueKind::Null; }
  bool missing() const { return kind == ValueKind::Absent || kind == ValueKind::Null; }

  bool is_bool() const { return kind == ValueKind::Bool; }
  bool is_string() const { return kind == ValueKind::String; }
  bool is_integer() const { return kind == ValueKind::Int || kind == ValueKind::Uint; }
  bool is_number() const { return is_integer() || kind == ValueKind::Float; }
  bool is_integer_in(int64_t low, int64_t high) const {
    return is_integer() && integer >= low && integer <= high;
  }

  double as_number() const { return kind == ValueKind::Float ? number : static_cast<double>(integer); }
  bool as_bool() const { return boolean_value; }
  const char* as_text() const { return text; }
};

class ValueSource {
 public:
  virtual ~ValueSource() = default;
  virtual DocumentValue value(const char* key) const = 0;
};

// A request body: a flat scalar map plus named arrays of flat objects, the whole
// shape the POST endpoints use. Array paths are dotted, as "dynamic.batteries".
class DocumentReader : public ValueSource {
 public:
  virtual bool has_rows(const char* path) const = 0;
  virtual size_t rows(const char* path) const = 0;
  virtual DocumentValue row_value(const char* path, size_t row, const char* key) const = 0;
};

class DocumentRow : public ValueSource {
 public:
  DocumentRow(const DocumentReader& reader, const char* path, size_t row)
      : reader_(reader), path_(path), row_(row) {}

  DocumentValue value(const char* key) const override { return reader_.row_value(path_, row_, key); }

 private:
  const DocumentReader& reader_;
  const char* path_;
  size_t row_;
};

#endif
