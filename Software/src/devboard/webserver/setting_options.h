#ifndef SETTING_OPTIONS_H
#define SETTING_OPTIONS_H

#include <cstdint>

// Response emission and POST membership checking both walk the producer through
// this sink, so a published list cannot drift from the one that is enforced.
class SettingOptionSink {
 public:
  virtual ~SettingOptionSink() = default;

  virtual void begin_list(const char* key) = 0;
  // slots == 0 omits the "s" hint; name == nullptr omits "n".
  virtual void option(int32_t value, const char* name, int32_t slots) = 0;
  virtual void text_option(const char* value) = 0;
  virtual void end_list() = 0;
};

void emit_all_options(SettingOptionSink& sink);

#endif
