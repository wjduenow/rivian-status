#include "webserver.h"

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <math.h>

#include "rivian_api.h"
#include "settings.h"
#include "net_wifi.h"
#include "net_ota.h"

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

// --- WiFi supervisor --------------------------------------------------------------------------
// Re-register the name-based services after a reconnect. Both the mDNS responder and the espota
// listener are bound to the netif that just died and do not come back on their own, so without
// this a self-healed device is reachable by IP but invisible to `.local` — which is how the /ota
// skill finds it (find_device.py). Half-recovered is not recovered.
static void readvertise() {
  MDNS.end();
  if (MDNS.begin(Settings::deviceName().c_str())) MDNS.addService("http", "tcp", 80);
  otaRestart();
}

// Sleep for `ms` while supervising the link, rather than going deaf inside one long vTaskDelay.
// That matters here in a way it doesn't in sonos-nest (which polls every 3 s with no backoff):
// this task's backoff reaches a 900 s cap, so a single blind sleep could leave the light stale
// and red for a quarter of an hour after the network came back.
//
// Returns true if the link was restored during the wait — the caller retries immediately and
// clears its error count, since those failures were the outage rather than a sick session.
static bool s_linkWasDown = false;      // file-static so recovery is caught across calls too
static bool sleepSupervised(uint32_t ms) {
  const uint32_t SLICE = 500, KICK_MS = 10000;
  static uint32_t s_lastKick = 0;

  for (uint32_t waited = 0; waited < ms; waited += SLICE) {
    if (!Net::isConnected()) {
      s_linkWasDown = true;
      const uint32_t now = millis();
      if (now - s_lastKick > KICK_MS) {     // backed off so we don't spin on the radio
        s_lastKick = now;
        Serial.println("[net] wifi down — reconnecting");
        Net::reconnect();
      }
    } else if (s_linkWasDown) {
      s_linkWasDown = false;
      Serial.printf("[net] wifi back — ip=%s (kick #%u)\n",
                    Net::ip().toString().c_str(), (unsigned)Net::reconnectCount());
      readvertise();
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(SLICE));
  }
  return false;
}

// --- Poll task ------------------------------------------------------------------------------
// Bootstraps from the persisted u-sess (no OTP): ensures VIN, then polls every 30 s with
// exponential backoff. If the session is missing/dead, idles as REAUTH so the web /login flow
// can take over. Paused while s_loginActive (so a two-step MFA login isn't clobbered).
static void pollTask(void*) {
  uint32_t err = 0;
  for (;;) {
    if (s_loginActive) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }

    // No link: don't spend 15 s TLS timeouts against a dead netif. Show OFFLINE and let the
    // supervisor work — this is the path that keeps the box from wedging until a power cycle.
    if (!Net::isConnected()) {
      setLink(LINK_OFFLINE);
      if (sleepSupervised(1000)) err = 0;
      continue;
    }

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
      sleepSupervised(30000);
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
      if (sleepSupervised(backoff * 1000)) err = 0;   // link came back: retry now, not in 15 min
    }
  }
}

// --- HTML helpers ---------------------------------------------------------------------------
static const char* CSS =
  "body{font-family:system-ui,sans-serif;max-width:34rem;margin:1.5rem auto;padding:0 1rem;"
  "background:#111;color:#eee}a{color:#6cf}h1{font-size:1.2rem}.card{background:#1c1c1c;"
  "border-radius:10px;padding:1rem;margin:.8rem 0}.row{display:flex;justify-content:space-between;"
  "padding:.25rem 0;border-bottom:1px solid #2a2a2a}.row:last-child{border:0}.k{color:#9a9a9a}"
  "input,select{padding:.5rem;border-radius:6px;border:1px solid #444;background:#222;color:#eee;"
  "width:100%}"
  "button{padding:.6rem 1rem;border:0;border-radius:6px;background:#3a7;color:#022;font-weight:600;"
  "cursor:pointer;margin-top:.6rem}.pill{padding:.15rem .5rem;border-radius:999px;font-size:.85rem}"
  ".ok{background:#153}.warn{background:#530}.err{background:#500}nav a{margin-right:1rem}"
  ".meter{display:flex;gap:4px;margin:.3rem 0;height:20px}"
  ".meter.col{flex-direction:column-reverse;height:168px;width:26px}"
  ".seg{flex:1;border-radius:4px}"
  ".orient{display:flex;gap:.4rem;margin:.3rem 0}.orient label{flex:1;text-align:center;"
  "border:1px solid #444;border-radius:8px;padding:.4rem .2rem;cursor:pointer;font-size:.78rem;"
  "color:#9a9a9a}.orient label:has(input:checked){border-color:#3a7;background:#152e22;color:#eee}"
  ".orient input{width:auto;margin:.2rem 0 0;accent-color:#3a7}"
  ".seg.o{background:#2a2a2a}.seg.g{background:#2ecc40}.seg.r{background:#ff4136}.seg.w{background:#e8e8e8}"
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
// FW_VERSION comes from tools/git_version.py (`git describe --tags --always --dirty`). Only a
// clean CI tag build reports a bare "v1.2.3"; anything flashed from a laptop carries a -dirty or
// -N-g<hash> suffix. Fallback keeps the page compiling if the pre-script ever doesn't run.
#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif

