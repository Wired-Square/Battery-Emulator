#include "logging_api.h"
#include "web_json.h"

#include <cstring>
#include <string>

#include "../../datalayer/datalayer.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"

// Defined in comm_can.cpp on device; a host stub supplies it for tests.
extern uint16_t user_selected_CAN_ID_cutoff_filter;

namespace {
// Empty segments are kept (legacy rendered blank rows). A trailing segment with
// no terminating '\n' is emitted only when keep_partial is set: the debug view
// shows an in-progress line, but the CAN view hid half-written frames.
//
// ArduinoJson links a bare const char* without copying; a std::string is copied
// into the document, so each line stays valid past this buffer's lifetime.
void add_lines(JsonArray lines, const char* buf, size_t buf_len, bool keep_partial) {
  size_t i = 0;
  while (i < buf_len && buf[i] != '\0') {
    size_t j = i;
    while (j < buf_len && buf[j] != '\0' && buf[j] != '\n') j++;
    const bool terminated = j < buf_len && buf[j] == '\n';
    if (terminated || keep_partial) lines.add(std::string(buf + i, j - i));
    if (!terminated) break;
    i = j + 1;
  }
}

}  // namespace

String build_canlog_json() {
  JsonDocument doc;
  const auto& info = datalayer.system.info;
  doc["active"] = info.can_logging_active;
  doc["sd"] = info.CAN_SD_logging_active;
  doc["cutoff"] = user_selected_CAN_ID_cutoff_filter;
  add_lines(doc["lines"].to<JsonArray>(), info.logged_can_messages, sizeof(info.logged_can_messages), false);
  return serialise_doc(doc);
}

String build_debug_json() {
  JsonDocument doc;
  const auto& info = datalayer.system.info;
  doc["web_active"] = info.web_logging_active;
  doc["sd_active"] = info.SD_logging_active;
  JsonArray lines = doc["lines"].to<JsonArray>();

  const char* buf = info.logged_can_messages;
  const size_t size = sizeof(info.logged_can_messages);
  const size_t offset = info.logged_can_messages_offset;
  // Wrapped: resume the older tail past the write head, then print the newer
  // head. A newline in the tail marks the first clean boundary (its leading
  // fragment was half-overwritten, so it is skipped); without one the whole
  // tail is a single fragment. Otherwise the buffer is linear.
  if (offset > 0 && offset < size - 1) {
    const char* boundary = (const char*)memchr(buf + offset + 1, '\n', size - offset - 1);
    if (boundary) add_lines(lines, boundary + 1, (buf + size) - (boundary + 1), true);
    else add_lines(lines, buf + offset + 1, size - offset - 1, true);
    add_lines(lines, buf, offset, true);
  } else {
    add_lines(lines, buf, size, true);
  }
  return serialise_doc(doc);
}
