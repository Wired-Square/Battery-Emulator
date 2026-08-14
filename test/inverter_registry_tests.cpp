#include <gtest/gtest.h>

#include "../Software/src/inverter/INVERTERS.h"
#include "../Software/src/inverter/InverterProtocol.h"
#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_lilygo.h"

namespace {

struct NamedType {
  InverterProtocolType id;
  const char* name;
};

// Golden display names; these are user-visible and must stay byte-identical.
const NamedType kGolden[] = {
    {InverterProtocolType::AforeCan,    "Afore battery over CAN"},
    {InverterProtocolType::BydCan,      "BYD Battery-Box Premium HVS over CAN Bus"},
    {InverterProtocolType::BydModbus,   "BYD 11kWh HVM battery over Modbus RTU"},
    {InverterProtocolType::FerroampCan, "Ferroamp Pylon battery over CAN bus"},
    {InverterProtocolType::Foxess,      "FoxESS compatible HV2600/ECS4100 battery"},
    {InverterProtocolType::GrowattHv,   "Growatt High Voltage protocol via CAN"},
    {InverterProtocolType::GrowattLv,   "Growatt Low Voltage (48V) protocol via CAN"},
    {InverterProtocolType::GrowattWit,  "Growatt WIT compatible battery via CAN"},
    {InverterProtocolType::Kostal,      "BYD battery via Kostal RS485"},
    {InverterProtocolType::Pylon,       "Pylontech HV battery over CAN bus"},
    {InverterProtocolType::PylonLv,     "Pylontech LV battery over CAN bus"},
    {InverterProtocolType::Schneider,   "Schneider V2 SE BMS CAN"},
    {InverterProtocolType::SmaBydH,     "SMA compatible BYD Battery-Box H"},
    {InverterProtocolType::SmaLv,       "SMA Low Voltage (48V) protocol via CAN"},
    {InverterProtocolType::SmaBydHvs,   "SMA compatible BYD Battery-Box HVS"},
    {InverterProtocolType::Sofar,       "Sofar BMS (Extended) via CAN, Battery ID"},
    {InverterProtocolType::Solax,       "SolaX Triple Power LFP over CAN bus"},
    {InverterProtocolType::Solxpow,     "Solxpow compatible battery"},
    {InverterProtocolType::SolArkLv,    "Sol-Ark LV protocol over CAN bus"},
    {InverterProtocolType::Sungrow,     "Sungrow SBRXXX emulation over CAN bus"},
    {InverterProtocolType::VCU,         "VCU mode: Nissan LEAF battery"},
    {InverterProtocolType::PylonLV485,  "Pylon low voltage via RS485"},
    {InverterProtocolType::SmaSBSByd,   "SMA SBS compatible BYD Battery-Box HVS"},
};

// id 13 has no enum value: a frozen gap in InverterProtocolType.
constexpr int kGapId = 13;

class InverterRegistryTest : public testing::Test {
 protected:
  void SetUp() override {
    init_hal();
    saved_inverter_ = inverter;
    saved_type_ = user_selected_inverter_protocol;
    inverter = nullptr;  // setup_inverter early-returns when non-null; isolate each test
  }
  void TearDown() override {
    if (inverter) {
      delete inverter;
    }
    inverter = saved_inverter_;
    user_selected_inverter_protocol = saved_type_;
  }

 private:
  InverterProtocol* saved_inverter_;
  InverterProtocolType saved_type_;
};

TEST_F(InverterRegistryTest, NoneNamesNone) {
  EXPECT_STREQ(name_for_inverter_type(InverterProtocolType::None), "None");
}

TEST_F(InverterRegistryTest, GapIdHasNoName) {
  EXPECT_EQ(name_for_inverter_type(static_cast<InverterProtocolType>(kGapId)), nullptr);
}

TEST_F(InverterRegistryTest, EveryRealTypeNameIsGolden) {
  for (const auto& g : kGolden) {
    EXPECT_STREQ(name_for_inverter_type(g.id), g.name) << "id " << static_cast<int>(g.id);
  }
}

TEST_F(InverterRegistryTest, SelectingNoneConstructsNothing) {
  user_selected_inverter_protocol = InverterProtocolType::None;
  EXPECT_TRUE(setup_inverter());
  EXPECT_EQ(inverter, nullptr);
}

TEST_F(InverterRegistryTest, SelectingGapConstructsNothing) {
  user_selected_inverter_protocol = static_cast<InverterProtocolType>(kGapId);
  EXPECT_FALSE(setup_inverter());
  EXPECT_EQ(inverter, nullptr);
}

TEST_F(InverterRegistryTest, SelectingRealTypeConstructsAndNames) {
  user_selected_inverter_protocol = InverterProtocolType::Pylon;
  setup_inverter();
  ASSERT_NE(inverter, nullptr);
  EXPECT_STREQ(inverter->name(), name_for_inverter_type(InverterProtocolType::Pylon));
}

TEST_F(InverterRegistryTest, AlreadySetupIsIdempotent) {
  user_selected_inverter_protocol = InverterProtocolType::Pylon;
  setup_inverter();
  InverterProtocol* first = inverter;
  ASSERT_NE(first, nullptr);
  EXPECT_TRUE(setup_inverter());
  EXPECT_EQ(inverter, first);
}

TEST_F(InverterRegistryTest, HighestHasNoName) {
  // Highest is a count sentinel; callers iterate ids below it, so it has no name.
  EXPECT_EQ(name_for_inverter_type(InverterProtocolType::Highest), nullptr);
}

}  // namespace
