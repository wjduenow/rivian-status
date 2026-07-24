// rivian-status — serial-only test harnesses for Phases 1 & 2 (plan §10).
//
//   -DPHASE1_SMOKE_TEST : one-shot auth + telemetry test; records the Phase 1 deliverables
//                         (distanceToEmpty units + chargerState/chargePortState enum values).
//   -DPHASE2_POLL_LOOP  : authenticate once, then poll getVehicleState on a 30 s cadence with
//                         exponential backoff and the lowRange check (plan §5/§6).
//
// Both share the same network + auth bring-up below. Creds are hard-coded in include/secrets.h
// for these phases only; the shipped firmware never persists the password (plan §1).

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>

#include "rivian_api.h"
#include "settings.h"
#include "webserver.h"
#include "net_wifi.h"
#include "net_ota.h"
#include "leds.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// Phases 1 & 2 hard-code creds in secrets.h. Phase 3 provisions WiFi via the portal and logs in
// via the browser, so it doesn't require the macros.
#if (defined(PHASE1_SMOKE_TEST) || defined(PHASE2_POLL_LOOP)) && (!defined(WIFI_SSID) || !defined(RIVIAN_EMAIL))
#error "Copy include/secrets.h.example to include/secrets.h and fill in your credentials."
#endif

// ===========================================================================
// Shared helpers
// ===========================================================================
static void rule(const char* label) {
  Serial.printf("\n========== %s ==========\n", label);
}

#if defined(PHASE1_SMOKE_TEST) || defined(PHASE2_POLL_LOOP)
static void printRaw() {
  Serial.println("--- raw response ---");
  Serial.println(RivianApi::lastRaw());
  Serial.println("--------------------");
}
#endif

// Halt: park the CPU so a one-shot test doesn't loop.
static void halt(const char* why) {
  Serial.printf("\n[HALT] %s\n", why);
  for (;;) delay(1000);
}

// Connect via the Net module (hostname = device name, NVS creds then secrets.h). Returns false
// on timeout so the caller decides: halt (dev serial tests) or raise the portal (the appliance).
static bool connectWifi() {
  rule("WiFi");
  Serial.printf("Connecting as \"%s\"...\n", Net::hostname().c_str());
  if (!Net::connect(20000)) return false;
  Serial.printf("Connected. IP=%s  RSSI=%d dBm  host=%s\n",
                Net::ip().toString().c_str(), WiFi.RSSI(), WiFi.getHostname());
  return true;
}

