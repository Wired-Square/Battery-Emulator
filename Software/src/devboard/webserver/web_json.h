#ifndef WEB_JSON_H
#define WEB_JSON_H

#include <WString.h>

#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"

// The host String stand-in is not an ArduinoJson writer, so serialise via
// std::string there.
String serialise_doc(const JsonDocument& doc);

// A slot the JSON API accepts writes for: slot 0 always — settings can be
// staged before a battery type is selected — slots 1+ only when they hold a
// configured battery.
bool battery_slot_addressable(uint8_t slot);

// Resolves a request's target battery slot from its "battery" field. Returns
// nullptr and sets slot on success, else the message for the 400 response.
// Absent means the primary.
const char* validate_battery_slot(const JsonDocument& doc, uint8_t& slot);

#endif
