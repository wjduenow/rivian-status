// net_wifi — WiFi bring-up, the DHCP hostname (= device name), runtime cred changes, and the
// SoftAP captive-portal provisioning fallback (plan §8/§9, Phase 4). Patterns borrowed from
// sonos-nest (src/core/net/wifi.cpp + portal.cpp), adapted to the Settings module.
//
// Hostname: the DHCP hostname, mDNS name, and UI title all come from Settings::deviceName().
// WiFi.setHostname() must be set BEFORE the STA mode transition or it's silently ignored
// (arduino-esp32 quirk documented in sonos-nest) — connect() does this.
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace Net {

String    hostname();                    // Settings::deviceName() (already sanitized)
bool      connect(uint32_t timeoutMs);   // set hostname, try NVS creds then secrets.h; wait
bool      isConnected();
String    ssid();
IPAddress ip();

// Runtime credential change (from the portal): try them, persist on success, revert on failure.
enum { APPLY_IDLE = 0, APPLY_OK = 1, APPLY_FAIL = 2 };
void apply(const String& ssid, const String& pass);
int  applyResult();
void applyResultReset();

// Blocking SoftAP captive portal (open AP "<name>-setup" + wildcard DNS + join page that also
// sets the device name). Called when connect() fails. Reboots after a successful join so the
// DHCP hostname / mDNS come up clean.
void runPortal();

} // namespace Net
