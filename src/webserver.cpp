#include "webserver.h"

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <math.h>

#include "rivian_api.h"
#include "settings.h"

// --- Link health (drives the status page and, later, the link-health LED) ------------------
enum { LINK_OK = 0, LINK_REAUTH = 1, LINK_OFFLINE = 2 };

// --- Shared state ---------------------------------------------------------------------------
static WebServer        s_server(80);
static SemaphoreHandle_t s_apiLock;      // serialize rivian_api network calls (shared TLS client)
static SemaphoreHandle_t s_stateLock;    // guard the snapshot below
static volatile bool    s_loginActive = false;  // pause polling during an interactive web login

struct Snap {
  VehicleStatus vs;
  int      link      = LINK_REAUTH;
  uint32_t lastMs    = 0;
  bool     everOk    = false;
};
static Snap s_snap;

static void setLink(int link) {
  xSemaphoreTake(s_stateLock, portMAX_DELAY);
  s_snap.link = link;
  xSemaphoreGive(s_stateLock);
}
static Snap snapshot() {
  xSemaphoreTake(s_stateLock, portMAX_DELAY);
  Snap c = s_snap;
  xSemaphoreGive(s_stateLock);
  return c;
}

// Public accessor for the leds module (plan §7).
LedState ledState() {
  Snap s = snapshot();
  LedState o;
  o.link   = s.link;
  o.everOk = s.everOk;
  o.ageMs  = s.lastMs ? (millis() - s.lastMs) : 0;
  o.vs     = s.vs;
  return o;
}

// Convenience: take/give the API lock around a rivian_api call.
#define API_LOCK()   xSemaphoreTake(s_apiLock, portMAX_DELAY)
#define API_UNLOCK() xSemaphoreGive(s_apiLock)

// --- Poll task ------------------------------------------------------------------------------
// Bootstraps from the persisted u-sess (no OTP): ensures VIN, then polls every 30 s with
// exponential backoff. If the session is missing/dead, idles as REAUTH so the web /login flow
// can take over. Paused while s_loginActive (so a two-step MFA login isn't clobbered).
static void pollTask(void*) {
  uint32_t err = 0;
  for (;;) {
    if (s_loginActive) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }

    if (!RivianApi::hasSession()) { setLink(LINK_REAUTH); vTaskDelay(pdMS_TO_TICKS(2000)); continue; }

    // Ensure we know the vehicle id (needed to key getVehicleState).
    if (RivianApi::vehicleId().isEmpty()) {
      API_LOCK();
      bool ok = RivianApi::createCsrf() && RivianApi::fetchVin();
      API_UNLOCK();
      if (!ok) { setLink(LINK_OFFLINE); vTaskDelay(pdMS_TO_TICKS(5000)); continue; }
    }

    VehicleStatus vs;
    API_LOCK();
    bool ok = RivianApi::pollState(vs);
    API_UNLOCK();

    if (ok) {
      err = 0;
      xSemaphoreTake(s_stateLock, portMAX_DELAY);
      s_snap.vs = vs; s_snap.link = LINK_OK; s_snap.lastMs = millis(); s_snap.everOk = true;
      xSemaphoreGive(s_stateLock);
      vTaskDelay(pdMS_TO_TICKS(30000));
    } else {
      err++;
      // Reactive re-CSRF (plan §4). If even CSRF fails, the API/network is down (OFFLINE);
      // if CSRF works but reads keep failing, the u-sess is likely dead (REAUTH).
      API_LOCK();
      bool re = RivianApi::createCsrf();
      API_UNLOCK();
      setLink(!re ? LINK_OFFLINE : (err >= 2 ? LINK_REAUTH : LINK_OK));
      uint32_t backoff = 30u << (err > 5 ? 5 : err);
      if (backoff > 900) backoff = 900;
      vTaskDelay(pdMS_TO_TICKS(backoff * 1000));
    }
  }
}

