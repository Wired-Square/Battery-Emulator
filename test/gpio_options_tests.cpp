#include <gtest/gtest.h>

#include "../Software/src/devboard/hal/gpio_options.h"

namespace {

// Shape mirrors the real 2CAN GPIOOPT1 group: every choice defines the same
// five roles, non-default modes also relocate WUP, Display disabled unless the
// display choice is selected.
constexpr PinAssignment kWupPins[] = {
    {GpioPinRole::Wup1, 1},
    {GpioPinRole::Wup2, 2},
    {GpioPinRole::EquipmentStop, 36},
    {GpioPinRole::DisplaySda, kGpioPinNotConnected},
    {GpioPinRole::DisplayScl, kGpioPinNotConnected},
};
constexpr PinAssignment kDisplayPins[] = {
    {GpioPinRole::Wup1, 18},
    {GpioPinRole::Wup2, 14},
    {GpioPinRole::EquipmentStop, 36},
    {GpioPinRole::DisplaySda, 1},
    {GpioPinRole::DisplayScl, 2},
};
constexpr PinAssignment kEstopPins[] = {
    {GpioPinRole::Wup1, 18},
    {GpioPinRole::Wup2, 14},
    {GpioPinRole::EquipmentStop, 1},
    {GpioPinRole::DisplaySda, kGpioPinNotConnected},
    {GpioPinRole::DisplayScl, kGpioPinNotConnected},
};
constexpr GpioOptionChoice kChoices[] = {
    {0, "WUP1 / WUP2", kWupPins, 5, 0},
    {1, "I2C Display (SSD1306)", kDisplayPins, 5, 0},
    {2, "E-Stop / BMS Power", kEstopPins, 5, 1},
};
constexpr GpioOptionGroup kGroups[] = {
    {"GPIOOPT1", "BMS Power pin", kChoices, 3, 0},
};
constexpr GpioOptionCatalog kCatalog = make_gpio_option_catalog(kGroups);
constexpr GpioOptionCatalog kEmptyCatalog = {nullptr, 0};

constexpr int16_t kFallback = 99;

// The validators are meant to be static_asserted in each board header; proving
// they are constant-expressions here is part of the contract, not decoration.
static_assert(gpio_choices_valid(kGroups[0]));
static_assert(gpio_choices_cover_same_roles(kGroups[0]));
static_assert(gpio_groups_roles_disjoint(kCatalog));

TEST(GpioOptions, MakeCatalogCountsGroups) {
  EXPECT_EQ(kCatalog.group_count, 1u);
  EXPECT_EQ(kCatalog.groups, kGroups);
}

TEST(GpioOptions, FindGroupByKey) {
  EXPECT_EQ(find_gpio_option_group(kCatalog, "GPIOOPT1"), &kGroups[0]);
  EXPECT_EQ(find_gpio_option_group(kCatalog, "NOPE"), nullptr);
  EXPECT_EQ(find_gpio_option_group(kEmptyCatalog, "GPIOOPT1"), nullptr);
}

TEST(GpioOptions, FindChoiceByValue) {
  EXPECT_EQ(find_gpio_option_choice(kGroups[0], 1), &kChoices[1]);
  EXPECT_EQ(find_gpio_option_choice(kGroups[0], 9), nullptr);
}

// A stored or posted value wider than a choice ordinal must miss, not alias onto
// the choice its low byte happens to name.
TEST(GpioOptions, FindChoiceRejectsWideValue) {
  EXPECT_EQ(find_gpio_option_choice(kGroups[0], 256u), nullptr);
  EXPECT_EQ(find_gpio_option_choice(kGroups[0], 257u), nullptr);
}

TEST(GpioOptions, ResolveReturnsSelectedChoicePin) {
  const uint8_t sel[] = {1};
  EXPECT_EQ(resolve_gpio_pin(kCatalog, sel, 1, GpioPinRole::Wup1, kFallback), 18);
  EXPECT_EQ(resolve_gpio_pin(kCatalog, sel, 1, GpioPinRole::DisplaySda, kFallback), 1);
}

// A role the selected choice defines as NC resolves to NC, not the fallback:
// "disabled under this choice" is a real assignment, not an absent role.
TEST(GpioOptions, ResolveDisabledRoleReturnsNotConnected) {
  const uint8_t sel[] = {0};
  EXPECT_EQ(resolve_gpio_pin(kCatalog, sel, 1, GpioPinRole::DisplaySda, kFallback), kGpioPinNotConnected);
  EXPECT_EQ(resolve_gpio_pin(kCatalog, sel, 1, GpioPinRole::Wup1, kFallback), 1);
}

TEST(GpioOptions, ResolveUnknownRoleFallsBack) {
  const uint8_t sel[] = {0};
  EXPECT_EQ(resolve_gpio_pin(kCatalog, sel, 1, GpioPinRole::Led, kFallback), kFallback);
}

TEST(GpioOptions, ResolveUnknownSelectionFallsBack) {
  const uint8_t sel[] = {9};
  EXPECT_EQ(resolve_gpio_pin(kCatalog, sel, 1, GpioPinRole::Wup1, kFallback), kFallback);
}

TEST(GpioOptions, ResolveHonoursSelectionCount) {
  const uint8_t sel[] = {1};
  EXPECT_EQ(resolve_gpio_pin(kCatalog, sel, 0, GpioPinRole::Wup1, kFallback), kFallback);
}

TEST(GpioOptions, ResolveEmptyCatalogFallsBack) {
  const uint8_t sel[] = {0};
  EXPECT_EQ(resolve_gpio_pin(kEmptyCatalog, sel, 1, GpioPinRole::Wup1, kFallback), kFallback);
}

TEST(GpioOptions, ChoicesValidRejectsFirstNonZero) {
  static constexpr GpioOptionChoice choices[] = {{1, "a", kWupPins, 5, 0}};
  static constexpr GpioOptionGroup group = {"BAD", "x", choices, 1, 0};
  EXPECT_FALSE(gpio_choices_valid(group));
}

TEST(GpioOptions, ChoicesValidRejectsNonAscending) {
  static constexpr GpioOptionChoice choices[] = {
      {0, "a", kWupPins, 5, 0},
      {2, "b", kDisplayPins, 5, 0},
      {2, "c", kEstopPins, 5, 0},
  };
  static constexpr GpioOptionGroup group = {"BAD", "x", choices, 3, 0};
  EXPECT_FALSE(gpio_choices_valid(group));
}

TEST(GpioOptions, CoverSameRolesRejectsDivergentChoices) {
  static constexpr PinAssignment a[] = {{GpioPinRole::Led, 5}};
  static constexpr PinAssignment b[] = {{GpioPinRole::SdCs, 6}};
  static constexpr GpioOptionChoice choices[] = {{0, "a", a, 1, 0}, {1, "b", b, 1, 0}};
  static constexpr GpioOptionGroup group = {"MIX", "x", choices, 2, 0};
  EXPECT_FALSE(gpio_choices_cover_same_roles(group));
}

TEST(GpioOptions, GroupsRolesDisjointRejectsSharedRole) {
  static constexpr PinAssignment g1[] = {{GpioPinRole::Led, 5}};
  static constexpr PinAssignment g2[] = {{GpioPinRole::Led, 7}};
  static constexpr GpioOptionChoice c1[] = {{0, "a", g1, 1, 0}};
  static constexpr GpioOptionChoice c2[] = {{0, "b", g2, 1, 0}};
  static constexpr GpioOptionGroup groups[] = {{"G1", "g1", c1, 1, 0}, {"G2", "g2", c2, 1, 0}};
  static constexpr GpioOptionCatalog catalog = make_gpio_option_catalog(groups);
  EXPECT_FALSE(gpio_groups_roles_disjoint(catalog));
}

// resolve_gpio_pin does not itself enforce disjointness; the earlier group wins
// so a mis-authored catalog is at least deterministic.
TEST(GpioOptions, ResolveEarlierGroupWinsOnOverlap) {
  static constexpr PinAssignment g1[] = {{GpioPinRole::Led, 5}};
  static constexpr PinAssignment g2[] = {{GpioPinRole::Led, 7}};
  static constexpr GpioOptionChoice c1[] = {{0, "a", g1, 1, 0}};
  static constexpr GpioOptionChoice c2[] = {{0, "b", g2, 1, 0}};
  static constexpr GpioOptionGroup groups[] = {{"G1", "g1", c1, 1, 0}, {"G2", "g2", c2, 1, 0}};
  static constexpr GpioOptionCatalog catalog = make_gpio_option_catalog(groups);
  const uint8_t sel[] = {0, 0};
  EXPECT_EQ(resolve_gpio_pin(catalog, sel, 2, GpioPinRole::Led, kFallback), 5);
}

}  // namespace
