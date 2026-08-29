#ifndef STREAMED_RESPONSE_H
#define STREAMED_RESPONSE_H

#include "../../lib/ESP32Async-ESPAsyncWebServer/src/ESPAsyncWebServer.h"
#include "response_writer.h"

// Answers with a chunked response produced on a dedicated task. Only one
// producer runs at a time; a request that arrives while it is busy is answered
// from the buffered writer rather than refused.
void send_streamed(AsyncWebServerRequest* request, const char* content_type, ResponseProducer producer);

#endif
