#ifndef WEB_UI_SELECTION_H
#define WEB_UI_SELECTION_H

#include <stddef.h>
#include <stdint.h>

#include "generated/web_assets.h"

struct AssetNameSpec {
  const char* prefix;
  const char* suffix;
};

constexpr AssetNameSpec kUiShellSpec{"/shell-", ".html"};

constexpr const char* kDefaultUiShell = "legacy";
constexpr size_t kMaxAssetNameLen = 31;

struct WebAssetTable {
  const WebAsset* assets;
  uint32_t count;
};

WebAssetTable default_web_asset_table();

size_t web_asset_name_count(WebAssetTable table, AssetNameSpec spec);

bool web_asset_name_at(WebAssetTable table, AssetNameSpec spec, size_t index, char* out, size_t out_len);

const char* web_asset_path_for_name(WebAssetTable table, AssetNameSpec spec, const char* name);

const char* resolve_named_asset(WebAssetTable table, AssetNameSpec spec, const char* requested, const char* stored,
                                const char* fallback);


#endif