// --- HTML helpers ---------------------------------------------------------------------------
static const char* CSS =
  "body{font-family:system-ui,sans-serif;max-width:34rem;margin:1.5rem auto;padding:0 1rem;"
  "background:#111;color:#eee}a{color:#6cf}h1{font-size:1.2rem}.card{background:#1c1c1c;"
  "border-radius:10px;padding:1rem;margin:.8rem 0}.row{display:flex;justify-content:space-between;"
  "padding:.25rem 0;border-bottom:1px solid #2a2a2a}.row:last-child{border:0}.k{color:#9a9a9a}"
  "input{padding:.5rem;border-radius:6px;border:1px solid #444;background:#222;color:#eee;width:100%}"
  "button{padding:.6rem 1rem;border:0;border-radius:6px;background:#3a7;color:#022;font-weight:600;"
  "cursor:pointer;margin-top:.6rem}.pill{padding:.15rem .5rem;border-radius:999px;font-size:.85rem}"
  ".ok{background:#153}.warn{background:#530}.err{background:#500}nav a{margin-right:1rem}"
  ".meter{display:flex;gap:4px;margin:.3rem 0}.seg{flex:1;height:20px;border-radius:4px}"
  ".seg.o{background:#2a2a2a}.seg.g{background:#2ecc40}.seg.r{background:#ff4136}"
  ".pulse{animation:pl 1s ease-in-out infinite}.flash{animation:fl .6s steps(1,end) infinite}"
  "@keyframes pl{0%,100%{opacity:.3}50%{opacity:1}}@keyframes fl{0%,50%{opacity:1}50.01%,100%{opacity:.15}}"
  "output{color:#9a9a9a;margin-left:.5rem}";

static String pageHead(const String& title, bool autorefresh) {
  String h = "<!doctype html><html><head><meta charset=utf-8>"
             "<meta name=viewport content='width=device-width,initial-scale=1'>";
  if (autorefresh) h += "<meta http-equiv=refresh content=15>";
  h += "<title>" + title + "</title><style>" + CSS + "</style></head><body>";
  h += "<nav><a href=/>Status</a><a href=/config>Config</a><a href=/login>Login</a></nav>";
  return h;
}
static String pageFoot() { return "</body></html>"; }

static String row(const String& k, const String& v) {
  return "<div class=row><span class=k>" + k + "</span><span>" + v + "</span></div>";
}

// An 8-segment preview of what the LED strip is showing (mirrors leds.cpp render()).
static String meterHtml(const Snap& s, int threshMiles) {
  const int N = 8;
  String seg[N];
  for (int i = 0; i < N; i++) seg[i] = "o";

  if (s.link != LINK_OK || !s.everOk) {
    seg[0] = "r pulse";                                  // link down: pixel 0 pulses red
  } else {
    const VehicleStatus& v = s.vs;
    float target = (isnan(v.batteryLimit) || v.batteryLimit <= 1.0f) ? 100.0f : v.batteryLimit;
    float soc    = isnan(v.batteryLevel) ? 0.0f : v.batteryLevel;
    float f      = constrain(soc / target, 0.0f, 1.0f);
    int   n      = (int)lroundf(f * N);
    if (soc < target - 0.5f && n >= N) n = N - 1;
    float mi     = isnan(v.distanceToEmpty) ? NAN : v.distanceToEmpty / 1.60934f;

    if (v.chargerState == "charging_active") {           // charging: meter + closest-empty green pulse
      for (int i = 0; i < N; i++) seg[i] = (i < n) ? "g" : "r";
      if (n < N) seg[n] = "g pulse";
    } else if (!isnan(mi) && mi < threshMiles) {         // low range: all flash red
      for (int i = 0; i < N; i++) seg[i] = "r flash";
    } else {                                             // meter: green filled + red empty
      for (int i = 0; i < N; i++) seg[i] = (i < n) ? "g" : "r";
    }
  }

  String h = "<div class=meter>";
  for (int i = 0; i < N; i++) h += "<div class='seg " + seg[i] + "'></div>";
  return h + "</div>";
}

