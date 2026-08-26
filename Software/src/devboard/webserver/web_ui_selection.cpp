#include "web_ui_selection.h"

#include <string.h>

namespace {

size_t shell_name_length(const char* path) {
  return strlen(path) - strlen(kUiShellPathPrefix) - strlen(kUiShellPathSuffix);
}

const char* shell_name_start(const char* path) {
  const size_t prefix_len = strlen(kUiShellPathPrefix);
  const size_t suffix_len = strlen(kUiShellPathSuffix);
  const size_t len = strlen(path);
  if (len <= prefix_len + suffix_len) {
    return nullptr;
  }
  if (strncmp(path, kUiShellPathPrefix, prefix_len) != 0) {
    return nullptr;
  }
  if (strcmp(path + len - suffix_len, kUiShellPathSuffix) != 0) {
    return nullptr;
  }
  return path + prefix_len;
}

const char* shell_asset_for_name(UiShellTable table, const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return nullptr;
  }
  const size_t name_len = strlen(name);
  for (uint32_t i = 0; i < table.count; i++) {
    const char* path = table.assets[i].path;
    const char* start = shell_name_start(path);
    if (start == nullptr) {
      continue;
    }
    if (shell_name_length(path) == name_len && strncmp(start, name, name_len) == 0) {
      return path;
    }
  }
  return nullptr;
}

}  // namespace

size_t ui_shell_count(UiShellTable table) {
  size_t count = 0;
  for (uint32_t i = 0; i < table.count; i++) {
    if (shell_name_start(table.assets[i].path) != nullptr) {
      count++;
    }
  }
  return count;
}

bool ui_shell_name_at(UiShellTable table, size_t index, char* out, size_t out_len) {
  size_t seen = 0;
  for (uint32_t i = 0; i < table.count; i++) {
    const char* path = table.assets[i].path;
    const char* start = shell_name_start(path);
    if (start == nullptr) {
      continue;
    }
    if (seen == index) {
      const size_t len = shell_name_length(path);
      if (len + 1 > out_len) {
        return false;
      }
      memcpy(out, start, len);
      out[len] = '\0';
      return true;
    }
    seen++;
  }
  return false;
}

const char* resolve_ui_shell_asset(UiShellTable table, const char* requested, const char* stored) {
  const char* asset = shell_asset_for_name(table, requested);
  if (asset != nullptr) {
    return asset;
  }
  asset = shell_asset_for_name(table, stored);
  if (asset != nullptr) {
    return asset;
  }
  asset = shell_asset_for_name(table, kDefaultUiShell);
  if (asset != nullptr) {
    return asset;
  }
  for (uint32_t i = 0; i < table.count; i++) {
    if (shell_name_start(table.assets[i].path) != nullptr) {
      return table.assets[i].path;
    }
  }
  return nullptr;
}
