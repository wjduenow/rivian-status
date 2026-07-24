// See net_updater.h. HTTP pull-OTA against the plan-02 manifest schema.
#include "net_updater.h"
#include "settings.h"
#include "net_ota.h"        // otaActive() — never pull-flash while an espota push is running

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

// Injected per build by tools/git_version.py (git describe --tags --always --dirty). Only a clean
// CI tag build reports a bare "v0.1.0"; laptop builds carry -N-g<hash>[-dirty], which parseVer()
// deliberately sorts NEWER than the tag so the updater never downgrades a dev build.
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// Default source when the setting is empty (AUTO). The `latest/download/<asset>` form is stable
// and follows every new Release, so no version is baked in. Override per fork with -D.
#ifndef UPDATE_MANIFEST_URL
#define UPDATE_MANIFEST_URL "https://github.com/wjduenow/rivian-status/releases/latest/download/manifest.json"
#endif

// This unit's key in the manifest. rivian-status ships exactly one env; the schema stays
// multi-unit so a second board is a CI matrix row rather than a rewrite.
static const char* kUnitId = "status";

static const uint32_t kCheckMs = 6UL * 60 * 60 * 1000;   // 6 h between periodic checks

static String        s_available;            // available version, "" if none/up-to-date/disabled
static String        s_lastError;
static bool          s_armed     = false;    // explicit approve -> apply on the next check
static bool          s_force     = false;    // bypass the rate limit once
static uint32_t      s_lastCheck = 0;
static bool          s_everChecked = false;
static volatile bool s_active   = false;
static volatile int  s_progress = -1;

bool   updaterActive()           { return s_active; }
int    updaterProgress()         { return s_progress; }
bool   updaterAvailable()        { return s_available.length() > 0; }
String updaterAvailableVersion() { return s_available; }
String updaterLastError()        { return s_lastError; }
void   updaterApprove()          { s_armed = true; s_force = true; }
void   updaterForceCheck()       { s_force = true; }

// Resolve the effective source. kindOut, if given, gets a static label for the UI.
static String resolveUrl(const char** kindOut) {
  String stored = Settings::updateUrl();
  if (stored == "off") { if (kindOut) *kindOut = "off";    return String(); }
  if (stored.length()) { if (kindOut) *kindOut = "custom"; return stored;   }
  if (kindOut) *kindOut = "github";
  return String(UPDATE_MANIFEST_URL);
}

String      updaterEffectiveUrl() { return resolveUrl(nullptr); }
const char* updaterSourceKind()   { const char* k = "off"; resolveUrl(&k); return k; }

// GET a small body over HTTP or HTTPS.
//
// HTTPS uses an INSECURE client — deliberately, and worth understanding: rivian_api.cpp pins
// Amazon Root CA 1 for the Rivian endpoint, so this is the one unvalidated TLS path in the
// firmware. Pinning GitHub's chain is brittle across their cert rotations, and the manifest only
// selects a version — the binary itself is still validated by the ESP32 bootloader's image check
// before anything boots from it. Same LAN/hobby threat model as plan 01 §11.
//
// Redirects MUST be followed: GitHub 302s release assets off to a signed CDN host (currently
// release-assets.githubusercontent.com — it has changed before, so never hard-code it).
//
// Templated because HTTPClient::begin() wants a concrete WiFiClient& and WiFiClientSecure derives
// from it; a template keeps one body for both.
template <typename C>
static bool doGet(HTTPClient& http, C& client, const String& url, String& body) {
  if (!http.begin(client, url)) return false;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(5000);
  http.setTimeout(8000);
  int code = http.GET();
  bool ok = (code == HTTP_CODE_OK);
  if (!ok) s_lastError = "manifest HTTP " + String(code);
  if (ok) body = http.getString();
  http.end();
  return ok;
}

static bool httpGetString(const String& url, String& body) {
  HTTPClient http;
  if (url.startsWith("https:")) {
    WiFiClientSecure sec;
    sec.setInsecure();
    return doGet(http, sec, url, body);
  }
  WiFiClient cl;
  return doGet(http, cl, url, body);
}

// Fetch the manifest and pull out this unit's entry.
static bool checkManifest(const String& base, String& version, String& url) {
  if (base.isEmpty()) return false;

  String body;
  if (!httpGetString(base, body)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body)) { s_lastError = "manifest parse failed"; return false; }

  JsonObject u = doc["units"][kUnitId];
  if (u.isNull()) { s_lastError = String("no \"") + kUnitId + "\" unit in manifest"; return false; }
  const char* urlp = u["url"];
  if (!urlp) { s_lastError = "unit has no url"; return false; }

  version = doc["version"] | "";
  url     = urlp;
  if (version.isEmpty()) { s_lastError = "manifest has no version"; return false; }
  s_lastError = "";
  return true;
}

