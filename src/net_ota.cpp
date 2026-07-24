#include "net_ota.h"
#include "net_wifi.h"     // Net::hostname() — device name, same source as the DHCP hostname
#include <ArduinoOTA.h>
#include <WiFi.h>

#if __has_include("secrets.h")
#include "secrets.h"      // optional: #define OTA_PASSWORD "..."
#endif

static volatile bool s_active   = false;
static volatile int  s_progress = -1;

void otaBegin() {
  if (WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.setHostname(Net::hostname().c_str());
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#else
#warning "No OTA_PASSWORD in secrets.h — OTA is unauthenticated (anyone on the LAN can flash it)."
#endif
  ArduinoOTA.onStart([]()  { s_active = true;  s_progress = 0;  Serial.println("\n[ota] start"); });
  ArduinoOTA.onEnd([]()    { s_active = false; s_progress = -1; Serial.println("[ota] done — rebooting"); });
  ArduinoOTA.onProgress([](unsigned p, unsigned t) {
    s_progress = t ? (int)(p * 100 / t) : 0;
    Serial.printf("[ota] %d%%\r", s_progress);
  });
  ArduinoOTA.onError([](ota_error_t e) { s_active = false; s_progress = -1; Serial.printf("[ota] error %u\n", e); });
  ArduinoOTA.begin();
  Serial.printf("[ota] ready as %s.local @ %s\n", Net::hostname().c_str(), WiFi.localIP().toString().c_str());
}

// Re-advertise after the WiFi supervisor has re-associated. The mDNS responder and the espota
// listener are bound to the old netif and don't come back on their own, so a self-healed device
// would be reachable by IP but invisible to `.local` discovery — which is exactly how the /ota
// skill finds it (find_device.py). Half-recovered is not recovered.
void otaRestart() {
  if (WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.end();
  otaBegin();
}

void otaHandle()   { ArduinoOTA.handle(); }
bool otaActive()   { return s_active; }
int  otaProgress() { return s_progress; }
