#include <gtest/gtest.h>
#include <cstring>
#include <set>
#include <string>

#include "../Software/src/communication/nvm/comm_nvm.h"
#include "../Software/src/datalayer/datalayer.h"
#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_lilygo.h"
#include "../Software/src/battery/BATTERIES.h"
#include "../Software/src/inverter/INVERTERS.h"
#include "../Software/src/battery/battery_slots.h"
#include "../Software/src/devboard/webserver/settings_api.h"
#include "../Software/src/devboard/webserver/json_document_reader.h"
#include "../Software/src/devboard/webserver/json_response_writer.h"

static SettingsApplyResult apply_settings_body(BatteryEmulatorSettingsStore& store, const JsonDocument& body) {
  JsonDocumentReader reader(body.as<JsonVariantConst>(), "values");
  return apply_settings(store, reader);
}

static String settings_json(BatteryEmulatorSettingsStore& store, bool reboot_required = false) {
  return render_json([&](ResponseWriter& out) { write_settings(out, store, reboot_required); });
}
#include "../Software/src/devboard/webserver/battery_slot_api.h"
#include "../Software/src/lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "emul/Preferences.h"

extern std::string password;  // live WiFi STA password global, refreshed by apply on PASSWORD change
String default_hostname();     // firmware wifi.cpp; host fake in emul/wifi.cpp

// esp32hal is process-global; save and restore it so init_hal() in one test
// cannot leak a live HAL into later, order-dependent tests.
struct HalScope {
  Esp32Hal* saved = esp32hal;
  HalScope() { init_hal(); }
  ~HalScope() { esp32hal = saved; }
};

struct NullHalScope {
  Esp32Hal* saved = esp32hal;
  NullHalScope() { esp32hal = nullptr; }
  ~NullHalScope() { esp32hal = saved; }
};

// The Preferences fake is process-global; each test must start from a clean
// "batterySettings" namespace.
class SettingsApiTest : public ::testing::Test {
 protected:
  void SetUp() override { Preferences::resetAllForTests(); }

  static JsonDocument parse_values(const String& json) {
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, json.c_str());
    EXPECT_FALSE(err) << err.c_str();
    return doc;
  }
};

TEST_F(SettingsApiTest, EmitsScalarsWithCorrectJsonTypes) {
  {
    BatteryEmulatorSettingsStore store;
    store.saveBool("MQTTENABLED", true);
    store.saveUInt("MQTTPORT", 8883);
    store.saveString("MQTTSERVER", "broker.local");
    store.saveUInt("BATTPVMAX", 4125);      // FloatX10 -> 412.5
    store.saveUInt("MQTTPUBLISHMS", 7000);  // SecondsToMs -> 7
  }

  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonObjectConst values = doc["values"];

  EXPECT_TRUE(values["MQTTENABLED"].is<bool>());
  EXPECT_TRUE(values["MQTTENABLED"].as<bool>());

  EXPECT_TRUE(values["MQTTPORT"].is<unsigned int>());
  EXPECT_EQ(values["MQTTPORT"].as<uint32_t>(), 8883u);

  EXPECT_STREQ(values["MQTTSERVER"].as<const char*>(), "broker.local");

  EXPECT_DOUBLE_EQ(values["BATTPVMAX"].as<double>(), 412.5);

  EXPECT_EQ(values["MQTTPUBLISHMS"].as<uint32_t>(), 7u);
}

TEST_F(SettingsApiTest, FloatStringCarriesNegativeSign) {
  {
    BatteryEmulatorSettingsStore store;
    store.saveString("CTOFFSET", "-2.5");  // FloatString, the sole negative-carrying field
  }

  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  EXPECT_STREQ(doc["values"]["CTOFFSET"].as<const char*>(), "-2.5");
}

TEST_F(SettingsApiTest, WireKeyIsTheNvsKey) {
  {
    BatteryEmulatorSettingsStore store;
    store.saveUInt("INVTYPE", 3);
  }

  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonObjectConst values = doc["values"];

  EXPECT_FALSE(values["INVTYPE"].isNull());
  EXPECT_EQ(values["INVTYPE"].as<uint32_t>(), 3u);

  JsonObjectConst balancing = doc["dynamic"]["balancing"][0];
  for (size_t i = 0; i < setting_count(); i++) {
    const SettingField& field = *setting_at(i).field;
    JsonObjectConst home = field.live.scope == SettingScope::BatterySlot ? balancing : values;
    EXPECT_FALSE(home[field.key].isNull())
        << field.key << " is missing, so some field still renames itself on the wire";
  }
}

TEST_F(SettingsApiTest, EveryDeviceRowResolvesToARegisteredOwner) {
  std::set<uint32_t> battery_ids;
  for (size_t i = 0; i < battery_type_settings_count(); i++) {
    battery_ids.insert(static_cast<uint32_t>(battery_type_settings_at(i).id));
  }
  std::set<uint32_t> inverter_ids;
  for (size_t i = 0; i < inverter_type_settings_count(); i++) {
    inverter_ids.insert(static_cast<uint32_t>(inverter_type_settings_at(i).id));
  }

  HalScope hal;
  BatteryEmulatorSettingsStore store;
  const JsonDocument doc = parse_values(settings_json(store));
  size_t device_rows = 0;
  for (JsonObjectConst entry : doc["schema"].as<JsonArrayConst>()) {
    JsonArrayConst owners = entry["owners"];
    if (owners.isNull()) {
      continue;
    }
    device_rows++;
    const char* key = entry["key"];
    const std::string domain = entry["domain"].as<const char*>();
    EXPECT_GT(owners.size(), 0u) << key << " is a device row no driver claims, so it can never be shown";
    const std::set<uint32_t>& registered = domain == "battery" ? battery_ids : inverter_ids;
    for (JsonVariantConst owner : owners) {
      EXPECT_EQ(registered.count(owner.as<uint32_t>()), 1u)
          << key << " names " << domain << " type " << owner.as<uint32_t>() << ", which no registry row declares";
    }
  }
  EXPECT_GT(device_rows, 0u);
}

TEST_F(SettingsApiTest, EveryDeclaredCapabilityIsClaimedByARow) {
  BatteryCapabilities battery_declared = 0;
  for (size_t i = 0; i < battery_type_settings_count(); i++) {
    battery_declared |= battery_type_settings_at(i).capabilities;
  }
  InverterCapabilities inverter_declared = 0;
  for (size_t i = 0; i < inverter_type_settings_count(); i++) {
    inverter_declared |= inverter_type_settings_at(i).capabilities;
  }

  BatteryCapabilities battery_claimed = 0;
  InverterCapabilities inverter_claimed = 0;
  for (size_t i = 0; i < setting_count(); i++) {
    const SettingRef ref = setting_at(i);
    if (ref.device == nullptr) {
      continue;
    }
    if (ref.domain == SettingDomain::Battery) {
      battery_claimed |= ref.device->capability;
    } else if (ref.domain == SettingDomain::Inverter) {
      inverter_claimed |= ref.device->capability;
    }
  }

  EXPECT_EQ(battery_declared & ~battery_claimed, 0u)
      << "battery capability bits " << (battery_declared & ~battery_claimed)
      << " are declared on a registry row but gate no setting, so the rows they were meant to own are "
         "shown to every configuration";
  EXPECT_EQ(inverter_declared & ~inverter_claimed, 0u)
      << "inverter capability bits " << (inverter_declared & ~inverter_claimed)
      << " are declared on a registry row but gate no setting";
}

TEST_F(SettingsApiTest, BydAutoCalibrationRowsBelongToTheBydDriver) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  const JsonDocument doc = parse_values(settings_json(store));
  size_t seen = 0;
  for (JsonObjectConst entry : doc["schema"].as<JsonArrayConst>()) {
    if (entry["section"].isNull() || std::string(entry["section"].as<const char*>()) != "bydautocal") {
      continue;
    }
    seen++;
    JsonArrayConst owners = entry["owners"];
    ASSERT_EQ(owners.size(), 1u) << entry["key"].as<const char*>();
    EXPECT_EQ(owners[0].as<uint32_t>(), static_cast<uint32_t>(BatteryType::BydAtto3))
        << entry["key"].as<const char*>() << " would render the auto-calibration card on a non-BYD install";
  }
  EXPECT_EQ(seen, 9u);
}

TEST(SettingsTableTest, EveryKeyIsUniqueAcrossStorageKinds) {
  std::set<std::string> seen;
  for (size_t i = 0; i < setting_count(); i++) {
    const std::string key = setting_at(i).field->key;
    EXPECT_TRUE(seen.insert(key).second) << key << " is claimed twice; values{} is one flat namespace";
  }
}

TEST_F(SettingsApiTest, EmitsDefaultsForAbsentKeys) {
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonObjectConst values = doc["values"];

  // WIFIAPENABLED defaults on even with no stored value.
  EXPECT_TRUE(values["WIFIAPENABLED"].as<bool>());
  EXPECT_EQ(values["PYLONBAUD"].as<uint32_t>(), 500u);
  EXPECT_EQ(values["MQTTPORT"].as<uint32_t>(), 1883u);
  // SecondsToMs default (5 s) survives the ms<->s transform with no stored key.
  EXPECT_EQ(values["MQTTPUBLISHMS"].as<uint32_t>(), 5u);
}