static String pageFoot() {
  return "<p class=k style='font-size:.8rem;text-align:center'>fw " FW_VERSION "</p>"
         "</body></html>";
}

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
      for (int i = 0; i < N; i++) seg[i] = (i < n) ? "g" : "w";
      if (n < N) seg[n] = "g pulse";
    } else if (!isnan(mi) && mi < threshMiles) {         // low range: all flash red
      for (int i = 0; i < N; i++) seg[i] = "r flash";
    } else {                                             // meter: green filled + white empty
      for (int i = 0; i < N; i++) seg[i] = (i < n) ? "g" : "w";
    }
  }

  // The firmware compensates for the mounting, so the meter always *reads* the same way — fills
  // up when the stick lands vertical, right when it lands horizontal. The preview therefore only
  // switches axis; it never reverses. (column-reverse puts logical pixel 0 at the bottom.)
  String h = String("<div class='meter") + (Settings::ledVertical() ? " col" : "") + "'>";
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

  // Health. The point of these four numbers is diagnosing the long-uptime failures this firmware
  // now self-heals: a climbing reconnect count says the link really is flapping, and an uptime
  // that keeps resetting says something is rebooting the box instead.
  const uint32_t up = millis() / 1000;
  String uptime = up < 3600 ? String(up / 60) + "m"
                : up < 86400 ? String(up / 3600) + "h " + String((up % 3600) / 60) + "m"
                             : String(up / 86400) + "d " + String((up % 86400) / 3600) + "h";
  body += "<div class=card><div class=k>Health</div>";
  body += row("Uptime", uptime);
  body += row("WiFi", Net::ssid() + " &middot; " + String(WiFi.RSSI()) + " dBm");
  body += row("Reconnects", String((unsigned)Net::reconnectCount()));
  body += row("Free heap", String(ESP.getFreeHeap() / 1024) + " KB");
  body += "</div>";

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

// One little diagram per mounting: the case outline with the USB-C / power lead on the edge you
// can see it on, and the 8 meter cells shaded the way they'll fill once the firmware compensates.
// Note the shading is always "full toward the top / the right" — that invariant IS the feature.
static String orientSvg(int rot) {
  // Which axis the stick lands on depends on the enclosure as well as the rotation: a quarter
  // turn swaps it, a half turn doesn't. (v1 box = horizontal at plug-down, v2 wall case = vertical.)
  const bool vert = Settings::ledStickVertical0() != (rot == 90 || rot == 270);
  const int  W = vert ? 26 : 54, H = vert ? 54 : 26;
  const int  x0 = (64 - W) / 2, y0 = (64 - H) / 2;
  const int  FILLED = 5;                             // representative 5-of-8 fill, just for shape

  String s = "<svg viewBox='0 0 64 64' width=52 height=52>";
  // Power lead, straddling whichever edge it exits.
  int px = 32 - 5, py = 32 - 5, pw = 10, ph = 5;
  if      (rot == 0)   { py = y0 + H - 1; }
  else if (rot == 180) { py = y0 - 4;     }
  else if (rot == 90)  { px = x0 - 4;      pw = 5; ph = 10; }
  else                 { px = x0 + W - 1;  pw = 5; ph = 10; }
  s += "<rect x=" + String(px) + " y=" + String(py) + " width=" + String(pw) + " height=" + String(ph) +
       " rx=1.5 fill='#8a8a8a'/>";
  // Case body.
  s += "<rect x=" + String(x0) + " y=" + String(y0) + " width=" + String(W) + " height=" + String(H) +
       " rx=4 fill='#222' stroke='#555'/>";
  // The 8 cells, laid out along the long axis. Logical index 0 is the "fill from" end: the
  // bottom when vertical, the left when horizontal.
  for (int i = 0; i < 8; i++) {
    const char* c = (i < FILLED) ? "#2ecc40" : "#3f3f3f";
    if (vert) {
      int ch = (H - 10) / 8, cx = x0 + 5, cw = W - 10;
      int cy = y0 + H - 5 - (i + 1) * ch;            // index 0 at the bottom -> fills upward
      s += "<rect x=" + String(cx) + " y=" + String(cy + 1) + " width=" + String(cw) +
           " height=" + String(ch - 2) + " rx=1 fill='" + c + "'/>";
    } else {
      int cw = (W - 10) / 8, cy = y0 + 5, ch = H - 10;
      int cx = x0 + 5 + i * cw;                      // index 0 at the left -> fills rightward
      s += "<rect x=" + String(cx + 1) + " y=" + String(cy) + " width=" + String(cw - 2) +
           " height=" + String(ch) + " rx=1 fill='" + c + "'/>";
    }
  }
  return s + "</svg>";
}

