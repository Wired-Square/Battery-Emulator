#ifndef BATTERY_SLOT_API_H
#define BATTERY_SLOT_API_H

#include "document_reader.h"

// A slot the JSON API accepts writes for: slot 0 always — settings can be
// staged before a battery type is selected — slots 1+ only when they hold a
// configured battery.
bool battery_slot_addressable(uint8_t slot);

// Resolves a request's target battery slot from its "battery" field. Returns
// nullptr and sets slot on success, else the message for the 400 response.
// Absent means the primary.
const char* validate_battery_slot(const ValueSource& body, uint8_t& slot);

#endif