TEST_F(SettingsApiTest, TeslaGatewayDefaultsMatchTheDriver) {
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonObjectConst values = doc["values"];

  EXPECT_EQ(values["GTWCOUNTRY"].as<uint32_t>(), kTeslaGtwCountryDefault);
  EXPECT_EQ(values["GTWRHD"].as<bool>(), kTeslaGtwRightHandDriveDefault);
  EXPECT_EQ(values["GTWMAPREG"].as<uint32_t>(), kTeslaGtwMapRegionDefault);
  EXPECT_EQ(values["GTWCHASSIS"].as<uint32_t>(), kTeslaGtwChassisTypeDefault);
  EXPECT_EQ(values["GTWPACK"].as<uint32_t>(), kTeslaGtwPackEnergyDefault);
}

TEST_F(SettingsApiTest, EmitsBatteryOptionsNoneFirst) {
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonArrayConst battery = doc["options"]["battery"];

  ASSERT_FALSE(battery.isNull());
  ASSERT_GT(battery.size(), 0u);
  EXPECT_EQ(battery[0]["v"].as<int>(), 0);
  EXPECT_STREQ(battery[0]["n"].as<const char*>(), "None");
  for (JsonObjectConst opt : battery) {
    EXPECT_TRUE(opt["v"].is<int>());
    EXPECT_TRUE(opt["n"].is<const char*>());
  }
}

TEST_F(SettingsApiTest, EmitsMapAndEnumOptionLists) {
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonObjectConst options = doc["options"];

  for (const char* key : {"chemistry", "inverter", "charger", "shunt", "attenuation", "ledmode"}) {
    JsonArrayConst list = options[key];
    ASSERT_FALSE(list.isNull()) << key;
    ASSERT_GT(list.size(), 0u) << key;
    EXPECT_TRUE(list[0]["v"].is<int>()) << key;
    EXPECT_TRUE(list[0]["n"].is<const char*>()) << key;
  }
}

// The interfaces array needs a live HAL; tests without a HalScope leave esp32hal null.
TEST_F(SettingsApiTest, EmitsSelectableInterfacesWhenHalPresent) {
  HalScope hal;
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonArrayConst interfaces = doc["interfaces"];

  ASSERT_FALSE(interfaces.isNull());
  ASSERT_GT(interfaces.size(), 0u);
  for (JsonObjectConst iface : interfaces) {
    EXPECT_TRUE(iface["id"].is<uint32_t>());
    EXPECT_TRUE(iface["index"].is<unsigned int>());
    EXPECT_TRUE(iface["name"].is<const char*>());
  }
}

TEST_F(SettingsApiTest, InterfacesSortedAlphabeticallyByName) {
  HalScope hal;
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonArrayConst interfaces = doc["interfaces"];

  ASSERT_GT(interfaces.size(), 1u);
  const char* previous = nullptr;
  for (JsonObjectConst iface : interfaces) {
    const char* name = iface["name"].as<const char*>();
    if (previous != nullptr) {
      EXPECT_LT(std::strcmp(previous, name), 0) << previous << " !< " << name;
    }
    previous = name;
  }
}

TEST_F(SettingsApiTest, UnsetCommResolvesToASelectableInterface) {
  HalScope hal;
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));

  const uint32_t batt_comm = doc["dynamic"]["batteries"][0]["comm"].as<uint32_t>();
  bool matches = false;
  for (JsonObjectConst iface : doc["interfaces"].as<JsonArrayConst>()) {
    if (iface["id"].as<uint32_t>() == batt_comm) {
      matches = true;
      break;
    }
  }
  EXPECT_TRUE(matches) << "battery slot 0 comm=" << batt_comm << " matches no interfaces[].id";
}

TEST_F(SettingsApiTest, BatterySlotsRoundTripThroughTheDynamicSection) {
  {
    BatteryEmulatorSettingsStore store;
    store.saveUInt("BATTTYPE", (uint32_t)BatteryType::NissanLeaf);
    store.saveUInt("BATT3TYPE", (uint32_t)BatteryType::NissanLeaf);
    store.saveBool("CNTCTRLTRI", true);
  }

  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonArrayConst batteries = doc["dynamic"]["batteries"];

  ASSERT_EQ(batteries.size(), (size_t)kMaxBatterySlots) << "every slot is emitted uniformly, holes included";
  EXPECT_EQ(batteries[0]["type"].as<uint32_t>(), (uint32_t)BatteryType::NissanLeaf);
  EXPECT_EQ(batteries[1]["type"].as<uint32_t>(), (uint32_t)BatteryType::None);
  EXPECT_EQ(batteries[2]["type"].as<uint32_t>(), (uint32_t)BatteryType::NissanLeaf);
  EXPECT_TRUE(batteries[2]["contactor_control"].as<bool>());
  EXPECT_TRUE(doc["values"]["battery"].isNull()) << "the flat battery scalars left the schema with the slot section";

  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  JsonObject entry = body["dynamic"]["batteries"].add<JsonObject>();
  entry["slot"] = 1;
  entry["type"] = (uint32_t)BatteryType::NissanLeaf;
  entry["contactor_control"] = true;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_TRUE(r.reboot_required);
  EXPECT_EQ(store.getUInt("BATT2TYPE", 0), (uint32_t)BatteryType::NissanLeaf);
  EXPECT_TRUE(store.getBool("CNTCTRLDBL", false));
  EXPECT_FALSE(store.settingExists("BATT2COMM")) << "an omitted comm sub-field must preserve, never write";
  EXPECT_EQ(store.getUInt("BATT3TYPE", 999), (uint32_t)BatteryType::NissanLeaf)
      << "an absent slot entry preserves the stored slot, never wipes it";

  const auto again = apply_settings_body(store, body);
  EXPECT_TRUE(again.ok) << again.error.c_str();
  EXPECT_FALSE(again.reboot_required) << "re-posting identical slot values must not demand a reboot";
}

TEST_F(SettingsApiTest, PostWithoutBatterySectionLeavesStoredBadShapeAlone) {
  BatteryEmulatorSettingsStore store;
  store.saveUInt("BATT2TYPE", (uint32_t)BatteryType::NissanLeaf);
  JsonDocument body;
  body["values"]["MQTTENABLED"] = true;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << "a POST that does not touch the battery section cannot create the bad shape, and rejecting it "
                       "would lock every unrelated setting on a box whose boot already latched the config fault";
  EXPECT_TRUE(store.getBool("MQTTENABLED", false));
}

TEST_F(SettingsApiTest, BatteryOptionsHaveExactlyOneNoneFirst) {
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonArrayConst battery = doc["options"]["battery"];

  ASSERT_GT(battery.size(), 0u);
  EXPECT_EQ(battery[0]["v"].as<int>(), 0);
  int none_count = 0;
  for (JsonObjectConst opt : battery) {
    if (opt["v"].as<int>() == 0) {
      none_count++;
    }
  }
  EXPECT_EQ(none_count, 1);
}

TEST_F(SettingsApiTest, BatteryOptionLabelsAscendingAfterNone) {
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonArrayConst battery = doc["options"]["battery"];

  ASSERT_GT(battery.size(), 1u);
  for (size_t i = 2; i < battery.size(); i++) {
    const char* previous = battery[i - 1]["n"].as<const char*>();
    const char* current = battery[i]["n"].as<const char*>();
    EXPECT_LT(std::strcmp(previous, current), 0) << previous << " !< " << current;
  }
}

TEST_F(SettingsApiTest, NullHalOmitsInterfacesAndDynamic) {
  NullHalScope no_hal;
  EXPECT_EQ(esp32hal, nullptr);
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));

  EXPECT_TRUE(doc["interfaces"].isNull());
  EXPECT_TRUE(doc["dynamic"]["termination"].isNull());
  EXPECT_TRUE(doc["dynamic"]["loadswitch"].isNull());
  EXPECT_FALSE(doc["dynamic"]["batteries"].isNull()) << "battery slots exist regardless of HAL presence";
}

TEST_F(SettingsApiTest, BuildReturnsNonEmptyOnSuccess) {
  BatteryEmulatorSettingsStore reader(true);
  EXPECT_GT(settings_json(reader).length(), 0u);
}

// POST echoes the applier's reboot_required through meta; GET (default arg) reports false.
TEST_F(SettingsApiTest, MetaRebootRequiredReflectsArgument) {
  BatteryEmulatorSettingsStore reader(true);

  const JsonDocument on = parse_values(settings_json(reader, true));
  EXPECT_TRUE(on["meta"]["reboot_required"].as<bool>());

  const JsonDocument off = parse_values(settings_json(reader));
  EXPECT_FALSE(off["meta"]["reboot_required"].as<bool>());
}

