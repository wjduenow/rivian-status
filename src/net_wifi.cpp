#include "net_wifi.h"
#include "settings.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "rivian-status"
#endif

static volatile int s_applyResult = Net::APPLY_IDLE;

String Net::hostname() {
  String h = Settings::deviceName();
  return h.length() ? h : String(DEVICE_HOSTNAME);
}

static bool waitConnected(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(200);
  return WiFi.status() == WL_CONNECTED;
}

// Pick the best creds: NVS (set on-device) first, then secrets.h dev fallback.
static bool beginFromStored() {
  String ss = Settings::wifiSsid(), pw = Settings::wifiPass();
  if (ss.isEmpty()) {
#if defined(WIFI_SSID) && defined(WIFI_PASS)
    ss = WIFI_SSID; pw = WIFI_PASS;
#endif
  }
  if (ss.isEmpty()) return false;
  WiFi.begin(ss.c_str(), pw.c_str());
  return true;
}

bool Net::connect(uint32_t timeoutMs) {
  // setHostname BEFORE the STA transition — otherwise the value is ignored and the router shows
  // the default esp32s3-XXXXXX (sonos-nest wifi.cpp note).
  WiFi.persistent(false);
  WiFi.setHostname(hostname().c_str());
  WiFi.mode(WIFI_STA);
  if (!beginFromStored()) return false;
  return waitConnected(timeoutMs);
}

bool      Net::isConnected() { return WiFi.status() == WL_CONNECTED; }
String    Net::ssid()        { return WiFi.SSID(); }
IPAddress Net::ip()          { return WiFi.localIP(); }

int  Net::applyResult()      { return s_applyResult; }
void Net::applyResultReset() { s_applyResult = APPLY_IDLE; }

// Try new creds; persist on success, revert to previous on failure (never strand the device).
void Net::apply(const String& ssid, const String& pass) {
  WiFi.disconnect();
  delay(100);
  WiFi.setHostname(hostname().c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());
  if (waitConnected(12000)) {
    Settings::setWifi(ssid, pass);
    s_applyResult = APPLY_OK;
    return;
  }
  WiFi.disconnect();
  delay(100);
  beginFromStored();
  waitConnected(8000);
  s_applyResult = APPLY_FAIL;
}

// ---------------------------------------------------------------------------
// SoftAP captive portal (adapted from sonos-nest portal.cpp).
// ---------------------------------------------------------------------------
static const uint16_t HTTP_PORT = 80;    // MUST be 80 — phones probe http://<gateway>/ there
static const uint16_t DNS_PORT  = 53;
static WebServer* s_pServer = nullptr;
static DNSServer* s_pDns    = nullptr;
static IPAddress  s_apIp;

static String esc(const String& in) {
  String o; o.reserve(in.length() + 8);
  for (char c : in) {
    switch (c) {
      case '&': o += "&amp;";  break;
      case '<': o += "&lt;";   break;
      case '>': o += "&gt;";   break;
      case '"': o += "&quot;"; break;
      default:  o += c;
    }
  }
  return o;
}

static const char kHead[] =
  "<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<title>rivian-status setup</title><style>:root{color-scheme:light dark}"
  "body{font:16px/1.5 system-ui,sans-serif;margin:0;padding:24px;max-width:26rem;margin-inline:auto}"
  "h1{font-size:1.25rem;margin:0 0 1.25rem}form{border:1px solid #8883;border-radius:12px;padding:16px}"
  "label{display:block;font-weight:600;margin:0 0 1rem}select,input{font:inherit;width:100%;padding:.5rem;"
  "border-radius:8px;box-sizing:border-box;margin-top:.3rem}button{font:inherit;padding:.6rem 1.2rem;"
  "border-radius:8px;border:1px solid #2a7;background:#2a7;color:#fff;cursor:pointer}"
  ".hint{font-size:.85rem;opacity:.75;margin-top:1rem}</style>";