// Parse "vX.Y.Z[-N[-gHASH[-dirty]]]" (git describe form) into [major,minor,patch,commits]. The
// commit count is the 4th field so a post-tag laptop build sorts NEWER than its tag — that is what
// stops the updater from trying to "update" a dev build back to the release it was built past.
// Returns false when the string isn't a numeric version (a bare commit hash), so the caller can
// fall back to inequality.
static bool parseVer(const String& s, long v[4]) {
  v[0] = v[1] = v[2] = v[3] = 0;
  int i = 0, n = s.length();
  if (i < n && (s[i] == 'v' || s[i] == 'V')) i++;

  int fields = 0;
  while (fields < 3 && i < n && isdigit((int)s[i])) {
    long num = 0;
    while (i < n && isdigit((int)s[i])) { num = num * 10 + (s[i] - '0'); i++; }
    v[fields++] = num;
    if (i >= n) break;
    if (s[i] == '.') { i++; continue; }
    if (s[i] == '-') break;
    // A numeric field ended on something that isn't '.' / '-' / end — so this is not a version.
    // ⚠️ This check is why the whole function exists in this shape: `git describe` emits a BARE
    // COMMIT HASH before the repo's first tag, and hashes are hex, so ~10 in 16 start with a
    // digit. Without this, "2591c5d-dirty" parsed as version 2591.0.0 and outranked every real
    // release — a device on a pre-tag build would report "up to date" forever and never update.
    // (Inherited from sonos-nest, where having tags from the start masked it.)
    return false;
  }
  // Need at least major.minor. A lone run of digits with no dot ("1234567") is a hash, not a tag.
  if (fields < 2) return false;

  if (i < n && s[i] == '-') {                 // "-N" commits past the tag
    i++;
    long num = 0; bool got = false;
    while (i < n && isdigit((int)s[i])) { num = num * 10 + (s[i] - '0'); i++; got = true; }
    if (got) v[3] = num;
  }
  return true;
}

// True iff `cand` is STRICTLY newer than `cur`. Never "different" — sonos-nest shipped a release
// specifically to kill a downgrade loop where otaAuto plus a lagging source flip-flopped a device
// between two builds forever. Falls back to inequality only when a side isn't a parseable version
// (a bare-hash dev build), so such a device can still take a real release.
static bool isNewer(const String& cand, const String& cur) {
  long a[4], b[4];
  if (!parseVer(cand, a) || !parseVer(cur, b)) return cand != cur;
  for (int k = 0; k < 4; k++) if (a[k] != b[k]) return a[k] > b[k];
  return false;
}

// Download + flash. Blocks the calling task; on success the device reboots into the new slot and
// this never returns. HTTPUpdate writes the INACTIVE OTA slot and only flips the boot pointer on a
// complete, valid image, so losing power or WiFi mid-download leaves the running firmware intact.
static void applyNow(const String& url) {
  s_armed = false;
  Serial.printf("[updater] applying %s\n           from %s\n", s_available.c_str(), url.c_str());

  // Quiesce before any flash write. Flash writes disable the instruction cache, and this is the
  // window sonos-nest's hardware pass found resets in. Set the flag, then give loop()
  // (ledsLoop/webAppLoop/otaHandle, ~2 ms cadence) a beat to observe it and back off.
  s_active   = true;
  s_progress = 0;
  vTaskDelay(pdMS_TO_TICKS(400));

  // HTTPUpdate's read/write loop NEVER yields. Unfixed, this task starves IDLE on its core for the
  // whole download and the Task WDT (~5 s) resets the device mid-transfer — sonos-nest hit exactly
  // this (reset reason 6 = TASK_WDT). espota dodges it because ArduinoOTA yields in its own loop.
  // The delay(1) per progress chunk lets IDLE run (feeding the WDT) and also lets loop() keep
  // rendering the LED progress bar; ~250 chunks over a 1 MB image costs well under a second.
  httpUpdate.onProgress([](int cur, int total) {
    delay(1);
    s_progress = total ? (int)((int64_t)cur * 100 / total) : 0;
    static int last = -1;
    if (s_progress != last && s_progress % 10 == 0) { last = s_progress; Serial.printf("[updater] %d%%\n", s_progress); }
  });
  httpUpdate.rebootOnUpdate(true);

  // MUST follow redirects, and this is a SEPARATE setting from the manifest fetch above:
  // HTTPUpdate builds its own internal HTTPClient, so setFollowRedirects() on ours doesn't carry
  // over. GitHub 302s a release asset to a signed CDN host, so without this the download dies
  // instantly with HTTP_UE_SERVER_WRONG_HTTP_CODE (-104, "Wrong HTTP Code") on the 302 itself —
  // which is exactly how the first live pull failed.
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  t_httpUpdate_return r;
  if (url.startsWith("https:")) {
    WiFiClientSecure sec;
    sec.setInsecure();
    r = httpUpdate.update(sec, url, FW_VERSION);
  } else {
    WiFiClient cl;
    r = httpUpdate.update(cl, url, FW_VERSION);
  }

  // Only reached on failure — HTTP_UPDATE_OK reboots.
  s_active   = false;
  s_progress = -1;
  if (r == HTTP_UPDATE_FAILED) {
    s_lastError = "flash failed (" + String(httpUpdate.getLastError()) + ") " + httpUpdate.getLastErrorString();
    Serial.printf("[updater] FAILED %s\n", s_lastError.c_str());
  } else if (r == HTTP_UPDATE_NO_UPDATES) {
    s_lastError = "server reports no update";
    Serial.println("[updater] server reports no update");
  }
}

// Core check. applyAuto is true at boot and on the periodic tick (see the divergence note in the
// header — unlike sonos-nest this unit has nothing to interrupt, and rarely reboots).
static void run(bool applyAuto) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (otaActive()) return;                       // an espota push is mid-flight; stay out of its way

  String base = updaterEffectiveUrl();
  if (base.isEmpty()) { s_available = ""; return; }   // "off"

  s_everChecked = true;
  String version, url;
  if (!checkManifest(base, version, url)) return;     // source down / bad manifest: keep prior state

  if (!isNewer(version, FW_VERSION)) { s_available = ""; return; }
  s_available = version;
  Serial.printf("[updater] update available: %s (running %s)\n", version.c_str(), FW_VERSION);

  if (s_armed || (applyAuto && Settings::otaAuto())) applyNow(url);   // reboots on success
}

void updaterBegin() {
  run(true);
  s_lastCheck = millis();
}

void updaterTick() {
  if (!s_force && s_everChecked && (millis() - s_lastCheck < kCheckMs)) return;
  s_force     = false;
  s_lastCheck = millis();
  run(true);
}