// --- Handlers -------------------------------------------------------------------------------
static void handleStatus() {
  s_loginActive = false;                   // navigating to status ends any abandoned login flow
  Snap s = snapshot();
  const int thresh = Settings::rangeThresholdMiles();

  String linkPill;
  if (s.link == LINK_OK)         linkPill = "<span class='pill ok'>online</span>";
  else if (s.link == LINK_REAUTH)linkPill = "<span class='pill warn'>re-auth needed</span>";
  else                           linkPill = "<span class='pill err'>offline</span>";

  String body = pageHead(Settings::deviceName(), true);
  body += "<h1>" + Settings::deviceName() + " " + linkPill + "</h1>";

  if (!s.everOk) {
    body += "<div class=card>No data yet. If this says <b>re-auth needed</b>, "
            "<a href=/login>log in</a>.</div>";
  } else {
    const VehicleStatus& v = s.vs;
    float mi = isnan(v.distanceToEmpty) ? NAN : v.distanceToEmpty / 1.60934f;
    bool low = !isnan(mi) && mi < thresh;
    String age = String((millis() - s.lastMs) / 1000) + "s ago";

    body += "<div class=card>";
    body += row("Charge", String(v.batteryLevel, 1) + " %");
    body += row("Charge target", String(v.batteryLimit, 0) + " %");
    body += row("Range", (isnan(mi) ? String("?") : String(mi, 0)) + " mi"
                + (low ? " <span class='pill err'>LOW (&lt;" + String(thresh) + ")</span>" : ""));
    body += row("Charger", v.chargerState);
    body += row("Charge port", v.chargePortState);
    if (v.timeToEndOfCharge >= 0)
      body += row("Time to target", String(v.timeToEndOfCharge) + " min");
    body += row("Updated", age);
    body += "</div>";
  }
  // LED strip preview — the same 8-segment meter the physical strip shows, right below the table.
  body += "<div class=card><div class=k>LED strip</div>" + meterHtml(s, thresh) + "</div>";
  body += pageFoot();
  s_server.send(200, "text/html", body);
}

static String loginForm(const String& msg) {
  String b = pageHead("Login", false);
  b += "<div class=card><h1>Rivian login</h1>";
  if (msg.length()) b += "<p class='pill warn'>" + msg + "</p>";
  b += "<form method=POST action=/login>"
       "<p class=k>Email</p><input name=email type=email autocomplete=username>"
       "<p class=k>Password</p><input name=password type=password autocomplete=current-password>"
       "<button type=submit>Log in</button></form>"
       "<p class=k>Password is used once and never stored; only the session token is kept.</p>";
  b += "</div>" + pageFoot();
  return b;
}
static String otpForm(const String& email, const String& msg) {
  String b = pageHead("Verify", false);
  b += "<div class=card><h1>Enter the code</h1>";
  if (msg.length()) b += "<p class='pill warn'>" + msg + "</p>";
  b += "<p class=k>Rivian emailed a one-time code to " + email + ".</p>"
       "<form method=POST action=/login/otp>"
       "<input type=hidden name=email value='" + email + "'>"
       "<p class=k>Code</p><input name=otp inputmode=numeric autocomplete=one-time-code>"
       "<button type=submit>Verify</button></form>";
  b += "</div>" + pageFoot();
  return b;
}

static void handleLoginGet() { s_server.send(200, "text/html", loginForm("")); }

static void handleLoginPost() {
  String email = s_server.arg("email");
  String pw    = s_server.arg("password");
  if (email.isEmpty() || pw.isEmpty()) { s_server.send(200, "text/html", loginForm("Enter email and password.")); return; }

  s_loginActive = true;                    // pause the poll task across the (possibly 2-step) login
  API_LOCK();
  bool csrf = RivianApi::createCsrf();
  RivianApi::LoginResult r = csrf ? RivianApi::login(email, pw) : RivianApi::LOGIN_ERROR;
  API_UNLOCK();

  if (r == RivianApi::LOGIN_OK) {
    API_LOCK(); RivianApi::fetchVin(); API_UNLOCK();
    s_loginActive = false;
    s_server.sendHeader("Location", "/"); s_server.send(303);
  } else if (r == RivianApi::LOGIN_MFA_REQUIRED) {
    // keep s_loginActive = true so the poll task doesn't re-CSRF between here and /login/otp
    s_server.send(200, "text/html", otpForm(email, ""));
  } else {
    s_loginActive = false;
    s_server.send(200, "text/html", loginForm("Login failed: " + RivianApi::lastError()));
  }
}

