#include "static_assets.h"

#include <string.h>

#include "generated/web_assets.h"

// Assets are not content-addressed by filename, so the browser must
// revalidate; the content-hash ETag then makes the answer a cheap 304.
static constexpr const char* kAssetCacheControl = "no-cache";
static constexpr const char* kIfNoneMatchHeader = "If-None-Match";
static constexpr const char* kEtagHeader = "ETag";
static constexpr const char* kCacheControlHeader = "Cache-Control";
static constexpr const char* kContentEncodingHeader = "Content-Encoding";
static constexpr const char* kGzipEncoding = "gzip";
static constexpr int kHttpOk = 200;
static constexpr int kHttpNotModified = 304;

static const WebAsset* find_web_asset(const char* path) {
  for (uint32_t i = 0; i < kWebAssetCount; i++) {
    if (strcmp(kWebAssets[i].path, path) == 0) {
      return &kWebAssets[i];
    }
  }
  return nullptr;
}

bool serve_web_asset(AsyncWebServerRequest* request, const char* path) {
  const WebAsset* asset = find_web_asset(path);
  if (asset == nullptr) {
    return false;
  }

  const AsyncWebHeader* if_none_match = request->getHeader(kIfNoneMatchHeader);
  if (if_none_match != nullptr && if_none_match->value() == asset->etag) {
    AsyncWebServerResponse* not_modified = request->beginResponse(kHttpNotModified);
    not_modified->addHeader(kEtagHeader, asset->etag);
    not_modified->addHeader(kCacheControlHeader, kAssetCacheControl);
    request->send(not_modified);
    return true;
  }

  AsyncWebServerResponse* response = request->beginResponse(kHttpOk, asset->content_type, asset->data, asset->len);
  response->addHeader(kContentEncodingHeader, kGzipEncoding);
  response->addHeader(kEtagHeader, asset->etag);
  response->addHeader(kCacheControlHeader, kAssetCacheControl);
  request->send(response);
  return true;
}

void for_each_web_asset_path(void (*visit)(const char* path)) {
  for (uint32_t i = 0; i < kWebAssetCount; i++) {
    visit(kWebAssets[i].path);
  }
}