// A key the server omits is client-owned (CLIENT_OPTIONS in settings.js) and can't be told
// apart here from a forgotten emit, so this only asserts emitted lists are non-empty; the
// settings.js "Missing options" fallback is what catches an unresolved key.
TEST_F(SettingsApiTest, ServerEmittedDropdownsAreNonEmpty) {
  HalScope hal;
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonObjectConst options = doc["options"];
  JsonArrayConst interfaces = doc["interfaces"];

  for (size_t i = 0; i < setting_count(); i++) {
    const SettingField& field = *setting_at(i).field;
    if (field.type == SettingType::EnumUint) {
      EXPECT_NE(field.options_key, nullptr) << field.key;
    }
    if (field.type == SettingType::InterfacePacked) {
      EXPECT_GT(interfaces.size(), 0u) << field.key;
    } else if (field.options_key != nullptr) {
      JsonArrayConst list = options[field.options_key];
      if (!list.isNull()) {
        EXPECT_GT(list.size(), 0u) << field.key << " -> options." << field.options_key;
      }
    }
  }
}

// schema folds in the old optionsKeys map.
TEST_F(SettingsApiTest, EmitsSchemaEntryPerField) {
  HalScope hal;
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonArrayConst schema = doc["schema"];

  ASSERT_FALSE(schema.isNull());
  // The board's GPIO-option rows are appended after the declared fields, so the
  // declared rows stay index-aligned with the settings tables.
  ASSERT_EQ(schema.size(), setting_count() + esp32hal->gpio_options().group_count);

  for (size_t i = 0; i < setting_count(); i++) {
    const SettingField& field = *setting_at(i).field;
    JsonObjectConst entry = schema[i];
    EXPECT_STREQ(entry["key"].as<const char*>(), field.key);
    EXPECT_STREQ(entry["category"].as<const char*>(), field.category);

    const char* expected_type = nullptr;
    switch (field.type) {
      case SettingType::Bool:
        expected_type = "bool";
        break;
      case SettingType::Uint:
        expected_type = "uint";
        break;
      case SettingType::Int:
        expected_type = "int";
        break;
      case SettingType::StringVal:
        expected_type = "string";
        break;
      case SettingType::EnumUint:
        expected_type = "enum";
        break;
      case SettingType::FloatX10:
      case SettingType::SignedFloatX10:
      case SettingType::Float:
        expected_type = "float";
        break;
      case SettingType::FloatString:
        expected_type = "floatstring";
        break;
      case SettingType::SecondsToMs:
        expected_type = "seconds";
        break;
      case SettingType::InterfacePacked:
        expected_type = "interface";
        break;
    }
    EXPECT_STREQ(entry["type"].as<const char*>(), expected_type) << field.key;

    if (field.options_key != nullptr) {
      EXPECT_STREQ(entry["options"].as<const char*>(), field.options_key) << field.key;
    } else if (field.type == SettingType::InterfacePacked) {
      EXPECT_STREQ(entry["options"].as<const char*>(), "interfaces") << field.key;
    } else {
      EXPECT_TRUE(entry["options"].isNull()) << field.key;
    }
  }

  // The dropped optionsKeys map must not linger alongside its replacement.
  EXPECT_TRUE(doc["optionsKeys"].isNull());
}

// HOSTNAME's placeholder carries the runtime-derived default (MAC-based) so the SPA
// can hint it when the field is blank; it is not a static schema default_str.
TEST_F(SettingsApiTest, HostnamePlaceholderCarriesDefaultHostname) {
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));

  EXPECT_STREQ(doc["placeholders"]["HOSTNAME"].as<const char*>(), default_hostname().c_str());
}

// The canonical bool-wipe guard: a POST that omits a Bool row must leave the
// stored value intact — the entire reason this migration exists.
TEST_F(SettingsApiTest, MissingBoolKeyIsPreservedNotWiped) {
  BatteryEmulatorSettingsStore store;
  store.saveBool("MQTTENABLED", true);

  JsonDocument body;
  body["values"]["WEBENABLED"] = true;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_TRUE(store.getBool("MQTTENABLED", false));
  EXPECT_TRUE(store.getBool("WEBENABLED", false));
}

TEST_F(SettingsApiTest, PresentFalseBoolIsWrittenFalse) {
  BatteryEmulatorSettingsStore store;
  store.saveBool("MQTTCELLV", true);

  JsonDocument body;
  body["values"]["MQTTCELLV"] = false;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_FALSE(store.getBool("MQTTCELLV", true));
}

TEST_F(SettingsApiTest, ExtraSlotRejectsTypeBeyondItsSlotCap) {
  BatteryEmulatorSettingsStore store;
  store.saveUInt("BATTTYPE", (uint32_t)BatteryType::NissanLeaf);
  JsonDocument body;
  JsonObject entry = body["dynamic"]["batteries"].add<JsonObject>();
  entry["slot"] = 2;
  entry["type"] = (uint32_t)BatteryType::TeslaModel3Y;
  const auto r = apply_settings_body(store, body);

  EXPECT_FALSE(r.ok) << "Tesla supports two packs at most; slot 3 must be refused at save time, not left to boot as "
                        "a silent no-start that strands the BMS-reset SOC walk";
  EXPECT_FALSE(store.settingExists("BATT3TYPE"));
}

TEST_F(SettingsApiTest, EmptyPrimaryWithStoredExtraBatteryIsRejected) {
  BatteryEmulatorSettingsStore store;
  store.saveUInt("BATTTYPE", (uint32_t)BatteryType::NissanLeaf);
  store.saveUInt("BATT2TYPE", (uint32_t)BatteryType::NissanLeaf);
  JsonDocument body;
  JsonObject entry = body["dynamic"]["batteries"].add<JsonObject>();
  entry["slot"] = 0;
  entry["type"] = 0;
  const auto r = apply_settings_body(store, body);

  EXPECT_FALSE(r.ok) << "the combined view and parallel safety join from pack 0, so an empty primary with a live "
                        "extra pack must be refused even when the extra slot arrives from storage, not the POST";
  EXPECT_EQ(store.getUInt("BATTTYPE", 999), (uint32_t)BatteryType::NissanLeaf);
}

TEST_F(SettingsApiTest, ApplyAcceptsTheNvsKey) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["INVTYPE"] = 3;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_EQ(store.getUInt("INVTYPE", 0), 3u);
  EXPECT_FALSE(store.settingExists("inverter"));
}

TEST_F(SettingsApiTest, SecondsToMsTransformOnApply) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["MQTTPUBLISHMS"] = 5;  // seconds in JSON
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_EQ(store.getUInt("MQTTPUBLISHMS", 0), 5000u);
}

TEST_F(SettingsApiTest, FloatX10TransformOnApply) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["BATTPVMAX"] = 400.5;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_EQ(store.getUInt("BATTPVMAX", 0), 4005u);
}

TEST_F(SettingsApiTest, FloatStringApplyKeepsSign) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["CTOFFSET"] = "-1.5";
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_STREQ(store.getString("CTOFFSET", "").c_str(), "-1.5");
}

TEST_F(SettingsApiTest, PasswordMismatchRejectedNothingWritten) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["HTTPPASS"] = "one";
  body["values"]["HTTPPASSCONFIRM"] = "two";
  body["values"]["MQTTPORT"] = 9000;
  const auto r = apply_settings_body(store, body);

  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(store.settingExists("HTTPPASS"));
  EXPECT_FALSE(store.settingExists("MQTTPORT"));
}

TEST_F(SettingsApiTest, WebauthWithEmptyPasswordRejected) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["WEBAUTH"] = true;
  body["values"]["HTTPUSER"] = "admin";
  body["values"]["HTTPPASS"] = "";
  const auto r = apply_settings_body(store, body);

  EXPECT_FALSE(r.ok);
  EXPECT_NE(std::string(r.error.c_str()).find("password"), std::string::npos) << r.error.c_str();
}

TEST_F(SettingsApiTest, MatchingPasswordAccepted) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["HTTPPASS"] = "secret";
  body["values"]["HTTPPASSCONFIRM"] = "secret";
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_STREQ(store.getString("HTTPPASS", "").c_str(), "secret");
  EXPECT_FALSE(store.settingExists("HTTPPASSCONFIRM"));
}

// Stored secrets must never be served back to the client.
TEST_F(SettingsApiTest, PasswordFieldsRedactedInBuild) {
  {
    BatteryEmulatorSettingsStore store;
    store.saveString("HTTPPASS", "web-secret");
    store.saveString("PASSWORD", "wifi-secret");
    store.saveString("APPASSWORD", "ap-secret");
    store.saveString("MQTTPASSWORD", "mqtt-secret");
    store.saveString("MQTTSERVER", "broker.local");  // non-secret string is still emitted
  }

  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonObjectConst values = doc["values"];

  EXPECT_STREQ(values["HTTPPASS"].as<const char*>(), "");
  EXPECT_STREQ(values["PASSWORD"].as<const char*>(), "");
  EXPECT_STREQ(values["APPASSWORD"].as<const char*>(), "");
  EXPECT_STREQ(values["MQTTPASSWORD"].as<const char*>(), "");
  EXPECT_STREQ(values["MQTTSERVER"].as<const char*>(), "broker.local");
}