static String orientCard(int rot, const char* caption, int current) {
  return "<label>" + orientSvg(rot) + "<br>" + caption +
         "<br><input type=radio name=orientation value=" + String(rot) +
         (rot == current ? " checked" : "") + "></label>";
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
       " oninput='bo.value=this.value'><output id=bo>" + String(Settings::ledBrightness()) + "</output>";
  // Mounting orientation. Asked as "where's the plug?" because that's the thing you can see
  // without thinking about LED wiring — the outlet decides how the device ends up hanging.
  const int rot = Settings::ledRotation();
  const bool v0 = Settings::ledStickVertical0();
  b += "<p class=k>Enclosure</p>"
       "<select name=enclosure>"
       "<option value=0" + String(v0 ? "" : " selected") + ">Box (v1) — LED bar runs across</option>"
       "<option value=1" + String(v0 ? " selected" : "") + ">Wall case (v2) — LED bar runs up-down</option>"
       "</select>"
       "<p class=k style='font-size:.8rem'>Set once per unit: which way the LED stick sits in the "
       "case, with the plug at the bottom. It decides whether a quarter turn makes the meter "
       "vertical or horizontal.</p>";
  b += "<p class=k>Mounting — where does the plug come out?</p><div class=orient>";
  b += orientCard(0,   "Bottom", rot);
  b += orientCard(90,  "Left",   rot);
  b += orientCard(180, "Top",    rot);
  b += orientCard(270, "Right",  rot);
  b += "</div><p class=k style='font-size:.8rem'>The meter always fills toward the top (when it "
       "lands vertical) or toward the right (when it lands sideways) — pick the picture that "
       "matches your device and the firmware handles the rest.</p>";
  // Which end of the stick pixel 0 is on isn't knowable from the wiring, so this is the escape
  // hatch: if the meter drains the wrong way, one glance and one click settles it.
  b += "<p class=k><label style='color:#9a9a9a'><input type=checkbox name=invert value=1"
       + String(Settings::ledInverted() ? " checked" : "") +
       " style='width:auto;margin-right:.4rem;accent-color:#3a7'>Meter fills the wrong way "
       "— reverse it</label></p>";
  b += "<p class=k>Device name</p>"
       "<input name=name maxlength=32 value='" + Settings::deviceName() + "'>"
       "<button type=submit>Save</button></form>";
  b += "</div>" + pageFoot();
  s_server.send(200, "text/html", b);
}
static void handleConfigPost() {
  if (s_server.hasArg("threshold")) Settings::setRangeThresholdMiles(s_server.arg("threshold").toInt());
  if (s_server.hasArg("brightness")) Settings::setLedBrightness(s_server.arg("brightness").toInt());
  // Takes effect on the next LED frame (~20 ms) — no reboot, same as brightness. The radios always
  // post one value, so their presence marks a real submit — which is what lets the unchecked
  // "invert" checkbox (which posts nothing at all) be read as false rather than "absent".
  if (s_server.hasArg("orientation")) {
    Settings::setLedRotation(s_server.arg("orientation").toInt());
    Settings::setLedStickVertical0(s_server.arg("enclosure") == "1");
    Settings::setLedInverted(s_server.hasArg("invert"));
  }

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
