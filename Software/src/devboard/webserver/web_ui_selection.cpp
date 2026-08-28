#include "web_ui_selection.h"

#include <string.h>

namespace {

size_t name_length(AssetNameSpec spec, const char* path) {
  return strlen(path) - strlen(spec.prefix) - strlen(spec.suffix);
}

const char* name_start(AssetNameSpec spec, const char* path) {
  const size_t prefix_len = strlen(spec.prefix);
  const size_t suffix_len = strlen(spec.suffix);
  const size_t len = strlen(path);
  if (len <= prefix_len + suffix_len) {
    return nullptr;
  }
  if (strncmp(path, spec.prefix, prefix_len) != 0) {
    return nullptr;
  }
  if (strcmp(path + len - suffix_len, spec.suffix) != 0) {
    return nullptr;
  }
  return path + prefix_len;
}

bool copy_name(AssetNameSpec spec, const char* path, char* out, size_t out_len) {
  const char* start = name_start(spec, path);
  if (start == nullptr) {
    return false;
  }
  const size_t len = name_length(spec, path);
  if (len + 1 > out_len) {
    return false;
  }
  memcpy(out, start, len);
  out[len] = '\0';
  return true;
}

const char* first_matching_path(WebAssetTable table, AssetNameSpec spec) {
  for (uint32_t i = 0; i < table.count; i++) {
    if (name_start(spec, table.assets[i].path) != nullptr) {
      return table.assets[i].path;
    }
  }
  return nullptr;
}

}  // namespace

size_t web_asset_name_count(WebAssetTable table, AssetNameSpec spec) {
  size_t count = 0;
  for (uint32_t i = 0; i < table.count; i++) {
    if (name_start(spec, table.assets[i].path) != nullptr) {
      count++;
    }
  }
  return count;
}

bool web_asset_name_at(WebAssetTable table, AssetNameSpec spec, size_t index, char* out, size_t out_len) {
  size_t seen = 0;
  for (uint32_t i = 0; i < table.count; i++) {
    const char* path = table.assets[i].path;
    if (name_start(spec, path) == nullptr) {
      continue;
    }
    if (seen == index) {
      return copy_name(spec, path, out, out_len);
    }
    seen++;
  }
  return false;
}

const char* web_asset_path_for_name(WebAssetTable table, AssetNameSpec spec, const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return nullptr;
  }
  const size_t len = strlen(name);
  for (uint32_t i = 0; i < table.count; i++) {
    const char* path = table.assets[i].path;
    const char* start = name_start(spec, path);
    if (start == nullptr) {
      continue;
    }
    if (name_length(spec, path) == len && strncmp(start, name, len) == 0) {
      return path;
    }
  }
  return nullptr;
}

const char* resolve_named_asset(WebAssetTable table, AssetNameSpec spec, const char* requested, const char* stored,
                                const char* fallback) {
  const char* path = web_asset_path_for_name(table, spec, requested);
  if (path != nullptr) {
    return path;
  }
  path = web_asset_path_for_name(table, spec, stored);
  if (path != nullptr) {
    return path;
  }
  path = web_asset_path_for_name(table, spec, fallback);
  if (path != nullptr) {
    return path;
  }
  return first_matching_path(table, spec);
}