// A blank password on POST means "keep the stored secret", not "clear it" — for
// every secret key, and without spuriously flagging a reboot for the no-op.
TEST_F(SettingsApiTest, EmptyPasswordPreservesStored) {
  BatteryEmulatorSettingsStore store;
  store.saveString("MQTTPASSWORD", "mqtt-kept");
  store.saveString("PASSWORD", "wifi-kept");
  store.saveString("APPASSWORD", "ap-kept");

  JsonDocument body;
  body["values"]["MQTTPASSWORD"] = "";
  body["values"]["PASSWORD"] = "";
  body["values"]["APPASSWORD"] = "";
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_FALSE(r.reboot_required);
  EXPECT_STREQ(store.getString("MQTTPASSWORD", "").c_str(), "mqtt-kept");
  EXPECT_STREQ(store.getString("PASSWORD", "").c_str(), "wifi-kept");
  EXPECT_STREQ(store.getString("APPASSWORD", "").c_str(), "ap-kept");
}

// A non-empty password overwrites the stored secret (positive write path).
TEST_F(SettingsApiTest, NonEmptyPasswordOverwritesStored) {
  BatteryEmulatorSettingsStore store;
  store.saveString("MQTTPASSWORD", "old");

  JsonDocument body;
  body["values"]["MQTTPASSWORD"] = "new";
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_STREQ(store.getString("MQTTPASSWORD", "").c_str(), "new");
}

// Upstream parity: a blank confirm copies the new password, so the match passes.
// The SPA still enforces the confirm client-side; this pins the server behaviour.
TEST_F(SettingsApiTest, BlankConfirmAcceptsNewPassword) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["HTTPPASS"] = "fresh";
  body["values"]["HTTPPASSCONFIRM"] = "";
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_STREQ(store.getString("HTTPPASS", "").c_str(), "fresh");
}

// With a password already stored, a blank field must not trip the webauth guard.
TEST_F(SettingsApiTest, WebauthEmptyPasswordAcceptedWhenStored) {
  BatteryEmulatorSettingsStore store;
  store.saveString("HTTPPASS", "stored");

  JsonDocument body;
  body["values"]["WEBAUTH"] = true;
  body["values"]["HTTPUSER"] = "admin";
  body["values"]["HTTPPASS"] = "";
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_STREQ(store.getString("HTTPPASS", "").c_str(), "stored");
}

// An explicit JSON null clears a stored secret (distinct from blank = preserve) and,
// since the credential changed, flags a reboot.
TEST_F(SettingsApiTest, NullPasswordClearsStored) {
  BatteryEmulatorSettingsStore store;
  store.saveString("MQTTPASSWORD", "old-secret");

  JsonDocument body;
  ASSERT_FALSE(deserializeJson(body, R"({"values":{"MQTTPASSWORD":null}})"));
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_TRUE(r.reboot_required);
  EXPECT_STREQ(store.getString("MQTTPASSWORD", "sentinel").c_str(), "");
}

// Absent (not null) still preserves — the bool-wipe invariant must survive the clear feature.
TEST_F(SettingsApiTest, AbsentPasswordPreservedNotCleared) {
  BatteryEmulatorSettingsStore store;
  store.saveString("MQTTPASSWORD", "kept");

  JsonDocument body;
  body["values"]["WEBENABLED"] = true;  // MQTTPASSWORD absent, not null
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_STREQ(store.getString("MQTTPASSWORD", "").c_str(), "kept");
}

// Clearing the web-interface password while auth stays enabled is refused (lockout guard),
// and nothing is written.
TEST_F(SettingsApiTest, ClearHttpPassWhileWebauthRejected) {
  BatteryEmulatorSettingsStore store;
  store.saveString("HTTPPASS", "stored");

  JsonDocument body;
  ASSERT_FALSE(deserializeJson(body, R"({"values":{"WEBAUTH":true,"HTTPUSER":"admin","HTTPPASS":null}})"));
  const auto r = apply_settings_body(store, body);

  EXPECT_FALSE(r.ok);
  EXPECT_NE(std::string(r.error.c_str()).find("password"), std::string::npos) << r.error.c_str();
  EXPECT_STREQ(store.getString("HTTPPASS", "").c_str(), "stored");
}

// The lockout guard must read the *stored* webauth state: a payload that clears HTTPPASS
// while omitting WEBAUTH (already enabled in NVS) must still be refused, not fail open.
TEST_F(SettingsApiTest, ClearHttpPassWithWebauthStoredButOmittedRejected) {
  BatteryEmulatorSettingsStore store;
  store.saveBool("WEBAUTH", true);
  store.saveString("HTTPPASS", "stored");

  JsonDocument body;
  ASSERT_FALSE(deserializeJson(body, R"({"values":{"HTTPPASS":null}})"));
  const auto r = apply_settings_body(store, body);

  EXPECT_FALSE(r.ok);
  EXPECT_NE(std::string(r.error.c_str()).find("password"), std::string::npos) << r.error.c_str();
  EXPECT_STREQ(store.getString("HTTPPASS", "").c_str(), "stored");
}

// Null on a non-password key preserves it (not clear) and does not fail type validation.
TEST_F(SettingsApiTest, NonPasswordNullPreserved) {
  BatteryEmulatorSettingsStore store;
  store.saveString("MQTTSERVER", "broker.local");

  JsonDocument body;
  ASSERT_FALSE(deserializeJson(body, R"({"values":{"MQTTSERVER":null}})"));
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_STREQ(store.getString("MQTTSERVER", "").c_str(), "broker.local");
}

// Clearing the WiFi password refreshes the live global so a reconnect sees the change.
TEST_F(SettingsApiTest, ClearWifiPasswordRefreshesGlobal) {
  BatteryEmulatorSettingsStore store;
  store.saveString("PASSWORD", "wifipass");
  password = "wifipass";

  JsonDocument body;
  ASSERT_FALSE(deserializeJson(body, R"({"values":{"PASSWORD":null}})"));
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_STREQ(store.getString("PASSWORD", "sentinel").c_str(), "");
  EXPECT_TRUE(password.empty());
}

TEST_F(SettingsApiTest, StaticIpStoredAsDottedQuadString) {
  {
    BatteryEmulatorSettingsStore store;
    JsonDocument body;
    body["values"]["LOCALIP"] = "192.168.1.50";
    body["values"]["GATEWAY"] = "192.168.1.1";
    body["values"]["SUBNET"] = "255.255.255.0";
    body["values"]["DNS"] = "1.1.1.1";
    const auto r = apply_settings_body(store, body);

    EXPECT_TRUE(r.ok) << r.error.c_str();
    EXPECT_STREQ(store.getString("LOCALIP", "").c_str(), "192.168.1.50");
    EXPECT_STREQ(store.getString("GATEWAY", "").c_str(), "192.168.1.1");
    EXPECT_STREQ(store.getString("SUBNET", "").c_str(), "255.255.255.0");
    EXPECT_STREQ(store.getString("DNS", "").c_str(), "1.1.1.1");
  }

  // IP fields are not secrets: they must round-trip through GET unredacted.
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  EXPECT_STREQ(doc["values"]["LOCALIP"].as<const char*>(), "192.168.1.50");
  EXPECT_STREQ(doc["values"]["DNS"].as<const char*>(), "1.1.1.1");
}

// The wrong-type field (WEBENABLED) sits AFTER the valid sibling (MQTTPORT) in
// kSettingFields order, so a single-pass reject-in-order would have written
// MQTTPORT before hitting the bad key. Its survival pins two-pass atomicity.
TEST_F(SettingsApiTest, WrongTypeRejectedNothingWritten) {
  BatteryEmulatorSettingsStore store;
  store.saveUInt("MQTTPORT", 1234);

  JsonDocument body;
  body["values"]["MQTTPORT"] = 5678;
  body["values"]["WEBENABLED"] = "yes";
  const auto r = apply_settings_body(store, body);

  EXPECT_FALSE(r.ok);
  EXPECT_NE(std::string(r.error.c_str()).find("WEBENABLED"), std::string::npos) << r.error.c_str();
  EXPECT_EQ(store.getUInt("MQTTPORT", 0), 1234u);
}

// A fresh device's GET emits each field's default; saving that unchanged form
// back must not report a change on non-zero-default fields (spurious reboot).
TEST_F(SettingsApiTest, NoRebootWhenSavingDisplayedDefaults) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["WIFIAPENABLED"] = true;  // default true
  body["values"]["PYLONBAUD"] = 500;       // default 500
  body["values"]["MQTTPORT"] = 1883;       // default 1883
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_FALSE(r.reboot_required);
}

TEST_F(SettingsApiTest, UnknownKeyIgnored) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["NOTAREALKEY"] = 42;
  body["values"]["MQTTPORT"] = 8080;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_EQ(store.getUInt("MQTTPORT", 0), 8080u);
}

