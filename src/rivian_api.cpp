#include "rivian_api.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_system.h>

// ---------------------------------------------------------------------------
// Endpoints & identity (plan §3). Change here if the unofficial API moves.
// ---------------------------------------------------------------------------
static const char* GRAPHQL_GATEWAY = "https://rivian.com/api/gql/gateway/graphql";

// Base headers on every gateway call. iOS identity (bretterer's client); Android also works.
static const char* HDR_USER_AGENT   = "RivianApp/707 CFNetwork/1237 Darwin/20.4.0";
static const char* HDR_CLIENT_NAME  = "com.rivian.ios.consumer-apollo-ios";

// Amazon Root CA 1 — the hosts serve an Amazon-issued leaf via CloudFront chaining to this root.
// Pin the ROOT, never the leaf (leaf rotates ~200 days). Fetched from amazontrust.com 2026-07-17.
static const char* AMAZON_ROOT_CA_1 = R"EOF(-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

// ---------------------------------------------------------------------------
// Session state (module-static for the smoke-test; NVS-backed in a later phase).
// ---------------------------------------------------------------------------
static String s_dcCid;          // persistent device-correlation id (NVS)
static String s_csrfToken;      // Csrf-Token
static String s_aSess;          // A-Sess (appSessionToken)
static String s_uSess;          // U-Sess (userSessionToken)
static String s_vin;
static String s_vehicleId;
static String s_vehicleName;
static String s_otpToken;       // carried between login() (MFA) and completeOtp()

static String s_lastError;
static String s_lastRaw;