static String pageIndex() {
  int n = WiFi.scanNetworks();
  String opts;
  for (int i = 0; i < n; ++i) {
    const String ss = WiFi.SSID(i);
    if (!ss.length()) continue;
    opts += "<option value=\"" + esc(ss) + "\">" + esc(ss);
    opts += WiFi.RSSI(i) >= -67 ? "</option>" : " &middot;</option>";
  }
  WiFi.scanDelete();
  if (opts.isEmpty()) opts = "<option value=\"\">(no networks — refresh)</option>";

  String p = kHead;
  p += "<h1>rivian-status &mdash; setup</h1><form method=POST action=/join>";
  p += "<label>Wi-Fi network<select name=ssid>" + opts + "</select></label>";
  p += "<label>Password<input type=password name=pass autocapitalize=off autocorrect=off spellcheck=false></label>";
  p += "<label>Device name<input name=devname value=\"" + esc(Net::hostname()) +
       "\" maxlength=32></label>";
  p += "<button>Join</button></form>";
  p += "<p class=hint>Pick your Wi-Fi and password. The device joins it, this setup network "
       "disappears, and it reboots as the name above (reach it at that name .local).</p>";
  return p;
}

static String pageResult(const String& title, const String& body, bool retry) {
  String p = kHead;
  p += "<h1>" + title + "</h1><p>" + body + "</p>";
  if (retry) p += "<p class=hint><a href=\"/\">&larr; back</a></p>";
  return p;
}

static void handleIndex() { s_pServer->send(200, "text/html", pageIndex()); }

static void handleJoin() {
  const String ssid = s_pServer->arg("ssid");
  const String pass = s_pServer->arg("pass");
  const String name = s_pServer->arg("devname");
  if (ssid.isEmpty()) {
    s_pServer->send(200, "text/html", pageResult("Pick a network", "No Wi-Fi network was selected.", true));
    return;
  }
  if (name.length()) Settings::setDeviceName(name);   // sanitized inside; applied on reboot

  Serial.printf("[portal] joining \"%s\"...\n", ssid.c_str());
  Net::applyResultReset();
  Net::apply(ssid, pass);                             // AP stays up (disconnect drops STA only)
  if (Net::applyResult() == Net::APPLY_OK) {
    Serial.printf("[portal] joined as %s — rebooting into %s\n",
                  Net::ip().toString().c_str(), Net::hostname().c_str());
    s_pServer->send(200, "text/html",
      pageResult("Connected",
                 "Joined <b>" + esc(ssid) + "</b>. Rebooting as <b>" + esc(Net::hostname()) +
                 "</b> — reconnect to your Wi-Fi and open http://" + esc(Net::hostname()) + ".local/",
                 false));
    delay(1200);
    ESP.restart();                                    // clean boot: hostname/mDNS come up correct
  } else {
    Serial.printf("[portal] join failed for \"%s\"\n", ssid.c_str());
    s_pServer->send(200, "text/html",
      pageResult("Couldn't join", "Couldn't connect to <b>" + esc(ssid) + "</b> — check the password.", true));
  }
}

static void handleCaptive() {
  s_pServer->sendHeader("Location", "http://" + s_apIp.toString() + "/", true);
  s_pServer->send(302, "text/plain", "");
}

void Net::runPortal() {
  String apSsid = hostname() + "-setup";
  Serial.printf("[portal] no usable WiFi — raising setup AP \"%s\"\n", apSsid.c_str());

  WiFi.mode(WIFI_AP_STA);                 // AP_STA so we can scan real networks while serving
  WiFi.softAP(apSsid.c_str());            // open network
  delay(100);
  s_apIp = WiFi.softAPIP();               // 192.168.4.1
  Serial.printf("[portal] join \"%s\", then open http://%s/\n", apSsid.c_str(), s_apIp.toString().c_str());

  s_pDns = new DNSServer();
  s_pDns->start(DNS_PORT, "*", s_apIp);   // wildcard → every hostname resolves to us

  s_pServer = new WebServer(HTTP_PORT);
  s_pServer->on("/",     HTTP_GET,  handleIndex);
  s_pServer->on("/join", HTTP_POST, handleJoin);
  s_pServer->onNotFound(handleCaptive);
  s_pServer->begin();

  while (!isConnected()) {                 // blocks until joined (handleJoin reboots on success)
    s_pDns->processNextRequest();
    s_pServer->handleClient();
    delay(2);
  }

  s_pServer->stop(); delete s_pServer; s_pServer = nullptr;
  s_pDns->stop();    delete s_pDns;    s_pDns    = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}