// APNAME and the custom-MQTT-topic group are dead: nothing reads them (MQTT identity and AP SSID are hostname-derived).
TEST_F(SettingsApiTest, RetiredKeysAbsentFromSchemaAndIgnoredOnApply) {
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonArrayConst schema = doc["schema"];
  ASSERT_FALSE(schema.isNull());
  ASSERT_GT(schema.size(), 0u);  // guard the absence checks below against a vacuous empty schema

  const char* retired[] = {"APNAME",     "MQTTTOPICS", "MQTTTOPIC",  "MQTTOBJIDPREFIX", "MQTTDEVICENAME",
                           "HADEVICEID", "battery",    "battery2",   "battery3",        "BATTCOMM",
                           "BATT2COMM",  "BATT3COMM",  "CNTCTRL",    "CNTCTRLDBL",      "CNTCTRLTRI",
                           "DBLBTR",     "TRIBTR"};
  for (const char* key : retired) {
    for (JsonObjectConst entry : schema) {
      EXPECT_STRNE(entry["key"].as<const char*>(), key) << key << " still present in schema";
    }
  }

  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  for (const char* key : retired) {
    body["values"][key] = "leftover";
  }
  const auto r = apply_settings_body(store, body);
  EXPECT_TRUE(r.ok) << r.error.c_str();
  for (const char* key : retired) {
    EXPECT_STREQ(store.getString(key, "").c_str(), "") << key << " persisted on apply";
  }
}

// IFSCHEMA is an internal key, not a table row, so a full-set apply never touches it.
TEST_F(SettingsApiTest, InternalIfschemaKeyUntouchedByApply) {
  BatteryEmulatorSettingsStore store;
  store.saveUInt("IFSCHEMA", 7);

  JsonDocument body;
  body["values"]["MQTTPORT"] = 1883;
  body["values"]["WEBENABLED"] = true;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_EQ(store.getUInt("IFSCHEMA", 0), 7u);
}

TEST_F(SettingsApiTest, RebootRequiredWhenBootFieldChanges) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["MQTTPORT"] = 9000;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_TRUE(r.changed);
  EXPECT_TRUE(r.reboot_required);
}

TEST_F(SettingsApiTest, NoChangeNoRebootWhenValueMatchesStored) {
  BatteryEmulatorSettingsStore store;
  store.saveUInt("MQTTPORT", 9000);

  BatteryEmulatorSettingsStore fresh;
  JsonDocument body;
  body["values"]["MQTTPORT"] = 9000;
  const auto r = apply_settings_body(fresh, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_FALSE(r.changed);
  EXPECT_FALSE(r.reboot_required);
}

// CHGTAPERFLOOR default mirrors comm_nvm's read (0), not the old form's 400.
TEST_F(SettingsApiTest, ChargeTaperDefaultsEmitted) {
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  JsonObjectConst values = doc["values"];

  EXPECT_FALSE(values["CHGTAPERSOC"].as<bool>());
  EXPECT_EQ(values["CHGTAPERSTART"].as<uint32_t>(), 95u);
  EXPECT_EQ(values["CHGTAPERFLOOR"].as<uint32_t>(), 0u);
}

TEST_F(SettingsApiTest, ChargeTaperApplyWrites) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["CHGTAPERSOC"] = true;
  body["values"]["CHGTAPERSTART"] = 90;
  body["values"]["CHGTAPERFLOOR"] = 250;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_TRUE(store.getBool("CHGTAPERSOC", false));
  EXPECT_EQ(store.getUInt("CHGTAPERSTART", 0), 90u);
  EXPECT_EQ(store.getUInt("CHGTAPERFLOOR", 0), 250u);
}

TEST_F(SettingsApiTest, FoxessDefaultsEmittedAndApplyWrites) {
  {
    BatteryEmulatorSettingsStore reader(true);
    const JsonDocument doc = parse_values(settings_json(reader));
    JsonObjectConst values = doc["values"];
    EXPECT_EQ(values["FOXESSTYPE"].as<uint32_t>(), 0u);
    EXPECT_EQ(values["FOXESSSUBTYPE"].as<uint32_t>(), 0u);
    EXPECT_EQ(values["FOXESSMODULES"].as<uint32_t>(), 0u);
  }

  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["FOXESSTYPE"] = 2;
  body["values"]["FOXESSSUBTYPE"] = 1;
  body["values"]["FOXESSMODULES"] = 4;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_EQ(store.getUInt("FOXESSTYPE", 99), 2u);
  EXPECT_EQ(store.getUInt("FOXESSSUBTYPE", 99), 1u);
  EXPECT_EQ(store.getUInt("FOXESSMODULES", 99), 4u);
}

TEST_F(SettingsApiTest, HaDiscoveryTopicDefaultAndApply) {
  {
    BatteryEmulatorSettingsStore reader(true);
    const JsonDocument doc = parse_values(settings_json(reader));
    EXPECT_STREQ(doc["values"]["HADISCTOPIC"].as<const char*>(), "homeassistant");
  }

  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["HADISCTOPIC"] = "hass";
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_STREQ(store.getString("HADISCTOPIC", "").c_str(), "hass");
}

TEST_F(SettingsApiTest, HaDiscoveryTriggersDefaultFalseAndApply) {
  {
    BatteryEmulatorSettingsStore reader(true);
    const JsonDocument doc = parse_values(settings_json(reader));
    ASSERT_FALSE(doc["values"]["HADISC"].isNull()) << "HADISC missing from the settings schema";
    ASSERT_FALSE(doc["values"]["HADISCFWU"].isNull()) << "HADISCFWU missing from the settings schema";
    EXPECT_FALSE(doc["values"]["HADISC"].as<bool>()) << "autodiscovery must not publish unless asked";
    EXPECT_FALSE(doc["values"]["HADISCFWU"].as<bool>()) << "autodiscovery must not publish unless asked";
  }

  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["HADISCFWU"] = true;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_TRUE(store.getBool("HADISCFWU", false));
  EXPECT_FALSE(store.getBool("HADISC", false)) << "arming the firmware-update trigger must not arm the next-boot one";
}

#ifndef SMALL_FLASH_DEVICE
TEST_F(SettingsApiTest, SyslogFieldsPresentWithDefaultsAndApply) {
  {
    BatteryEmulatorSettingsStore reader(true);
    const JsonDocument doc = parse_values(settings_json(reader));
    JsonObjectConst values = doc["values"];
    EXPECT_FALSE(values["SYSLOGEN"].as<bool>());
    EXPECT_STREQ(values["SYSLOGIP"].as<const char*>(), "");
    EXPECT_EQ(values["SYSLOGPORT"].as<uint32_t>(), 514u);
    EXPECT_EQ(values["SYSLOGFAC"].as<uint32_t>(), 1u);
  }

  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["SYSLOGEN"] = true;
  body["values"]["SYSLOGIP"] = "192.168.1.10";
  body["values"]["SYSLOGPORT"] = 1514;
  body["values"]["SYSLOGFAC"] = 16;
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_TRUE(store.getBool("SYSLOGEN", false));
  EXPECT_STREQ(store.getString("SYSLOGIP", "").c_str(), "192.168.1.10");
  EXPECT_EQ(store.getUInt("SYSLOGPORT", 0), 1514u);
  EXPECT_EQ(store.getUInt("SYSLOGFAC", 0), 16u);
}
#endif

TEST_F(SettingsApiTest, SchemaEmitsNumericBoundsOnlyWhereSet) {
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));

  bool saw_bounded = false, saw_unbounded = false;
  for (JsonObjectConst entry : doc["schema"].as<JsonArrayConst>()) {
    const char* key = entry["key"].as<const char*>();
    if (std::strcmp(key, "CHGTAPERSTART") == 0) {
      EXPECT_EQ(entry["min"].as<int>(), 50);
      EXPECT_EQ(entry["max"].as<int>(), 99);
      saw_bounded = true;
    }
    if (std::strcmp(key, "PYLONSEND") == 0) {  // numeric field left unbounded
      EXPECT_TRUE(entry["min"].isNull());
      EXPECT_TRUE(entry["max"].isNull());
      saw_unbounded = true;
    }
  }
  EXPECT_TRUE(saw_bounded);
  EXPECT_TRUE(saw_unbounded);
}

TEST_F(SettingsApiTest, AboveMaxRejectedAtomically) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["CHGTAPERFLOOR"] = 500;  // valid, earlier in table order
  body["values"]["MQTTPORT"] = 70000;     // max is 65535, later in table order
  const auto r = apply_settings_body(store, body);

  EXPECT_FALSE(r.ok);
  EXPECT_NE(std::string(r.error.c_str()).find("MQTTPORT"), std::string::npos) << r.error.c_str();
  EXPECT_FALSE(store.settingExists("CHGTAPERFLOOR"));
}

TEST_F(SettingsApiTest, BelowMinRejected) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["MQTTPORT"] = 0;  // min is 1
  const auto r = apply_settings_body(store, body);

  EXPECT_FALSE(r.ok);
  EXPECT_NE(std::string(r.error.c_str()).find("MQTTPORT"), std::string::npos) << r.error.c_str();
  EXPECT_FALSE(store.settingExists("MQTTPORT"));
}

