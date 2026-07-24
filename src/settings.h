// settings — small NVS-backed config the web page edits (plan §8/§9). Distinct from the token/
// dc-cid persistence in rivian_api; this is user-facing config: the low-range threshold (miles)
// and the device/mDNS name.
#pragma once

#include <Arduino.h>

namespace Settings {

void begin();                              // open NVS namespace, load defaults

int  rangeThresholdMiles();                // low-range alert threshold X, in miles (default 50)
void setRangeThresholdMiles(int miles);    // clamped to [1, 500], persisted

int  ledBrightness();                      // LED strip brightness 0-255 (default LED_BRIGHTNESS=40)
void setLedBrightness(int b);              // clamped to [1, 255], persisted

// Physical mounting orientation, expressed the way the user can actually see it: **where the
// USB-C / power lead leaves the case**, as a rotation in degrees clockwise off the natural
// mounting. An outlet's orientation dictates how the device hangs, so all four are realistic.
//   0 = plug at the BOTTOM (natural)   90 = plug at the LEFT
// 180 = plug at the TOP                270 = plug at the RIGHT
// Invariant the firmware maintains: the meter always fills toward screen-**up** when the stick
// lands vertical, and toward screen-**right** when it lands horizontal. leds.cpp compensates.
int  ledRotation();                        // 0 | 90 | 180 | 270 (default 0)
void setLedRotation(int deg);              // snapped to the nearest quarter turn, persisted

bool ledFlipped();                         // derived: does the strip need reversing? (rot >= 180)
bool ledVertical();                        // derived: is the stick vertical? (rot 0 or 180)

String deviceName();                       // DHCP hostname + mDNS + UI title (default DEVICE_HOSTNAME)
void   setDeviceName(const String& name);  // sanitized to DNS-safe, then persisted

// Sanitize to a valid DHCP/mDNS hostname: letters, digits, hyphens; space/underscore -> hyphen;
// no leading/trailing hyphen. "" if nothing usable survives. (Borrowed from sonos-nest.)
String sanitizeHostname(const String& raw);

// WiFi credentials (NVS). Empty ssid = never provisioned on-device (fall back to secrets.h).
String wifiSsid();
String wifiPass();
void   setWifi(const String& ssid, const String& pass);

} // namespace Settings