static void handleOtpPost() {
  String email = s_server.arg("email");
  String otp   = s_server.arg("otp");
  API_LOCK();
  bool ok = RivianApi::completeOtp(email, otp);
  if (ok) RivianApi::fetchVin();
  API_UNLOCK();
  s_loginActive = false;
  if (ok) { s_server.sendHeader("Location", "/"); s_server.send(303); }
  else    { s_server.send(200, "text/html", otpForm(email, "Code rejected: " + RivianApi::lastError())); }
}

static void handleConfigGet(const String& msg = "") {
  String b = pageHead("Config", false);
  b += "<div class=card><h1>Config</h1>";
  if (msg.length()) b += "<p class='pill ok'>" + msg + "</p>";
  b += "<form method=POST action=/config>"
       "<p class=k>Low-range alert threshold (miles)</p>"
       "<input name=threshold type=number min=1 max=500 value='" + String(Settings::rangeThresholdMiles()) + "'>"
       "<p class=k>LED brightness</p>"
       "<input name=brightness type=range min=1 max=255 value='" + String(Settings::ledBrightness()) + "'"
       " oninput='bo.value=this.value'><output id=bo>" + String(Settings::ledBrightness()) + "</output>"
       "<p class=k>Device name</p>"
       "<input name=name maxlength=32 value='" + Settings::deviceName() + "'>"
       "<button type=submit>Save</button></form>";
  b += "</div>" + pageFoot();
  s_server.send(200, "text/html", b);
}
static void handleConfigPost() {
  if (s_server.hasArg("threshold")) Settings::setRangeThresholdMiles(s_server.arg("threshold").toInt());
  if (s_server.hasArg("brightness")) Settings::setLedBrightness(s_server.arg("brightness").toInt());

  // A device-name change means a new DHCP hostname + mDNS + AP name. Those only re-register on a
  // fresh STA association, so — like sonos-nest — save and reboot so all three come up together.
  bool renamed = false;
  if (s_server.hasArg("name")) {
    String want = Settings::sanitizeHostname(s_server.arg("name"));
    if (want.length() && want != Settings::deviceName()) { Settings::setDeviceName(s_server.arg("name")); renamed = true; }
  }

  if (renamed) {
    String n = Settings::deviceName();
    String b = pageHead("Renamed", false);
    b += "<div class=card><h1>Renamed to " + n + "</h1><p>Rebooting so the network name updates. "
         "Reconnect in a few seconds at <a href='http://" + n + ".local/'>http://" + n + ".local/</a>.</p></div>"
         + pageFoot();
    s_server.send(200, "text/html", b);
    delay(1200);
    ESP.restart();
  } else {
    handleConfigGet("Saved.");
  }
}

// --- Lifecycle ------------------------------------------------------------------------------
void webAppBegin() {
  s_apiLock   = xSemaphoreCreateMutex();
  s_stateLock = xSemaphoreCreateMutex();

  if (MDNS.begin(Settings::deviceName().c_str())) MDNS.addService("http", "tcp", 80);

  s_server.on("/",           HTTP_GET,  handleStatus);
  s_server.on("/login",      HTTP_GET,  handleLoginGet);
  s_server.on("/login",      HTTP_POST, handleLoginPost);
  s_server.on("/login/otp",  HTTP_POST, handleOtpPost);
  s_server.on("/config",     HTTP_GET,  [](){ handleConfigGet(); });
  s_server.on("/config",     HTTP_POST, handleConfigPost);
  s_server.onNotFound([](){ s_server.sendHeader("Location", "/"); s_server.send(303); });
  s_server.begin();

  // TLS needs a healthy stack; the poll task does the HTTPS work.
  xTaskCreatePinnedToCore(pollTask, "poll", 12288, nullptr, 1, nullptr, 1);
}

void webAppLoop() { s_server.handleClient(); }