TEST_F(SettingsApiTest, InclusiveBoundariesAndSecondsDomainAccepted) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["CHGTAPERSTART"] = 99;   // inclusive max
  body["values"]["MQTTPUBLISHMS"] = 300;  // seconds-domain max, stored as ms
  const auto r = apply_settings_body(store, body);

  EXPECT_TRUE(r.ok) << r.error.c_str();
  EXPECT_EQ(store.getUInt("CHGTAPERSTART", 0), 99u);
  EXPECT_EQ(store.getUInt("MQTTPUBLISHMS", 0), 300000u);
}

TEST_F(SettingsApiTest, SecondsToMsAboveSecondsBoundRejected) {
  BatteryEmulatorSettingsStore store;
  JsonDocument body;
  body["values"]["MQTTPUBLISHMS"] = 301;  // max 300 s
  const auto r = apply_settings_body(store, body);

  EXPECT_FALSE(r.ok);
  EXPECT_NE(std::string(r.error.c_str()).find("MQTTPUBLISHMS"), std::string::npos) << r.error.c_str();
  EXPECT_FALSE(store.settingExists("MQTTPUBLISHMS"));
}

namespace {
// Exercises the generic gpio_options() emission the device cannot show while no
// board declares a catalog: labels chosen so alphabetical order ("Pin 33" <
// "Pin 5") differs from value order, plus a hidden (null-label) choice.
constexpr GpioOptionChoice kTestGpioChoices[] = {
    {0, "Pin 5", nullptr, 0, 0},
    {1, "Pin 33", nullptr, 0, 0},
    {2, nullptr, nullptr, 0, 0},
};
constexpr GpioOptionGroup kTestGpioGroups[] = {
    {"GPIOOPT9", "Test option", kTestGpioChoices, 3, 0},
};
constexpr GpioOptionCatalog kTestGpioCatalog = make_gpio_option_catalog(kTestGpioGroups);

class GpioOptionHal : public LilyGoHal {
 public:
  GpioOptionCatalog gpio_options() override { return kTestGpioCatalog; }
};
}  // namespace

TEST_F(SettingsApiTest, EmitsGpioOptionGroupOrderedByLabel) {
  GpioOptionHal hal;
  Esp32Hal* saved = esp32hal;
  esp32hal = &hal;

  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));

  EXPECT_EQ(doc["values"]["GPIOOPT9"].as<uint32_t>(), 0u);

  JsonArrayConst opts = doc["options"]["GPIOOPT9"];
  ASSERT_EQ(opts.size(), 2u);
  EXPECT_STREQ(opts[0]["n"].as<const char*>(), "Pin 33");
  EXPECT_EQ(opts[0]["v"].as<int>(), 1);
  EXPECT_STREQ(opts[1]["n"].as<const char*>(), "Pin 5");
  EXPECT_EQ(opts[1]["v"].as<int>(), 0);

  bool found = false;
  for (JsonObjectConst row : doc["schema"].as<JsonArrayConst>()) {
    if (std::strcmp(row["key"].as<const char*>(), "GPIOOPT9") == 0) {
      found = true;
      EXPECT_STREQ(row["label"].as<const char*>(), "Test option");
      EXPECT_STREQ(row["options"].as<const char*>(), "GPIOOPT9");
      EXPECT_STREQ(row["type"].as<const char*>(), "enum");
      EXPECT_STREQ(row["category"].as<const char*>(), "hardware");
    }
  }
  EXPECT_TRUE(found);

  esp32hal = saved;
}

TEST_F(SettingsApiTest, GpioOptionValueReportsClampedChoice) {
  GpioOptionHal hal;
  Esp32Hal* saved = esp32hal;
  esp32hal = &hal;

  {
    BatteryEmulatorSettingsStore store;
    store.saveUInt("GPIOOPT9", 7);  // no such choice -> reported as default 0
  }
  BatteryEmulatorSettingsStore reader(true);
  const JsonDocument doc = parse_values(settings_json(reader));
  EXPECT_EQ(doc["values"]["GPIOOPT9"].as<uint32_t>(), 0u);

  esp32hal = saved;
}

TEST_F(SettingsApiTest, GpioOptionApplyClampsUnknownValueToDefault) {
  GpioOptionHal hal;
  Esp32Hal* saved = esp32hal;
  esp32hal = &hal;

  {
    BatteryEmulatorSettingsStore store;
    JsonDocument body;
    body["values"]["GPIOOPT9"] = 7;  // no such choice -> clamp to default 0
    const auto r = apply_settings_body(store, body);
    EXPECT_TRUE(r.ok) << r.error.c_str();
    EXPECT_EQ(store.getUInt("GPIOOPT9", 99), 0u);
  }
  {
    BatteryEmulatorSettingsStore store;
    JsonDocument body;
    body["values"]["GPIOOPT9"] = 1;  // valid choice -> stored verbatim
    const auto r = apply_settings_body(store, body);
    EXPECT_TRUE(r.ok) << r.error.c_str();
    EXPECT_EQ(store.getUInt("GPIOOPT9", 99), 1u);
  }
  {
    // 257 shares its low byte with choice 1; it must clamp, not alias onto it.
    BatteryEmulatorSettingsStore store;
    JsonDocument body;
    body["values"]["GPIOOPT9"] = 257;
    const auto r = apply_settings_body(store, body);
    EXPECT_TRUE(r.ok) << r.error.c_str();
    EXPECT_EQ(store.getUInt("GPIOOPT9", 99), 0u);
  }

  esp32hal = saved;
}

static JsonDocument balancing_body(const char* json) {
  JsonDocument doc;
  EXPECT_FALSE(deserializeJson(doc, json));
  return doc;
}

static const char* battery_slot_of(const JsonDocument& body, uint8_t& slot) {
  JsonDocumentReader reader(body.as<JsonVariantConst>());
  return validate_battery_slot(reader, slot);
}

static const char* validate_balancing_update(battery_chemistry_enum chemistry, const JsonDocument& doc) {
  JsonDocumentReader reader(doc.as<JsonVariantConst>());
  for (const char* key : {"max_cell_mv", "max_dev_mv", "float_power_w", "max_pack_v", "max_time_min"}) {
    if (const char* error = validate_balancing_field(chemistry, key, reader.value(key))) {
      return error;
    }
  }
  return nullptr;
}

namespace {
class BatterySlotResolutionTest : public testing::Test {
 protected:
  void SetUp() override {
    for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
      saved_batteries_[slot] = batteries[slot];
    }
    static int marker;
    batteries[0] = reinterpret_cast<Battery*>(&marker);
    batteries[1] = nullptr;
    batteries[2] = nullptr;
  }

  void TearDown() override {
    for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
      batteries[slot] = saved_batteries_[slot];
    }
  }

  Battery* saved_batteries_[kMaxBatterySlots] = {};
};
}  // namespace

TEST_F(BatterySlotResolutionTest, AbsentFieldMeansPrimary) {
  uint8_t slot = 99;
  EXPECT_EQ(battery_slot_of(balancing_body("{}"), slot), nullptr);
  EXPECT_EQ(slot, 0);
}

TEST_F(BatterySlotResolutionTest, RejectsNonIntegerField) {
  uint8_t slot = 0;
  EXPECT_STREQ(battery_slot_of(balancing_body(R"({"battery":"0"})"), slot), "Bad Request");
  EXPECT_STREQ(battery_slot_of(balancing_body(R"({"battery":1.5})"), slot), "Bad Request");
}

TEST_F(BatterySlotResolutionTest, RejectsOutOfRangeSlots) {
  uint8_t slot = 0;
  EXPECT_STREQ(battery_slot_of(balancing_body(R"({"battery":-1})"), slot), "Invalid battery");
  EXPECT_STREQ(battery_slot_of(balancing_body(R"({"battery":3})"), slot), "Invalid battery");
}

TEST_F(BatterySlotResolutionTest, RejectsUnconfiguredSecondarySlot) {
  uint8_t slot = 0;
  EXPECT_STREQ(battery_slot_of(balancing_body(R"({"battery":1})"), slot), "Invalid battery");
}

// Deliberate: settings can be staged before a battery type is selected.
TEST_F(BatterySlotResolutionTest, PrimaryAcceptedWithNoDriverConfigured) {
  batteries[0] = nullptr;
  uint8_t slot = 99;
  EXPECT_EQ(battery_slot_of(balancing_body(R"({"battery":0})"), slot), nullptr);
  EXPECT_EQ(slot, 0);
}

// The editcards emitter loops this predicate, so its rules must track the
// resolver's exactly.
TEST_F(BatterySlotResolutionTest, AddressablePredicateMatchesResolverRules) {
  EXPECT_TRUE(battery_slot_addressable(0));
  EXPECT_FALSE(battery_slot_addressable(1));
  batteries[1] = batteries[0];
  EXPECT_TRUE(battery_slot_addressable(1));
  EXPECT_FALSE(battery_slot_addressable(kMaxBatterySlots));

  batteries[0] = nullptr;
  EXPECT_TRUE(battery_slot_addressable(0));
}

