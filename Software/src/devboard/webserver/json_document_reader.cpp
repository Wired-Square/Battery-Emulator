#include "json_document_reader.h"

#include <cstring>

namespace {

DocumentValue member(JsonObjectConst object, const char* key) {
  DocumentValue out;
  if (object.isNull() || !object.containsKey(key)) {
    return out;
  }
  JsonVariantConst value = object[key];
  if (value.isNull()) {
    out.kind = ValueKind::Null;
  } else if (value.is<bool>()) {
    out.kind = ValueKind::Bool;
    out.boolean_value = value.as<bool>();
  } else if (value.is<int64_t>()) {
    out.kind = ValueKind::Int;
    out.integer = value.as<int64_t>();
  } else if (value.is<uint64_t>()) {
    out.kind = ValueKind::Uint;
    out.integer = static_cast<int64_t>(value.as<uint64_t>());
  } else if (value.is<float>()) {
    out.kind = ValueKind::Float;
    out.number = value.as<double>();
  } else if (value.is<const char*>()) {
    out.kind = ValueKind::String;
    out.text = value.as<const char*>();
  }
  return out;
}

}  // namespace

JsonDocumentReader::JsonDocumentReader(JsonVariantConst root, const char* scalar_map) : root_(root) {
  scalars_ = scalar_map == nullptr ? root.as<JsonObjectConst>() : root[scalar_map].as<JsonObjectConst>();
}

JsonVariantConst JsonDocumentReader::resolve(const char* path) const {
  JsonVariantConst at = root_;
  const char* segment = path;
  while (true) {
    const char* dot = std::strchr(segment, '.');
    if (dot == nullptr) {
      return at[segment];
    }
    char name[32];
    const size_t len = static_cast<size_t>(dot - segment);
    if (len >= sizeof(name)) {
      return JsonVariantConst();
    }
    std::memcpy(name, segment, len);
    name[len] = '\0';
    at = at[name];
    segment = dot + 1;
  }
}

DocumentValue JsonDocumentReader::value(const char* key) const {
  return member(scalars_, key);
}

bool JsonDocumentReader::has_rows(const char* path) const {
  return resolve(path).is<JsonArrayConst>();
}

size_t JsonDocumentReader::rows(const char* path) const {
  return resolve(path).as<JsonArrayConst>().size();
}

DocumentValue JsonDocumentReader::row_value(const char* path, size_t row, const char* key) const {
  return member(resolve(path).as<JsonArrayConst>()[row].as<JsonObjectConst>(), key);
}