// ---------------------------------------------------------------------------
// dc-cid: generate once, persist to NVS, reuse forever (plan §3). A churning
// dc-cid looks like a brand-new device on every reboot.
// ---------------------------------------------------------------------------
static String genUuidV4() {
  uint8_t b[16];
  esp_fill_random(b, sizeof(b));           // hardware RNG
  b[6] = (b[6] & 0x0F) | 0x40;             // version 4
  b[8] = (b[8] & 0x3F) | 0x80;             // variant 1
  char s[37];
  snprintf(s, sizeof(s),
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
           b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
  return String("m-ios-") + s;
}

static void loadOrCreateDcCid() {
  Preferences prefs;
  prefs.begin("rivian", false);
  s_dcCid = prefs.getString("dc_cid", "");
  if (s_dcCid.isEmpty()) {
    s_dcCid = genUuidV4();
    prefs.putString("dc_cid", s_dcCid);
  }
  prefs.end();
}

// --- Session persistence ---------------------------------------------------
// NOTE: stored unencrypted for now; enable NVS encryption before shipping (plan §11). u-sess is
// a live credential granting account read access.
static void loadSession() {
  Preferences prefs;
  prefs.begin("rivian", true);           // read-only
  s_uSess = prefs.getString("u_sess", "");
  prefs.end();
}

static void persistSession() {
  Preferences prefs;
  prefs.begin("rivian", false);
  prefs.putString("u_sess", s_uSess);
  prefs.end();
}

bool RivianApi::hasSession() { return !s_uSess.isEmpty(); }

void RivianApi::clearSession() {
  s_uSess = "";
  Preferences prefs;
  prefs.begin("rivian", false);
  prefs.remove("u_sess");
  prefs.end();
}

void RivianApi::seedSession(const String& uSess) {
  s_uSess = uSess;
  persistSession();
}

// ---------------------------------------------------------------------------
// HTTP plumbing. One persistent WiFiClientSecure; HTTPClient rides on it.
// `extraHeaders` are appended after the base set (name/value pairs, count = n*2).
// ---------------------------------------------------------------------------
static WiFiClientSecure s_tls;

static bool gqlPost(const String& body,
                    const char* const* extraHeaders, size_t extraCount,
                    String& outResp, int& outCode) {
  HTTPClient http;
  http.setTimeout(15000);                  // don't let a dead socket wedge the caller (plan §8)
  if (!http.begin(s_tls, GRAPHQL_GATEWAY)) {
    s_lastError = "http.begin failed";
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", HDR_USER_AGENT);
  http.addHeader("Apollographql-Client-Name", HDR_CLIENT_NAME);
  http.addHeader("dc-cid", s_dcCid);
  for (size_t i = 0; i + 1 < extraCount * 2; i += 2) {
    http.addHeader(extraHeaders[i], extraHeaders[i + 1]);
  }

  outCode = http.POST(body);
  outResp = http.getString();
  http.end();

  s_lastRaw = outResp;
  if (outCode <= 0) {
    s_lastError = String("POST failed: ") + HTTPClient::errorToString(outCode);
    return false;
  }
  return true;
}

// Surface a GraphQL-level error (`errors[]`) into s_lastError. Returns true if one was present.
static bool captureGqlError(JsonDocument& doc) {
  if (doc["errors"].is<JsonArray>() && doc["errors"].size() > 0) {
    JsonObject e0 = doc["errors"][0];
    String code = e0["extensions"]["code"] | "";
    String msg  = e0["message"] | "unknown GraphQL error";
    s_lastError = code.isEmpty() ? msg : (code + ": " + msg);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void RivianApi::begin() {
  loadOrCreateDcCid();
  loadSession();                           // reuse a persisted u-sess if we have one (plan §4)
  s_tls.setCACert(AMAZON_ROOT_CA_1);       // pin the root (plan §3)
}

bool RivianApi::createCsrf() {
  s_lastError = "";
  const char* body =
      "{\"operationName\":\"CreateCSRFToken\",\"variables\":null,"
      "\"query\":\"mutation CreateCSRFToken { createCsrfToken { __typename csrfToken appSessionToken } }\"}";

  String resp; int code;
  if (!gqlPost(body, nullptr, 0, resp, code)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) { s_lastError = "CSRF: bad JSON"; return false; }
  if (captureGqlError(doc)) return false;

  JsonObject t = doc["data"]["createCsrfToken"];
  s_csrfToken = t["csrfToken"] | "";
  s_aSess     = t["appSessionToken"] | "";
  if (s_csrfToken.isEmpty() || s_aSess.isEmpty()) {
    s_lastError = "CSRF: missing csrfToken/appSessionToken";
    return false;
  }
  return true;
}

RivianApi::LoginResult RivianApi::login(const String& email, const String& password) {
  s_lastError = "";
  JsonDocument req;
  req["operationName"] = "Login";
  req["variables"]["email"] = email;
  req["variables"]["password"] = password;
  req["query"] =
      "mutation Login($email: String!, $password: String!) { "
      "login(email: $email, password: $password) { __typename "
      "... on MobileLoginResponse { accessToken refreshToken userSessionToken } "
      "... on MobileMFALoginResponse { otpToken } } }";
  String body; serializeJson(req, body);

  const char* headers[] = { "Csrf-Token", s_csrfToken.c_str(), "A-Sess", s_aSess.c_str() };
  String resp; int code;
  if (!gqlPost(body, headers, 2, resp, code)) return LOGIN_ERROR;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) { s_lastError = "Login: bad JSON"; return LOGIN_ERROR; }
  if (captureGqlError(doc)) return LOGIN_ERROR;

  JsonObject login = doc["data"]["login"];
  if (login["userSessionToken"].is<const char*>()) {
    s_uSess = login["userSessionToken"] | "";
    if (s_uSess.isEmpty()) return LOGIN_ERROR;
    persistSession();                      // save for reuse across resets/reflashes (plan §4)
    return LOGIN_OK;
  }
  if (login["otpToken"].is<const char*>()) {
    // MFA path: stash otpToken for completeOtp() (reuses the same csrf + a-sess). Rivian texts
    // the code to the owner's phone.
    s_otpToken = login["otpToken"] | "";
    return s_otpToken.isEmpty() ? LOGIN_ERROR : LOGIN_MFA_REQUIRED;
  }
  s_lastError = "Login: unexpected response shape";
  return LOGIN_ERROR;
}

bool RivianApi::completeOtp(const String& email, const String& otpCode) {
  s_lastError = "";
  if (s_otpToken.isEmpty()) { s_lastError = "OTP: no otpToken from login"; return false; }

  JsonDocument req;
  req["operationName"] = "LoginWithOTP";
  req["variables"]["email"]    = email;
  req["variables"]["otpCode"]  = otpCode;
  req["variables"]["otpToken"] = s_otpToken;
  req["query"] =
      "mutation LoginWithOTP($email: String!, $otpCode: String!, $otpToken: String!) { "
      "loginWithOTP(email: $email, otpCode: $otpCode, otpToken: $otpToken) { __typename "
      "... on MobileLoginResponse { accessToken refreshToken userSessionToken } } }";
  String body; serializeJson(req, body);

  const char* headers[] = { "Csrf-Token", s_csrfToken.c_str(), "A-Sess", s_aSess.c_str() };
  String resp; int code;
  if (!gqlPost(body, headers, 2, resp, code)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) { s_lastError = "OTP: bad JSON"; return false; }
  if (captureGqlError(doc)) return false;

  s_uSess = doc["data"]["loginWithOTP"]["userSessionToken"] | "";
  if (s_uSess.isEmpty()) { s_lastError = "OTP: no userSessionToken"; return false; }
  persistSession();                        // save for reuse across resets/reflashes (plan §4)
  return true;
}

bool RivianApi::fetchVin() {
  s_lastError = "";
  const char* body =
      "{\"operationName\":\"getUserInfo\",\"variables\":null,"
      "\"query\":\"query getUserInfo { currentUser { vehicles { id vin name } } }\"}";

  const char* headers[] = { "A-Sess", s_aSess.c_str(), "U-Sess", s_uSess.c_str() };
  String resp; int code;
  if (!gqlPost(body, headers, 2, resp, code)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) { s_lastError = "getUserInfo: bad JSON"; return false; }
  if (captureGqlError(doc)) return false;

  JsonArray vehicles = doc["data"]["currentUser"]["vehicles"];
  if (vehicles.isNull() || vehicles.size() == 0) {
    s_lastError = "getUserInfo: no vehicles on account";
    return false;
  }
  JsonObject v0 = vehicles[0];
  s_vehicleId   = v0["id"]   | "";
  s_vin         = v0["vin"]  | "";
  s_vehicleName = v0["name"] | "";
  if (s_vin.isEmpty()) { s_lastError = "getUserInfo: empty VIN"; return false; }
  return true;
}

// Build + send a getVehicleState for `id`, parse into `out`. Returns false on error.
static bool pollWith(const String& id, VehicleStatus& out) {
  JsonDocument req;
  req["operationName"] = "GetVehicleState";
  req["variables"]["vehicleID"] = id;
  req["query"] =
      "query GetVehicleState($vehicleID: String!) { vehicleState(id: $vehicleID) { "
      "batteryLevel { timeStamp value } "
      "batteryLimit { timeStamp value } "
      "chargerState { timeStamp value } "
      "chargePortState { timeStamp value } "
      "distanceToEmpty { timeStamp value } "
      "timeToEndOfCharge { timeStamp value } } }";
  String body; serializeJson(req, body);

  const char* headers[] = { "A-Sess", s_aSess.c_str(), "U-Sess", s_uSess.c_str() };
  String resp; int code;
  if (!gqlPost(body, headers, 2, resp, code)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) { s_lastError = "getVehicleState: bad JSON"; return false; }
  if (captureGqlError(doc)) return false;

  JsonObject vs = doc["data"]["vehicleState"];
  if (vs.isNull()) { s_lastError = "getVehicleState: null vehicleState"; return false; }

  out.batteryLevel      = vs["batteryLevel"]["value"]      | (float)NAN;  // float in the API (e.g. 59.7)
  out.batteryLimit      = vs["batteryLimit"]["value"]      | (float)NAN;  // charge target %
  out.chargerState      = vs["chargerState"]["value"]      | "";
  out.chargePortState   = vs["chargePortState"]["value"]   | "";
  out.distanceToEmpty   = vs["distanceToEmpty"]["value"]   | NAN;
  out.timeToEndOfCharge = vs["timeToEndOfCharge"]["value"] | -1;          // minutes (int in API)
  out.timeStamp         = vs["batteryLevel"]["timeStamp"]  | "";
  out.valid = true;
  return true;
}

bool RivianApi::pollState(VehicleStatus& out) {
  s_lastError = "";
  // Phase 1 finding (2026-07-21, live vehicle): vehicleState(id:) takes the vehicle *id*
  // (e.g. "01-244090061"), NOT the VIN — the VIN returns NOT_FOUND. Use id; fall back to VIN
  // only if id is somehow empty.
  if (!s_vehicleId.isEmpty() && pollWith(s_vehicleId, out)) return true;
  String idErr = s_lastError;
  if (pollWith(s_vin, out)) return true;
  if (!idErr.isEmpty())
    s_lastError = idErr + " (VIN key also failed: " + s_lastError + ")";
  return false;
}

String RivianApi::vin()         { return s_vin; }
String RivianApi::vehicleId()   { return s_vehicleId; }
String RivianApi::vehicleName() { return s_vehicleName; }
String RivianApi::userSession() { return s_uSess; }
String RivianApi::dcCid()       { return s_dcCid; }
String RivianApi::lastError()   { return s_lastError; }
String RivianApi::lastRaw()     { return s_lastRaw; }
