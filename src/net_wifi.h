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

// Non-blocking reconnect kick for the poll task's WiFi supervisor. A transient outage (router
// reboot, DHCP renewal, AP roam) is near-certain over a multi-day uptime, and this box is
// mains-powered and always on — without a supervisor it sits alive-but-wedged until someone
// pulls the USB, with the web UI *and* OTA both dead, so you can't even push a fix. Re-issuing
// the connect ourselves is more reliable than the framework's implicit auto-reconnect, which can
// stay stuck after certain disconnect reasons — notably AUTH_EXPIRE, this board's characteristic
// failure when the U.FL antenna is marginal. Does NOT wait; the caller re-checks isConnected().
// (Ported from sonos-nest wifiReconnect(), commit f58573d.)
void      reconnect();

// Count of supervisor-driven reconnect kicks since boot — surfaced on the status page, because
// "has this actually been dropping?" is the first question when a unit misbehaves after days up.
uint32_t  reconnectCount();

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
