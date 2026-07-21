// settings — small NVS-backed config the web page edits (plan §8/§9). Distinct from the token/
// dc-cid persistence in rivian_api; this is user-facing config: the low-range threshold (miles)
// and the device/mDNS name.
#pragma once

#include <Arduino.h>

namespace Settings {

void begin();                              // open NVS namespace, load defaults

int  rangeThresholdMiles();                // low-range alert threshold X, in miles (default 50)
void setRangeThresholdMiles(int miles);    // clamped to [1, 500], persisted

String deviceName();                       // mDNS/host + UI title (default DEVICE_HOSTNAME)
void   setDeviceName(const String& name);  // persisted

} // namespace Settings