// TLS cert-validity checks need a real wall clock. Sync SNTP before any HTTPS.
static bool syncTime() {
  rule("NTP time sync");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = 0;
  uint32_t start = millis();
  while ((now = time(nullptr)) < 1700000000 && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (now < 1700000000) {
    Serial.println("FAILED — no NTP; TLS cert validation will likely fail.");
    return false;
  }
  Serial.printf("Time set: %s", asctime(gmtime(&now)));  // asctime adds \n
  return true;
}

#if defined(PHASE1_SMOKE_TEST) || defined(PHASE2_POLL_LOOP)
// Blocking read of one line from serial (for the MFA OTP). Echoes as typed.
static String readSerialLine(const char* prompt) {
  Serial.print(prompt);
  String line;
  for (;;) {
    while (!Serial.available()) delay(10);
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') break;
    line += c;
    Serial.print(c);
  }
  Serial.println();
  line.trim();
  return line;
}
#endif  // PHASE1_SMOKE_TEST || PHASE2_POLL_LOOP

// Settings + WiFi + NTP + API init. `provision`: on WiFi failure, raise the SoftAP captive portal
// (the appliance) instead of halting (dev serial tests). Settings load first — the DHCP hostname
// is the device name and must be known before WiFi associates.
static void bringUpNetwork(bool provision) {
  Settings::begin();
  if (!connectWifi()) {
    if (provision) {
      Serial.println("No WiFi — raising setup portal (join \"<name>-setup\").");
      Net::runPortal();                // blocks until joined; reboots on success
    } else {
      halt("no WiFi (check creds / antenna)");
    }
  }
  syncTime();                          // continue even if it fails, to surface the TLS error
  RivianApi::begin();
  Serial.printf("dc-cid (persistent): %s\n", RivianApi::dcCid().c_str());
#ifdef SEED_USESS
  // One-time bootstrap: inject an out-of-band u-sess (from secrets.h, gitignored) if NVS has none,
  // so the reuse path can be exercised without a fresh login/OTP. Remove once NVS is seeded.
  if (!RivianApi::hasSession()) {
    RivianApi::seedSession(SEED_USESS);
    Serial.println("[seed] injected u-sess from build define");
  }
#endif
  Serial.printf("persisted session present: %s\n", RivianApi::hasSession() ? "yes" : "no");
}

// Full auth handshake: CSRF -> Login -> (OTP over serial if MFA) -> getUserInfo. Shared by the two
// serial harnesses only — Phase 3 logs in from the browser (webserver.cpp), so this is compiled
// out there. That matters for CI: it's the only code that reads RIVIAN_EMAIL/RIVIAN_PASSWORD, and
// a Release build of phase3 is made with NO include/secrets.h at all (see .github/workflows/).
// `verbose` dumps raw responses (Phase 1 wants them). Returns true once the VIN is cached.
#if defined(PHASE1_SMOKE_TEST) || defined(PHASE2_POLL_LOOP)
static bool authenticate(bool verbose) {
  // Fast path (plan §4): if a u-sess was persisted from a prior run, reuse it — mint a fresh CSRF
  // (which yields a new a-sess) and prove the session with getUserInfo. No login, no MFA/OTP.
  // This is what makes resets/reflashes painless once you've authenticated once.
  if (RivianApi::hasSession()) {
    rule("Auth: reuse persisted u-sess (no login/OTP)");
    if (RivianApi::createCsrf() && RivianApi::fetchVin()) {
      Serial.printf("Session reuse OK. Vehicle: name=\"%s\"  vin=%s  id=%s\n",
                    RivianApi::vehicleName().c_str(), RivianApi::vin().c_str(),
                    RivianApi::vehicleId().c_str());
      return true;
    }
    Serial.printf("Persisted u-sess invalid (%s) — clearing, falling back to full login.\n",
                  RivianApi::lastError().c_str());
    RivianApi::clearSession();
  }

  rule("Auth: CreateCSRFToken");
  if (!RivianApi::createCsrf()) { printRaw(); Serial.printf("CSRF failed: %s\n", RivianApi::lastError().c_str()); return false; }
  if (verbose) printRaw();
  Serial.println("OK — csrf-token + a-sess acquired.");

  rule("Auth: Login");
  RivianApi::LoginResult lr = RivianApi::login(RIVIAN_EMAIL, RIVIAN_PASSWORD);
  if (verbose) printRaw();
  if (lr == RivianApi::LOGIN_ERROR) { if (!verbose) printRaw(); Serial.printf("Login failed: %s\n", RivianApi::lastError().c_str()); return false; }

  if (lr == RivianApi::LOGIN_MFA_REQUIRED) {
    rule("Auth: LoginWithOTP (MFA)");
    Serial.println("MFA enabled — Rivian just sent a code to your email.");
    String otp = readSerialLine("Enter OTP code, then press Enter: ");
    if (!RivianApi::completeOtp(RIVIAN_EMAIL, otp)) { printRaw(); Serial.printf("OTP failed: %s\n", RivianApi::lastError().c_str()); return false; }
    if (verbose) printRaw();
    Serial.println("OK — OTP accepted, u-sess acquired.");
  } else {
    Serial.println("No MFA — u-sess acquired directly.");
  }

  rule("Auth: getUserInfo (fetch VIN)");
  if (!RivianApi::fetchVin()) { printRaw(); Serial.printf("getUserInfo failed: %s\n", RivianApi::lastError().c_str()); return false; }
  if (verbose) printRaw();
  Serial.printf("Vehicle: name=\"%s\"  vin=%s  id=%s\n",
                RivianApi::vehicleName().c_str(), RivianApi::vin().c_str(),
                RivianApi::vehicleId().c_str());

  // We just did a full login (this path only runs when session-reuse failed / no stored token).
  // The u-sess is now persisted to NVS, but print it too so you can refresh SEED_USESS in
  // secrets.h — then even an NVS wipe won't force another OTP until this token expires.
  Serial.println("\n>>> Fresh login complete. To keep re-images OTP-free, set in include/secrets.h:");
  Serial.printf(">>>   #define SEED_USESS \"%s\"\n", RivianApi::userSession().c_str());
  return true;
}
#endif  // PHASE1_SMOKE_TEST || PHASE2_POLL_LOOP

// ===========================================================================
// PHASE 1 — one-shot smoke test
// ===========================================================================
#if defined(PHASE1_SMOKE_TEST)

void setup() {
  Serial.begin(115200);
  delay(2000);                         // let USB-CDC enumerate before the first prints
  Serial.println("\n\n### rivian-status — Phase 1 auth smoke-test ###");

  bringUpNetwork(/*provision=*/false);
  if (!authenticate(/*verbose=*/true)) halt("auth failed");

  rule("getVehicleState");
  VehicleStatus st;
  if (!RivianApi::pollState(st)) { printRaw(); halt(RivianApi::lastError().c_str()); }
  printRaw();
  if (!RivianApi::lastError().isEmpty())
    Serial.printf("[note] %s\n", RivianApi::lastError().c_str());

  // -------- Phase 1 deliverables (plan §10) --------
  rule("PHASE 1 DELIVERABLES — record these");
  Serial.printf("batteryLevel     : %.1f %%\n", st.batteryLevel);
  Serial.printf("batteryLimit     : %.0f %%   (charge target — where charging stops)\n", st.batteryLimit);
  Serial.printf("timeToEndOfCharge: %d min\n", st.timeToEndOfCharge);
  Serial.printf("chargerState     : \"%s\"   <-- record this enum value\n", st.chargerState.c_str());
  Serial.printf("chargePortState  : \"%s\"   <-- record this enum value\n", st.chargePortState.c_str());
  Serial.printf("distanceToEmpty  : %.1f  (units UNCONFIRMED)\n", st.distanceToEmpty);
  Serial.printf("  timeStamp      : %s\n", st.timeStamp.c_str());
  Serial.println();
  Serial.println(">>> UNITS CHECK: open the Rivian app and compare its displayed range to the");
  Serial.printf(">>> distanceToEmpty above (%.1f). If the app shows ~%.0f mi, units are MILES;\n",
                st.distanceToEmpty, st.distanceToEmpty);
  Serial.printf(">>> if the app shows a number ~1.6x larger (~%.0f), the API value is in KM.\n",
                st.distanceToEmpty * 1.60934);
  Serial.println(">>> Then set RANGE_THRESHOLD_X (platformio.ini phase2) in the confirmed unit.");
  Serial.println();
  Serial.println(">>> To map §7 LEDs, re-run in different charge states (unplugged, plugged-idle,");
  Serial.println(">>> charging, complete, fault) and record each chargerState/chargePortState value.");

  rule("DONE");
  Serial.println("Auth + telemetry path verified end-to-end against the real account.");
}

void loop() { delay(1000); }           // one-shot; poll loop is Phase 2

// ===========================================================================
// PHASE 2 — continuous poll loop (plan §5/§6)
// ===========================================================================
#elif defined(PHASE2_POLL_LOOP)

static const uint32_t POLL_INTERVAL_S = 30;      // base cadence (plan §5)
static const uint32_t BACKOFF_CAP_S   = 900;     // exponential-backoff ceiling (plan §5)

#ifndef RANGE_THRESHOLD_X
#define RANGE_THRESHOLD_X 50                      // fallback if the build flag is missing
#endif

static uint32_t s_errCount = 0;                  // drives min(30 * 2^n, 900) backoff

// Sleep in 1 s steps so a long backoff still feels responsive on the console.
static void sleepSeconds(uint32_t s) {
  for (uint32_t i = 0; i < s; i++) delay(1000);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n### rivian-status — Phase 2 poll loop ###");
  Serial.printf("Poll cadence %us, backoff cap %us, rangeThresholdX=%d miles (API km -> mi)\n",
                POLL_INTERVAL_S, BACKOFF_CAP_S, (int)RANGE_THRESHOLD_X);

  bringUpNetwork(/*provision=*/false);
  if (!authenticate(/*verbose=*/false)) halt("initial auth failed");
  Serial.println("\nAuthenticated. Entering poll loop.");
}

void loop() {
  VehicleStatus st;
  if (RivianApi::pollState(st)) {
    s_errCount = 0;                              // reset backoff on success

    // lowRange = distanceToEmpty < X (plan §6). Charging-aware annotation is free (§7): when
    // low AND charging, the alert would "pulse" rather than sit steady. chargerState enum
    // values are TBD (Phase 1), so we match on a substring heuristic for now.
    // distanceToEmpty is KILOMETERS (Phase 1 confirmed); convert to miles for the US threshold.
    float rangeMiles = isnan(st.distanceToEmpty) ? NAN : st.distanceToEmpty / 1.60934f;
    bool lowRange  = !isnan(rangeMiles) && rangeMiles < RANGE_THRESHOLD_X;
    // chargerState=="charging_active" seen in Phase 1; keep a substring match for other states.
    String cs = st.chargerState; cs.toLowerCase();
    bool charging  = cs.indexOf("charg") >= 0 && cs.indexOf("not") < 0;

    // "charge complete" ≈ current has reached the target (more reliable than chargerState alone).
    bool atTarget = !isnan(st.batteryLevel) && !isnan(st.batteryLimit) &&
                    st.batteryLevel >= st.batteryLimit - 0.5f;
    Serial.printf("[poll] soc=%.1f%% / limit %.0f%%  range=%.0f mi (%.0f km)  charger=\"%s\"  port=\"%s\"  eta=%dmin%s\n",
                  st.batteryLevel, st.batteryLimit, rangeMiles, st.distanceToEmpty,
                  st.chargerState.c_str(), st.chargePortState.c_str(), st.timeToEndOfCharge,
                  atTarget ? "  [at target]" : "");
    if (lowRange) {
      Serial.printf("  >>> LOW RANGE (%.0f mi < %d mi)%s\n", rangeMiles, (int)RANGE_THRESHOLD_X,
                    charging ? " — but charging (would pulse)" : " — not charging (would be steady red)");
    }
    sleepSeconds(POLL_INTERVAL_S);
  } else {
    s_errCount++;
    Serial.printf("[poll] error (#%lu): %s\n", (unsigned long)s_errCount, RivianApi::lastError().c_str());

    // Reactive token model (plan §4): on any failure, mint a fresh CSRF (reuses the still-valid
    // u-sess) before retrying — this silently recovers from an expired a-sess/csrf. Only a full
    // re-login (owner + OTP) would be needed if u-sess itself has died; that surfaces as repeated
    // failures here and, in the shipped firmware, lights the "re-auth needed" LED (§7).
    if (!RivianApi::createCsrf())
      Serial.printf("  re-CSRF also failed: %s (u-sess may be dead → re-login needed)\n",
                    RivianApi::lastError().c_str());
    else
      Serial.println("  re-CSRF ok — will retry poll after backoff");

    uint32_t backoff = POLL_INTERVAL_S << (s_errCount > 5 ? 5 : s_errCount);  // 30 * 2^n, clamp shift
    if (backoff > BACKOFF_CAP_S) backoff = BACKOFF_CAP_S;
    Serial.printf("  backing off %us\n", backoff);
    sleepSeconds(backoff);
  }
}

// ===========================================================================
// PHASE 3 — browser UI (status + login + config) with a background poll task
// ===========================================================================
#elif defined(PHASE3_WEBAPP)

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n### rivian-status — Phase 3 web app ###");

  bringUpNetwork(/*provision=*/true);      // Settings + WiFi (portal fallback) + NTP + RivianApi
  webAppBegin();                           // mDNS + HTTP server + poll task (reuses u-sess)
  otaBegin();                              // ArduinoOTA as <device name>.local
  ledsBegin();                             // 8-pixel WS2812 status map on D10/GPIO9 (§7)

  Serial.printf("Web UI: http://%s/  (or http://%s.local/)\n",
                WiFi.localIP().toString().c_str(), Settings::deviceName().c_str());
  Serial.printf("Persisted session: %s — %s\n",
                RivianApi::hasSession() ? "yes" : "no",
                RivianApi::hasSession() ? "polling starts automatically"
                                        : "open /login in a browser to authenticate");
}

void loop() {
  webAppLoop();
  otaHandle();                             // service wireless firmware pushes
  ledsLoop();                              // re-render the status pixels (~50 Hz, self-limited)
  delay(2);
}

#else
#error "Define PHASE1_SMOKE_TEST, PHASE2_POLL_LOOP, or PHASE3_WEBAPP (select env:phase1/2/3)."
#endif
