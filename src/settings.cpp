#include "settings.h"
#include <Preferences.h>

#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "rivian-status"
#endif
#ifndef LED_BRIGHTNESS
#define LED_BRIGHTNESS 40                  // default cap; matches the phase3 build flag
#endif

static const char* NS = "cfg";

static int    s_thresholdMiles = 50;
static int    s_ledBrightness  = LED_BRIGHTNESS;
static int    s_ledRotation    = 0;
static bool   s_ledStickVert0  = false;    // v1 box (stick horizontal at plug-down) — see below
static bool   s_ledInverted    = false;
static String s_deviceName     = DEVICE_HOSTNAME;

void Settings::begin() {
  Preferences p;
  p.begin(NS, false);                      // read-write: creates the namespace on first boot
                                           // (a read-only open would log nvs_open NOT_FOUND)
  s_thresholdMiles = p.getInt("thresh_mi", 50);
  s_ledBrightness  = p.getInt("led_bri", LED_BRIGHTNESS);
  // Default carries over the older boolean flip, so a unit already set to "flipped" lands on 180°.
  s_ledRotation    = p.getInt("led_rot", p.getBool("led_flip", false) ? 180 : 0);
  // Defaults describe the unit actually in service: the v1 box, mounted board-to-the-wall with
  // the LEDs facing out, which puts the stick horizontal when the plug is at the bottom.
  s_ledStickVert0  = p.getBool("led_vert0", false);
  s_ledInverted    = p.getBool("led_inv", false);
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

int Settings::ledBrightness() { return s_ledBrightness; }

void Settings::setLedBrightness(int b) {
  if (b < 1)   b = 1;
  if (b > 255) b = 255;
  s_ledBrightness = b;
  Preferences p;
  p.begin(NS, false);
  p.putInt("led_bri", b);
  p.end();
}

int Settings::ledRotation() { return s_ledRotation; }

void Settings::setLedRotation(int deg) {
  deg = ((deg % 360) + 360) % 360;          // accept negatives / over-turns
  deg = ((deg + 45) / 90 * 90) % 360;       // snap to the nearest quarter turn
  s_ledRotation = deg;
  Preferences p;
  p.begin(NS, false);
  p.putInt("led_rot", deg);
  p.end();
}

bool Settings::ledStickVertical0() { return s_ledStickVert0; }

void Settings::setLedStickVertical0(bool vertical) {
  s_ledStickVert0 = vertical;
  Preferences p;
  p.begin(NS, false);
  p.putBool("led_vert0", vertical);
  p.end();
}

bool Settings::ledInverted() { return s_ledInverted; }

void Settings::setLedInverted(bool inverted) {
  s_ledInverted = inverted;
  Preferences p;
  p.begin(NS, false);
  p.putBool("led_inv", inverted);
  p.end();
}

// Where pixel 0 points once the device is on the wall, as clockwise degrees off screen-up
// (0 = up, 90 = right, 180 = down, 270 = left). Everything else falls out of this one number.
//
// At the reference mounting (plug at the bottom) pixel 0 sits at the bottom if the stick is
// vertical, at the left if it's horizontal — "inverted" swaps that to the opposite end. Rotating
// the device clockwise carries pixel 0 round with it, hence the += rotation.
static int pixel0Dir() {
  int d = s_ledStickVert0 ? 180 : 270;      // bottom : left
  if (s_ledInverted) d += 180;
  return (d + s_ledRotation) % 360;
}

// The stick is vertical exactly when pixel 0 points up or down.
bool Settings::ledVertical() { int d = pixel0Dir(); return d == 0 || d == 180; }

// The meter fills AWAY from pixel 0, and we want it filling up (vertical) or right (horizontal).
// That's satisfied when pixel 0 points down or left; the other two cases need the reverse.
bool Settings::ledFlipped() { int d = pixel0Dir(); return d == 0 || d == 90; }

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