TEST_F(BatterySlotResolutionTest, ResolvesConfiguredSlots) {
  uint8_t slot = 99;
  EXPECT_EQ(battery_slot_of(balancing_body(R"({"battery":0})"), slot), nullptr);
  EXPECT_EQ(slot, 0);

  batteries[1] = batteries[0];
  EXPECT_EQ(battery_slot_of(balancing_body(R"({"battery":1})"), slot), nullptr);
  EXPECT_EQ(slot, 1);
}

TEST(BalancingValidationTest, AcceptsEmptyBody) {
  EXPECT_EQ(validate_balancing_update(battery_chemistry_enum::Autodetect, balancing_body("{}")), nullptr);
}

TEST(BalancingValidationTest, AcceptsAllFieldsAtLfpCeilings) {
  const auto doc = balancing_body(
      R"({"max_time_min":300,"max_cell_mv":3650,"max_dev_mv":400,"max_pack_v":394.0,"float_power_w":2000})");
  EXPECT_EQ(validate_balancing_update(battery_chemistry_enum::LFP, doc), nullptr);
}

TEST(BalancingValidationTest, AcceptsAllFieldsAtFloors) {
  const auto doc = balancing_body(
      R"({"max_time_min":1,"max_cell_mv":3400,"max_dev_mv":300,"max_pack_v":380.0,"float_power_w":100})");
  EXPECT_EQ(validate_balancing_update(battery_chemistry_enum::LFP, doc), nullptr);
}

TEST(BalancingValidationTest, CellCeilingFollowsChemistry) {
  EXPECT_STREQ(validate_balancing_update(battery_chemistry_enum::LFP, balancing_body(R"({"max_cell_mv":3651})")),
               "Cell voltage out of range");
  EXPECT_EQ(validate_balancing_update(battery_chemistry_enum::NMC, balancing_body(R"({"max_cell_mv":3651})")),
            nullptr);
  EXPECT_EQ(validate_balancing_update(battery_chemistry_enum::NCA, balancing_body(R"({"max_cell_mv":4250})")),
            nullptr);
  EXPECT_STREQ(validate_balancing_update(battery_chemistry_enum::NMC, balancing_body(R"({"max_cell_mv":4251})")),
               "Cell voltage out of range");
}

TEST(BalancingValidationTest, DeviationCeilingFollowsChemistry) {
  EXPECT_STREQ(validate_balancing_update(battery_chemistry_enum::LFP, balancing_body(R"({"max_dev_mv":401})")),
               "Cell deviation out of range");
  EXPECT_EQ(validate_balancing_update(battery_chemistry_enum::NMC, balancing_body(R"({"max_dev_mv":500})")),
            nullptr);
  EXPECT_STREQ(validate_balancing_update(battery_chemistry_enum::NMC, balancing_body(R"({"max_dev_mv":501})")),
               "Cell deviation out of range");
}

TEST(BalancingValidationTest, PackCeilingIsChemistryMaxPlusHeadroom) {
  EXPECT_EQ(validate_balancing_update(battery_chemistry_enum::LFP, balancing_body(R"({"max_pack_v":394.0})")),
            nullptr);
  EXPECT_STREQ(validate_balancing_update(battery_chemistry_enum::LFP, balancing_body(R"({"max_pack_v":394.1})")),
               "Pack voltage out of range");
  EXPECT_EQ(validate_balancing_update(battery_chemistry_enum::NMC, balancing_body(R"({"max_pack_v":409.0})")),
            nullptr);
  EXPECT_STREQ(validate_balancing_update(battery_chemistry_enum::NMC, balancing_body(R"({"max_pack_v":409.1})")),
               "Pack voltage out of range");
}

TEST(BalancingValidationTest, RejectsBelowFloors) {
  EXPECT_STREQ(validate_balancing_update(battery_chemistry_enum::NMC, balancing_body(R"({"max_cell_mv":3399})")),
               "Cell voltage out of range");
  EXPECT_STREQ(validate_balancing_update(battery_chemistry_enum::NMC, balancing_body(R"({"max_dev_mv":299})")),
               "Cell deviation out of range");
  EXPECT_STREQ(validate_balancing_update(battery_chemistry_enum::NMC, balancing_body(R"({"max_pack_v":379.9})")),
               "Pack voltage out of range");
  EXPECT_STREQ(validate_balancing_update(battery_chemistry_enum::NMC, balancing_body(R"({"float_power_w":99})")),
               "Float power out of range");
}

TEST(BalancingValidationTest, FloatPowerRangeIsChemistryIndependent) {
  EXPECT_EQ(validate_balancing_update(battery_chemistry_enum::LFP, balancing_body(R"({"float_power_w":2000})")),
            nullptr);
  EXPECT_STREQ(validate_balancing_update(battery_chemistry_enum::NMC, balancing_body(R"({"float_power_w":2001})")),
               "Float power out of range");
}

// Mirrors the driver: autodetect can only ever conclude LFP, so anything that
// is not LFP runs (and validates) as NCM.
TEST(BalancingValidationTest, NonLfpChemistryValidatesAsNcm) {
  EXPECT_EQ(
      validate_balancing_update(battery_chemistry_enum::Autodetect, balancing_body(R"({"max_cell_mv":4250})")),
      nullptr);
  EXPECT_STREQ(
      validate_balancing_update(battery_chemistry_enum::Autodetect, balancing_body(R"({"max_cell_mv":4251})")),
      "Cell voltage out of range");
  EXPECT_EQ(validate_balancing_update(battery_chemistry_enum::ZEBRA, balancing_body(R"({"max_cell_mv":4250})")),
            nullptr);
}

TEST(BalancingValidationTest, TimeIsUnbounded) {
  EXPECT_EQ(validate_balancing_update(battery_chemistry_enum::Autodetect, balancing_body(R"({"max_time_min":301})")),
            nullptr);
  EXPECT_EQ(validate_balancing_update(battery_chemistry_enum::LFP, balancing_body(R"({"max_time_min":0.5})")),
            nullptr);
}

TEST(BalancingValidationTest, MalformedValuesAreBadRequest) {
  for (const char* body : {R"({"max_cell_mv":"3650"})", R"({"max_cell_mv":3650.5})", R"({"float_power_w":-100})",
                           R"({"max_pack_v":"394"})", R"({"max_time_min":true})", R"({"max_dev_mv":70000})"}) {
    EXPECT_STREQ(validate_balancing_update(battery_chemistry_enum::LFP, balancing_body(body)), "Bad Request") << body;
  }
}

struct BalancingSlotScope {
  DATALAYER_BATTERY_SETTINGS_TYPE saved = datalayer.battery_slot(0).settings;
  BalancingSlotScope() { datalayer.battery_slot(0).settings = DATALAYER_BATTERY_SETTINGS_TYPE{}; }
  ~BalancingSlotScope() { datalayer.battery_slot(0).settings = saved; }
};

static SettingsApplyResult apply_balancing(BatteryEmulatorSettingsStore& store, const char* entry_json) {
  const std::string body = std::string(R"({"dynamic":{"balancing":[)") + entry_json + "]}}";
  JsonDocument doc;
  EXPECT_FALSE(deserializeJson(doc, body));
  return apply_settings_body(store, doc);
}

TEST_F(SettingsApiTest, BalancingWritesEveryPresentFieldWithUnitConversion) {
  HalScope hal;
  BalancingSlotScope slot;
  BatteryEmulatorSettingsStore store;
  const SettingsApplyResult result = apply_balancing(
      store,
      R"({"slot":0,"max_time_min":90,"max_cell_mv":3650,"max_dev_mv":400,"max_pack_v":394.0,"float_power_w":1500})");
  ASSERT_TRUE(result.ok) << result.error.c_str();
  const auto& settings = datalayer.battery_slot(0).settings;
  EXPECT_EQ(settings.balancing_max_time_ms, 5400000u);
  EXPECT_EQ(settings.balancing_max_cell_voltage_mV, 3650);
  EXPECT_EQ(settings.balancing_max_deviation_cell_voltage_mV, 400);
  EXPECT_EQ(settings.balancing_max_pack_voltage_dV, 3940);
  EXPECT_EQ(settings.balancing_float_power_W, 1500);
}

TEST_F(SettingsApiTest, BalancingPreservesAbsentFields) {
  HalScope hal;
  BalancingSlotScope slot;
  BatteryEmulatorSettingsStore store;
  auto& settings = datalayer.battery_slot(0).settings;
  settings.balancing_max_time_ms = 60000;
  settings.balancing_max_cell_voltage_mV = 3600;
  settings.balancing_max_pack_voltage_dV = 3900;
  const SettingsApplyResult result = apply_balancing(store, R"({"slot":0,"max_dev_mv":350})");
  ASSERT_TRUE(result.ok) << result.error.c_str();
  EXPECT_EQ(settings.balancing_max_time_ms, 60000u);
  EXPECT_EQ(settings.balancing_max_cell_voltage_mV, 3600);
  EXPECT_EQ(settings.balancing_max_deviation_cell_voltage_mV, 350);
  EXPECT_EQ(settings.balancing_max_pack_voltage_dV, 3900);
}

