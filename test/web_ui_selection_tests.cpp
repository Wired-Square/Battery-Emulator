#include <gtest/gtest.h>
#include <cstring>

#include "../Software/src/devboard/webserver/web_ui_selection.h"

namespace {

const uint8_t kStubData[] = {0};

const WebAsset kThreeAssets[] = {
    {"/app.css", kStubData, 1, "text/css", "\"a\""},
    {"/shell-legacy.html", kStubData, 1, "text/html", "\"b\""},
    {"/shell-modern.html", kStubData, 1, "text/html", "\"c\""},
};

const WebAsset kNoDefaultAssets[] = {
    {"/app.css", kStubData, 1, "text/css", "\"a\""},
    {"/shell-modern.html", kStubData, 1, "text/html", "\"c\""},
};

const WebAsset kNoShellAssets[] = {
    {"/app.css", kStubData, 1, "text/css", "\"a\""},
};

UiShellTable three() {
  return UiShellTable{kThreeAssets, 3};
}

}  // namespace

TEST(WebUiSelection, CountsOnlyShellAssets) {
  EXPECT_EQ(ui_shell_count(three()), 2u);
}

TEST(WebUiSelection, NamesAreBare) {
  char name[kMaxUiShellNameLen + 1];
  ASSERT_TRUE(ui_shell_name_at(three(), 0, name, sizeof(name)));
  EXPECT_STREQ(name, "legacy");
  ASSERT_TRUE(ui_shell_name_at(three(), 1, name, sizeof(name)));
  EXPECT_STREQ(name, "modern");
}

TEST(WebUiSelection, RejectsIndexPastEnd) {
  char name[kMaxUiShellNameLen + 1];
  EXPECT_FALSE(ui_shell_name_at(three(), 2, name, sizeof(name)));
}

TEST(WebUiSelection, RejectsUndersizedNameBuffer) {
  char name[3];
  EXPECT_FALSE(ui_shell_name_at(three(), 0, name, sizeof(name)));
}

TEST(WebUiSelection, ResolvesStoredName) {
  EXPECT_STREQ(resolve_ui_shell_asset(three(), nullptr, "modern"), "/shell-modern.html");
}

TEST(WebUiSelection, RequestedOverridesStored) {
  EXPECT_STREQ(resolve_ui_shell_asset(three(), "modern", "legacy"), "/shell-modern.html");
}

TEST(WebUiSelection, UnknownRequestedFallsBackToStored) {
  EXPECT_STREQ(resolve_ui_shell_asset(three(), "nope", "modern"), "/shell-modern.html");
}

TEST(WebUiSelection, UnknownStoredFallsBackToDefault) {
  EXPECT_STREQ(resolve_ui_shell_asset(three(), nullptr, "nope"), "/shell-legacy.html");
}

TEST(WebUiSelection, EmptyInputsUseDefault) {
  EXPECT_STREQ(resolve_ui_shell_asset(three(), "", ""), "/shell-legacy.html");
}

TEST(WebUiSelection, AbsentDefaultFallsBackToFirstShippedShell) {
  const UiShellTable table{kNoDefaultAssets, 2};
  EXPECT_STREQ(resolve_ui_shell_asset(table, nullptr, "nope"), "/shell-modern.html");
}

TEST(WebUiSelection, NoShellsResolvesToNull) {
  const UiShellTable table{kNoShellAssets, 1};
  EXPECT_EQ(resolve_ui_shell_asset(table, nullptr, nullptr), nullptr);
}

TEST(WebUiSelection, IgnoresPrefixWithoutSuffix) {
  const WebAsset assets[] = {{"/shell-legacy.htm", kStubData, 1, "text/html", "\"b\""}};
  const UiShellTable table{assets, 1};
  EXPECT_EQ(ui_shell_count(table), 0u);
}

TEST(WebUiSelection, IgnoresEmptyName) {
  const WebAsset assets[] = {{"/shell-.html", kStubData, 1, "text/html", "\"b\""}};
  const UiShellTable table{assets, 1};
  EXPECT_EQ(ui_shell_count(table), 0u);
}
