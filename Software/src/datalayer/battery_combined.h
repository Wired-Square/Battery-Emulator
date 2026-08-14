#ifndef BATTERY_COMBINED_H
#define BATTERY_COMBINED_H

#include "../system_settings.h"

/** Recomputes per-pack reported values and composes datalayer.battery.combined.
 * Slot 0 is always composed; populated[] gates which further slots fold in
 * (mirrors batteries[slot] presence — passed in so this unit stays free of
 * battery-module dependencies). */
void update_reported_values(const bool (&populated)[kMaxBatterySlots]);

#endif