TEST_F(SettingsApiTest, BalancingIsEmittedPerSlotAndRoundTrips) {
  HalScope hal;
  BalancingSlotScope slot;
  BatteryEmulatorSettingsStore store;
  const SettingsApplyResult result = apply_balancing(
      store,
      R"({"slot":0,"max_time_min":90,"max_cell_mv":3650,"max_dev_mv":400,"max_pack_v":394.0,"float_power_w":1500})");
  ASSERT_TRUE(result.ok) << result.error.c_str();

  const JsonDocument doc = parse_values(settings_json(store));
  JsonObjectConst entry = doc["dynamic"]["balancing"][0];
  EXPECT_EQ(entry["slot"].as<uint8_t>(), 0);
  EXPECT_FLOAT_EQ(entry["max_time_min"].as<float>(), 90.0f);
  EXPECT_EQ(entry["max_cell_mv"].as<uint16_t>(), 3650);
  EXPECT_EQ(entry["max_dev_mv"].as<uint16_t>(), 400);
  EXPECT_FLOAT_EQ(entry["max_pack_v"].as<float>(), 394.0f);
  EXPECT_EQ(entry["float_power_w"].as<uint16_t>(), 1500);
}

TEST_F(SettingsApiTest, BalancingRejectsAnOutOfRangeValueForTheSlotChemistry) {
  HalScope hal;
  BalancingSlotScope slot;
  BatteryEmulatorSettingsStore store;
  const SettingsApplyResult result = apply_balancing(store, R"({"slot":0,"max_dev_mv":501})");
  EXPECT_FALSE(result.ok);
  EXPECT_STREQ(result.error.c_str(), "Cell deviation out of range");
}

TEST_F(SettingsApiTest, RejectsValueAbsentFromItsOptionList) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["values"]["BATTCHEM"] = 9999;
  const SettingsApplyResult result = apply_settings_body(store, doc);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(std::string(result.error.c_str()).find("BATTCHEM"), std::string::npos);
}

// Regression: the membership check used to reject every value for a pick-list the
// firmware does not publish, which made saving any settings impossible.
TEST_F(SettingsApiTest, AcceptsAValueFromAClientOwnedOptionList) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["values"]["GTWCOUNTRY"] = 16725;
  const SettingsApplyResult result = apply_settings_body(store, doc);
  EXPECT_TRUE(result.ok) << result.error.c_str();
}

// The membership check silently stands down for a list the firmware does not
// publish, so a mistyped options_key would disable validation unnoticed. Pin the
// set that is deliberately client-owned; anything else must be published here.
TEST_F(SettingsApiTest, EveryOptionsKeyIsPublishedOrDeliberatelyClientOwned) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  const JsonDocument doc = parse_values(settings_json(store));
  JsonObjectConst options = doc["options"];
  const std::set<std::string> client_owned = {"country",    "mapregion",  "chassis", "pack",
                                              "pylonbrand", "contactor",  "sungrow"};
  for (size_t i = 0; i < setting_count(); i++) {
    const SettingField& field = *setting_at(i).field;
    if (field.options_key == nullptr) {
      continue;
    }
    const bool published = !options[field.options_key].isNull();
    EXPECT_TRUE(published || client_owned.count(field.options_key) == 1)
        << field.key << " names option list '" << field.options_key
        << "' which this firmware never publishes and the client does not own";
  }
}

TEST_F(SettingsApiTest, AcceptsValuePresentInItsOptionList) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["values"]["BATTCHEM"] = 1;
  const SettingsApplyResult result = apply_settings_body(store, doc);
  EXPECT_TRUE(result.ok) << result.error.c_str();
}

TEST_F(SettingsApiTest, LeavesFieldsWithoutOptionsUnchecked) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["values"]["SOFAR_ID"] = 5;
  const SettingsApplyResult result = apply_settings_body(store, doc);
  EXPECT_TRUE(result.ok) << result.error.c_str();
}

TEST_F(SettingsApiTest, PublishesShellOptionsForWebUi) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  const JsonDocument doc = parse_values(settings_json(store));
  JsonArrayConst shells = doc["options"]["webui"].as<JsonArrayConst>();
  ASSERT_EQ(shells.size(), 2u);
  EXPECT_STREQ(shells[0]["v"] | "", "legacy");
  EXPECT_STREQ(shells[1]["v"] | "", "modern");
  EXPECT_FALSE(shells[0]["n"].is<const char*>()) << "display text belongs to the client, not the payload";
  EXPECT_FALSE(shells[1]["n"].is<const char*>()) << "display text belongs to the client, not the payload";
}

TEST_F(SettingsApiTest, WebUiDefaultsToLegacy) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  const JsonDocument doc = parse_values(settings_json(store));
  EXPECT_STREQ(doc["values"]["WEBUI"] | "", "legacy");
}

TEST_F(SettingsApiTest, WebUiSchemaRowCarriesOptionsAndCategory) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  const JsonDocument doc = parse_values(settings_json(store));
  bool seen = false;
  for (JsonObjectConst entry : doc["schema"].as<JsonArrayConst>()) {
    if (strcmp(entry["key"] | "", "WEBUI") != 0) {
      continue;
    }
    seen = true;
    EXPECT_STREQ(entry["category"] | "", "interface");
    EXPECT_STREQ(entry["type"] | "", "string");
    EXPECT_STREQ(entry["options"] | "", "webui");
  }
  EXPECT_TRUE(seen);
}

TEST_F(SettingsApiTest, RejectsUnknownShellName) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["values"]["WEBUI"] = "nosuchskin";
  const SettingsApplyResult result = apply_settings_body(store, doc);
  EXPECT_FALSE(result.ok);
}

TEST_F(SettingsApiTest, AcceptsShippedShellName) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["values"]["WEBUI"] = "modern";
  const SettingsApplyResult result = apply_settings_body(store, doc);
  EXPECT_TRUE(result.ok) << result.error.c_str();
  EXPECT_EQ(store.getString("WEBUI", ""), String("modern"));
}

TEST_F(SettingsApiTest, LiveFieldDoesNotRequireReboot) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["values"]["WEBUI"] = "modern";
  const SettingsApplyResult result = apply_settings_body(store, doc);
  ASSERT_TRUE(result.ok) << result.error.c_str();
  EXPECT_TRUE(result.changed);
  EXPECT_FALSE(result.reboot_required);
}

TEST_F(SettingsApiTest, BootFieldStillRequiresReboot) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["values"]["SOFAR_ID"] = 7;
  const SettingsApplyResult result = apply_settings_body(store, doc);
  ASSERT_TRUE(result.ok) << result.error.c_str();
  EXPECT_TRUE(result.reboot_required);
}







TEST_F(SettingsApiTest, RejectionCarriesKeyForPasswordMismatch) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["values"]["HTTPPASS"] = "correct-horse";
  doc["values"]["HTTPPASSCONFIRM"] = "battery-staple";
  const SettingsApplyResult result = apply_settings_body(store, doc);
  ASSERT_FALSE(result.ok);
  EXPECT_STREQ(result.error_key, "error.webauth_password_mismatch");
  EXPECT_STREQ(result.error_arg.c_str(), "");
}

TEST_F(SettingsApiTest, RejectionCarriesKeyAndArgForWrongType) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["values"]["SSID"] = 42;
  const SettingsApplyResult result = apply_settings_body(store, doc);
  ASSERT_FALSE(result.ok);
  EXPECT_STREQ(result.error_key, "error.setting_invalid_type");
  EXPECT_STREQ(result.error_arg.c_str(), "SSID");
}

TEST_F(SettingsApiTest, RejectionCarriesKeyAndArgForOutOfRange) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["values"]["WIFICHANNEL"] = 99;
  const SettingsApplyResult result = apply_settings_body(store, doc);
  ASSERT_FALSE(result.ok);
  EXPECT_STREQ(result.error_key, "error.setting_out_of_range");
  EXPECT_STREQ(result.error_arg.c_str(), "WIFICHANNEL");
}

TEST_F(SettingsApiTest, RejectionCarriesKeyForUnknownBatterySlot) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["dynamic"]["batteries"][0]["slot"] = kMaxBatterySlots;
  const SettingsApplyResult result = apply_settings_body(store, doc);
  ASSERT_FALSE(result.ok);
  EXPECT_STREQ(result.error_key, "error.battery_slot_unknown");
}

TEST_F(SettingsApiTest, AcceptedApplyCarriesNoErrorKey) {
  HalScope hal;
  BatteryEmulatorSettingsStore store;
  JsonDocument doc;
  doc["values"]["SSID"] = "bench";
  const SettingsApplyResult result = apply_settings_body(store, doc);
  ASSERT_TRUE(result.ok) << result.error.c_str();
  EXPECT_EQ(result.error_key, nullptr);
}
