#include <gtest/gtest.h>

#include "../Software/src/charger/CHARGERS.h"
#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_lilygo.h"

namespace {

struct NamedType {
  ChargerType id;
  const char* name;
};

// Golden display names; user-visible, must stay byte-identical.
const NamedType kGolden[] = {
    {ChargerType::NissanLeaf, "Nissan LEAF 2013-2024 PDM charger"},
    {ChargerType::ChevyVolt,  "Chevy Volt Gen1 Charger"},
};

class ChargerRegistryTest : public testing::Test {
 protected:
  void SetUp() override {
    init_hal();
    saved_charger_ = charger;
    saved_type_ = user_selected_charger_type;
    charger = nullptr;  // isolate each test
  }
  void TearDown() override {
    if (charger) {
      delete charger;
    }
    charger = saved_charger_;
    user_selected_charger_type = saved_type_;
  }

 private:
  CanCharger* saved_charger_;
  ChargerType saved_type_;
};

TEST_F(ChargerRegistryTest, NoneNamesNone) {
  EXPECT_STREQ(name_for_charger_type(ChargerType::None), "None");
}

TEST_F(ChargerRegistryTest, EveryRealTypeNameIsGolden) {
  for (const auto& g : kGolden) {
    EXPECT_STREQ(name_for_charger_type(g.id), g.name) << "id " << static_cast<int>(g.id);
  }
}

TEST_F(ChargerRegistryTest, SelectingNoneConstructsNothing) {
  user_selected_charger_type = ChargerType::None;
  setup_charger();
  EXPECT_EQ(charger, nullptr);
}

TEST_F(ChargerRegistryTest, SelectingRealTypeConstructsAndNames) {
  user_selected_charger_type = ChargerType::NissanLeaf;
  setup_charger();
  ASSERT_NE(charger, nullptr);
  EXPECT_STREQ(charger->name(), name_for_charger_type(ChargerType::NissanLeaf));
}

TEST_F(ChargerRegistryTest, HighestHasNoName) {
  // Highest is a count sentinel; callers iterate ids below it, so it has no name.
  EXPECT_EQ(name_for_charger_type(ChargerType::Highest), nullptr);
}

}  // namespace
