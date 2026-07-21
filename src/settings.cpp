#include "settings.h"
#include <Preferences.h>

#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "rivian-status"
#endif

static const char* NS = "cfg";

static int    s_thresholdMiles = 50;
static String s_deviceName     = DEVICE_HOSTNAME;

void Settings::begin() {
  Preferences p;
  p.begin(NS, false);                      // read-write: creates the namespace on first boot
                                           // (a read-only open would log nvs_open NOT_FOUND)
  s_thresholdMiles = p.getInt("thresh_mi", 50);
  s_deviceName     = p.getString("dev_name", DEVICE_HOSTNAME);
  p.end();
}

int Settings::rangeThresholdMiles() { return s_thresholdMiles; }

void Settings::setRangeThresholdMiles(int miles) {
  if (miles < 1)   miles = 1;
  if (miles > 500) miles = 500;
  s_thresholdMiles = miles;
  Preferences p;
  p.begin(NS, false);
  p.putInt("thresh_mi", miles);
  p.end();
}

String Settings::deviceName() { return s_deviceName; }

// DNS-safe hostname: letters/digits/hyphens; space/underscore -> hyphen; trim edge hyphens.
String Settings::sanitizeHostname(const String& raw) {
  String h;
  for (unsigned i = 0; i < raw.length(); ++i) {
    char c = raw[i];
    bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (alnum)                                 h += c;
    else if (c == ' ' || c == '_' || c == '-') h += '-';
  }
  while (h.startsWith("-")) h.remove(0, 1);
  while (h.endsWith("-"))   h.remove(h.length() - 1);
  return h;
}

void Settings::setDeviceName(const String& name) {
  String n = sanitizeHostname(name);
  if (n.isEmpty()) n = DEVICE_HOSTNAME;
  if (n.length() > 32) n = n.substring(0, 32);
  s_deviceName = n;
  Preferences p;
  p.begin(NS, false);
  p.putString("dev_name", n);
  p.end();
}

String Settings::wifiSsid() {
  Preferences p; p.begin(NS, true);
  String s = p.getString("wifi_ssid", "");
  p.end(); return s;
}
String Settings::wifiPass() {
  Preferences p; p.begin(NS, true);
  String s = p.getString("wifi_pass", "");
  p.end(); return s;
}
void Settings::setWifi(const String& ssid, const String& pass) {
  Preferences p; p.begin(NS, false);
  p.putString("wifi_ssid", ssid);
  p.putString("wifi_pass", pass);
  p.end();
}
