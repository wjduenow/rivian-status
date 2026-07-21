// rivian_api — the ONLY file that knows Rivian's URLs, headers, and GraphQL strings (plan §8).
// Unofficial, reverse-engineered API; if Rivian changes something, it's fixed here. Sources:
// research/rivian-cloud-api-protocol.md (mined from bretterer/rivian-python-client).
//
// Phase 1 scope: login + getUserInfo + getVehicleState, read-only. No commands, no HMAC, no BLE.
// Tokens live in module statics for the smoke-test; NVS persistence arrives with the `settings`
// module in a later phase. The dc-cid IS persisted now (plan §3/§10) so it's stable from boot one.
#pragma once

#include <Arduino.h>

// Result of a single getVehicleState poll. Every scalar in the API arrives as {timeStamp,value};
// we keep the value plus one timestamp for freshness. `distanceToEmpty` units are UNCONFIRMED
// (km vs miles) — confirming them is a Phase 1 deliverable (plan §6).
struct VehicleStatus {
  bool   valid           = false;
  float  batteryLevel    = NAN;    // charge %, 0-100 — API returns a FLOAT (e.g. 59.700001)
  String chargerState;             // raw enum string (values TBD — Phase 1 records them)
  String chargePortState;          // raw enum string (values TBD)
  float  distanceToEmpty = NAN;    // range, raw value; units TBD (§6)
  String timeStamp;                // timeStamp of batteryLevel
};

namespace RivianApi {

// login() return codes.
enum LoginResult { LOGIN_OK = 0, LOGIN_MFA_REQUIRED = 1, LOGIN_ERROR = -1 };

// One-time init: load-or-generate the persistent dc-cid, pin the Amazon Root CA. Call after WiFi
// is up and system time is set (TLS cert-validity checks need a real clock).
void begin();

// Step 1: CreateCSRFToken. Populates csrf-token + a-sess. No auth headers.
bool createCsrf();

// Step 2: Login(email,password). LOGIN_OK -> u-sess set; LOGIN_MFA_REQUIRED -> call completeOtp().
LoginResult login(const String& email, const String& password);

// Step 3 (MFA only): LoginWithOTP. Rivian texts the code; caller supplies it. Sets u-sess.
bool completeOtp(const String& email, const String& otpCode);

// getUserInfo -> cache the first vehicle's id + vin + name. Call once after auth.
bool fetchVin();

// getVehicleState for the cached vehicle. Requests only the 4 fields we use (plan §5).
bool pollState(VehicleStatus& out);

// Accessors / diagnostics.
String vin();
String vehicleId();
String vehicleName();
String dcCid();
String lastError();   // human-readable last failure
String lastRaw();     // raw body of the last HTTP response (for smoke-test printing)

} // namespace RivianApi
