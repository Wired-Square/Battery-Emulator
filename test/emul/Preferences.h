#ifndef PREFERENCES
#define PREFERENCES

#include <WString.h>
#include <stdint.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <variant>

// Host-test fake for the ESP32 Preferences (NVS) API, backing the real
// BatteryEmulatorSettingsStore under ctest. The store is process-global and
// namespace-keyed like NVS (a namespace reopened with begin() keeps its
// values); resetAllForTests() clears it between fixtures.
class Preferences {
 public:
  Preferences() {}

  bool begin(const char* name, bool readOnly = false, const char* partition_label = nullptr) {
    currentNamespace = name;
    readOnlyMode = readOnly;
    store()[currentNamespace];  // ensure the namespace exists once opened
    return true;
  }

  void end() { currentNamespace.clear(); }

  bool clear() {
    if (readOnlyMode) {
      return false;
    }
    namespaceValues().clear();
    return true;
  }

  bool remove(const char* key) {
    if (readOnlyMode) {
      return false;
    }
    return namespaceValues().erase(key) > 0;
  }

  size_t putInt(const char* key, int32_t value) { return put(key, value); }
  size_t putUInt(const char* key, uint32_t value) { return put(key, value); }
  size_t putBool(const char* key, bool value) { return put(key, value); }
  size_t putString(const char* key, const char* value) { return put(key, String(value)); }
  size_t putString(const char* key, String value) { return put(key, value); }

  bool isKey(const char* key) {
    auto& values = namespaceValues();
    return values.find(key) != values.end();
  }

  int32_t getInt(const char* key, int32_t defaultValue = 0) { return get(key, defaultValue); }
  uint32_t getUInt(const char* key, uint32_t defaultValue = 0) { return get(key, defaultValue); }
  bool getBool(const char* key, bool defaultValue = false) { return get(key, defaultValue); }

  size_t getString(const char* key, char* value, size_t maxLen) {
    if (maxLen == 0) {
      return 0;
    }
    const String stored = getString(key, String());
    const size_t copied = std::min(static_cast<size_t>(stored.length()), maxLen - 1);
    std::memcpy(value, stored.c_str(), copied);
    value[copied] = '\0';
    return copied;
  }

  String getString(const char* key, String defaultValue = String()) { return get(key, defaultValue); }

  // Fixtures call this in SetUp() so the shared process-global store does not
  // leak values between tests (R8).
  static void resetAllForTests() { store().clear(); }

 private:
  using Value = std::variant<int32_t, uint32_t, bool, String>;
  using Namespace = std::map<std::string, Value>;

  static std::map<std::string, Namespace>& store() {
    static std::map<std::string, Namespace> instance;
    return instance;
  }

  Namespace& namespaceValues() { return store()[currentNamespace]; }

  template <typename T>
  size_t put(const char* key, const T& value) {
    if (readOnlyMode) {
      return 0;
    }
    namespaceValues()[key] = value;
    return sizeof(T);
  }

  template <typename T>
  T get(const char* key, const T& defaultValue) {
    auto& values = namespaceValues();
    const auto it = values.find(key);
    if (it == values.end() || !std::holds_alternative<T>(it->second)) {
      return defaultValue;
    }
    return std::get<T>(it->second);
  }

  std::string currentNamespace;
  bool readOnlyMode = false;
};
#endif
