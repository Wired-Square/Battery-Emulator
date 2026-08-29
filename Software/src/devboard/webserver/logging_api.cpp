#include "logging_api.h"

#include <cstring>

#include "../../datalayer/datalayer.h"

// Defined in comm_can.cpp on device; a host stub supplies it for tests.
extern uint16_t user_selected_CAN_ID_cutoff_filter;

namespace {
// Empty segments are kept (legacy rendered blank rows). A trailing segment with
// no terminating '\n' is emitted only when keep_partial is set: the debug view
// shows an in-progress line, but the CAN view hid half-written frames.
void add_lines(ResponseWriter& out, const char* buf, size_t buf_len, bool keep_partial) {
  size_t i = 0;
  while (i < buf_len && buf[i] != '\0') {
    size_t j = i;
    while (j < buf_len && buf[j] != '\0' && buf[j] != '\n') j++;
    const bool terminated = j < buf_len && buf[j] == '\n';
    if (terminated || keep_partial) out.element(buf + i, j - i);
    if (!terminated) break;
    i = j + 1;
  }
}

}  // namespace

void write_canlog(ResponseWriter& out) {
  const auto& info = datalayer.system.info;
  out.begin_object();
  out.field("active", info.can_logging_active);
  out.field("sd", info.CAN_SD_logging_active);
  out.field("cutoff", user_selected_CAN_ID_cutoff_filter);
  out.begin_array("lines");
  add_lines(out, info.logged_can_messages, sizeof(info.logged_can_messages), false);
  out.end_array();
  out.end_object();
}

void write_debug(ResponseWriter& out) {
  const auto& info = datalayer.system.info;
  out.begin_object();
  out.field("web_active", info.web_logging_active);
  out.field("sd_active", info.SD_logging_active);
  out.begin_array("lines");

  const char* buf = info.logged_can_messages;
  const size_t size = sizeof(info.logged_can_messages);
  const size_t offset = info.logged_can_messages_offset;
  // Wrapped: resume the older tail past the write head, then print the newer
  // head. A newline in the tail marks the first clean boundary (its leading
  // fragment was half-overwritten, so it is skipped); without one the whole
  // tail is a single fragment. Otherwise the buffer is linear.
  if (offset > 0 && offset < size - 1) {
    const char* boundary = (const char*)memchr(buf + offset + 1, '\n', size - offset - 1);
    if (boundary) add_lines(out, boundary + 1, (buf + size) - (boundary + 1), true);
    else add_lines(out, buf + offset + 1, size - offset - 1, true);
    add_lines(out, buf, offset, true);
  } else {
    add_lines(out, buf, size, true);
  }
  out.end_array();
  out.end_object();
}
