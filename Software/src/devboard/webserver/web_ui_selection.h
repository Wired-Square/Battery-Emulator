#ifndef WEB_UI_SELECTION_H
#define WEB_UI_SELECTION_H

#include <stddef.h>
#include <stdint.h>

#include "generated/web_assets.h"

constexpr const char* kUiShellPathPrefix = "/shell-";
constexpr const char* kUiShellPathSuffix = ".html";
constexpr const char* kDefaultUiShell = "legacy";
constexpr size_t kMaxUiShellNameLen = 31;

struct UiShellTable {
  const WebAsset* assets;
  uint32_t count;
};

UiShellTable default_ui_shell_table();

size_t ui_shell_count(UiShellTable table);

bool ui_shell_name_at(UiShellTable table, size_t index, char* out, size_t out_len);

const char* resolve_ui_shell_asset(UiShellTable table, const char* requested, const char* stored);

#endif
