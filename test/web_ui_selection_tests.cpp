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

WebAssetTable three() {
  return WebAssetTable{kThreeAssets, 3};
}

const char* shell(WebAssetTable table, const char* requested, const char* stored) {
  return resolve_named_asset(table, kUiShellSpec, requested, stored, kDefaultUiShell);
}

}  // namespace

TEST(WebUiSelection, CountsOnlyShellAssets) {
  EXPECT_EQ(web_asset_name_count(three(), kUiShellSpec), 2u);
}

TEST(WebUiSelection, NamesAreBare) {
  char name[kMaxAssetNameLen + 1];
  ASSERT_TRUE(web_asset_name_at(three(), kUiShellSpec, 0, name, sizeof(name)));
  EXPECT_STREQ(name, "legacy");
  ASSERT_TRUE(web_asset_name_at(three(), kUiShellSpec, 1, name, sizeof(name)));
  EXPECT_STREQ(name, "modern");
}

TEST(WebUiSelection, RejectsIndexPastEnd) {
  char name[kMaxAssetNameLen + 1];
  EXPECT_FALSE(web_asset_name_at(three(), kUiShellSpec, 2, name, sizeof(name)));
}

TEST(WebUiSelection, RejectsUndersizedNameBuffer) {
  char name[3];
  EXPECT_FALSE(web_asset_name_at(three(), kUiShellSpec, 0, name, sizeof(name)));
}

TEST(WebUiSelection, ResolvesStoredName) {
  EXPECT_STREQ(shell(three(), nullptr, "modern"), "/shell-modern.html");
}

TEST(WebUiSelection, RequestedOverridesStored) {
  EXPECT_STREQ(shell(three(), "modern", "legacy"), "/shell-modern.html");
}

TEST(WebUiSelection, UnknownRequestedFallsBackToStored) {
  EXPECT_STREQ(shell(three(), "nope", "modern"), "/shell-modern.html");
}

TEST(WebUiSelection, UnknownStoredFallsBackToDefault) {
  EXPECT_STREQ(shell(three(), nullptr, "nope"), "/shell-legacy.html");
}

TEST(WebUiSelection, EmptyInputsUseDefault) {
  EXPECT_STREQ(shell(three(), "", ""), "/shell-legacy.html");
}

TEST(WebUiSelection, AbsentDefaultFallsBackToFirstShippedShell) {
  const WebAssetTable table{kNoDefaultAssets, 2};
  EXPECT_STREQ(shell(table, nullptr, "nope"), "/shell-modern.html");
}

TEST(WebUiSelection, NoShellsResolvesToNull) {
  const WebAssetTable table{kNoShellAssets, 1};
  EXPECT_EQ(shell(table, nullptr, nullptr), nullptr);
}

TEST(WebUiSelection, IgnoresPrefixWithoutSuffix) {
  const WebAsset assets[] = {{"/shell-legacy.htm", kStubData, 1, "text/html", "\"b\""}};
  const WebAssetTable table{assets, 1};
  EXPECT_EQ(web_asset_name_count(table, kUiShellSpec), 0u);
}

TEST(WebUiSelection, IgnoresEmptyName) {
  const WebAsset assets[] = {{"/shell-.html", kStubData, 1, "text/html", "\"b\""}};
  const WebAssetTable table{assets, 1};
  EXPECT_EQ(web_asset_name_count(table, kUiShellSpec), 0u);
}





