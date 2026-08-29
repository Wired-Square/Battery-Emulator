#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "../../Software/src/communication/nvm/comm_nvm.h"
#include "../../Software/src/devboard/webserver/json_document_reader.h"
#include "../../Software/src/devboard/webserver/settings_api.h"
#include "../emul/Preferences.h"

namespace {


JsonDocument parse(const char* json) {
  JsonDocument doc;
  EXPECT_FALSE(deserializeJson(doc, json));
  return doc;
}

// A reader built from plain C++ containers rather than a parsed document.
class MapDocumentReader : public DocumentReader {
 public:
  std::map<std::string, DocumentValue> scalars;
  std::map<std::string, std::vector<std::map<std::string, DocumentValue>>> arrays;

  DocumentValue value(const char* key) const override {
    const auto it = scalars.find(key);
    return it == scalars.end() ? DocumentValue{} : it->second;
  }

  bool has_rows(const char* path) const override { return arrays.count(path) != 0; }

  size_t rows(const char* path) const override {
    const auto it = arrays.find(path);
    return it == arrays.end() ? 0 : it->second.size();
  }

  DocumentValue row_value(const char* path, size_t row, const char* key) const override {
    const auto it = arrays.find(path);
    if (it == arrays.end() || row >= it->second.size()) {
      return DocumentValue{};
    }
    const auto& cells = it->second[row];
    const auto cell = cells.find(key);
    return cell == cells.end() ? DocumentValue{} : cell->second;
  }
};

DocumentValue text(const char* value) {
  DocumentValue out;
  out.kind = ValueKind::String;
  out.text = value;
  return out;
}

DocumentValue integer(int64_t value) {
  DocumentValue out;
  out.kind = ValueKind::Int;
  out.integer = value;
  return out;
}

DocumentValue cleared() {
  DocumentValue out;
  out.kind = ValueKind::Null;
  return out;
}

}  // namespace

TEST(JsonDocumentReader, KeepsAbsentAndNullDistinct) {
  const JsonDocument doc = parse(R"({"values":{"HTTPPASS":null,"HTTPUSER":"admin"}})");
  const JsonDocumentReader reader(doc.as<JsonVariantConst>(), "values");

  EXPECT_TRUE(reader.value("HTTPPASS").cleared());
  EXPECT_FALSE(reader.value("HTTPPASS").absent());
  EXPECT_TRUE(reader.value("MQTTPASSWORD").absent());
  EXPECT_FALSE(reader.value("MQTTPASSWORD").cleared());
  EXPECT_TRUE(reader.value("HTTPPASS").missing());
  EXPECT_TRUE(reader.value("MQTTPASSWORD").missing());
}

TEST(JsonDocumentReader, ClassifiesScalarKinds) {
  const JsonDocument doc =
      parse(R"({"b":true,"i":-5,"u":4294967295,"f":1.5,"s":"x","big":9223372036854775807})");
  const JsonDocumentReader reader(doc.as<JsonVariantConst>());

  EXPECT_TRUE(reader.value("b").is_bool());
  EXPECT_TRUE(reader.value("b").as_bool());
  EXPECT_FALSE(reader.value("b").is_number());

  EXPECT_TRUE(reader.value("i").is_integer_in(INT32_MIN, INT32_MAX));
  EXPECT_FALSE(reader.value("i").is_integer_in(0, UINT32_MAX));
  EXPECT_TRUE(reader.value("u").is_integer_in(0, UINT32_MAX));
  EXPECT_FALSE(reader.value("u").is_integer_in(INT32_MIN, INT32_MAX));

  EXPECT_TRUE(reader.value("f").is_number());
  EXPECT_FALSE(reader.value("f").is_integer());
  EXPECT_DOUBLE_EQ(reader.value("f").as_number(), 1.5);

  EXPECT_TRUE(reader.value("s").is_string());
  EXPECT_STREQ(reader.value("s").as_text(), "x");
  EXPECT_EQ(reader.value("big").integer, INT64_MAX);
}

TEST(JsonDocumentReader, ReadsPathKeyedRows) {
  const JsonDocument doc =
      parse(R"({"dynamic":{"batteries":[{"slot":0,"type":7},{"slot":1}],"loadswitch":{"channels":[{"duty":50}]}}})");
  const JsonDocumentReader reader(doc.as<JsonVariantConst>());

  EXPECT_TRUE(reader.has_rows("dynamic.batteries"));
  EXPECT_EQ(reader.rows("dynamic.batteries"), 2u);
  EXPECT_EQ(reader.row_value("dynamic.batteries", 0, "type").integer, 7);
  EXPECT_TRUE(reader.row_value("dynamic.batteries", 1, "type").absent());
  EXPECT_EQ(reader.rows("dynamic.loadswitch.channels"), 1u);
  EXPECT_EQ(reader.row_value("dynamic.loadswitch.channels", 0, "duty").integer, 50);

  // An absent array must not read as an empty one: the primary-battery guard
  // only applies to a payload that actually carried a batteries section.
  EXPECT_FALSE(reader.has_rows("dynamic.termination"));
  EXPECT_EQ(reader.rows("dynamic.termination"), 0u);
}

TEST(DocumentReaderBackends, ApplySettingsRunsWithoutADeserialiser) {
  Preferences::resetAllForTests();
  MapDocumentReader body;
  body.scalars["MQTTSERVER"] = text("broker.local");
  body.scalars["MQTTPORT"] = integer(8883);
  body.arrays["dynamic.batteries"] = {{{"slot", integer(0)}, {"type", integer(0)}}};

  BatteryEmulatorSettingsStore store;
  const SettingsApplyResult result = apply_settings(store, body);

  ASSERT_TRUE(result.ok) << result.error.c_str();
  BatteryEmulatorSettingsStore reader(true);
  EXPECT_EQ(std::string(reader.getString("MQTTSERVER", "").c_str()), "broker.local");
  EXPECT_EQ(reader.getUInt("MQTTPORT", 0), 8883u);
}

TEST(DocumentReaderBackends, ExplicitNullClearsAStoredSecret) {
  Preferences::resetAllForTests();
  {
    BatteryEmulatorSettingsStore store;
    store.saveString("MQTTPASSWORD", "hunter2");
  }

  MapDocumentReader absent;
  {
    BatteryEmulatorSettingsStore store;
    ASSERT_TRUE(apply_settings(store, absent).ok);
  }
  EXPECT_EQ(std::string(BatteryEmulatorSettingsStore(true).getString("MQTTPASSWORD", "").c_str()), "hunter2");

  MapDocumentReader explicit_null;
  explicit_null.scalars["MQTTPASSWORD"] = cleared();
  {
    BatteryEmulatorSettingsStore store;
    ASSERT_TRUE(apply_settings(store, explicit_null).ok);
  }
  EXPECT_EQ(std::string(BatteryEmulatorSettingsStore(true).getString("MQTTPASSWORD", "").c_str()), "");
}
