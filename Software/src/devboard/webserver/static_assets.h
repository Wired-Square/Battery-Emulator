#ifndef STATIC_ASSETS_H
#define STATIC_ASSETS_H

#include "../../lib/ESP32Async-ESPAsyncWebServer/src/ESPAsyncWebServer.h"

// Serves a gzipped PROGMEM asset. Returns false when the path is unknown; an
// unknown path means the route table and the generated assets disagree, so the
// caller answers 500 rather than 404.
bool serve_web_asset(AsyncWebServerRequest* request, const char* path);

// Lets callers build routes from the generated set rather than a parallel list that drifts.
void for_each_web_asset_path(void (*visit)(const char* path));

#endif
