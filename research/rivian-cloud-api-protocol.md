# Rivian cloud GraphQL API — protocol details for an ESP32 port

Gathered 2026-07-17. Authoritative implementation mined: **`bretterer/rivian-python-client`**
(`main`) — the exact library the HACS "Rivian (Unofficial)" integration
(`bretterer/home-assistant-rivian`) depends on. `kaedenb/rivian-api` (RivDocs) corroborates and
supplies the endpoint map. Conflicts between the two are flagged inline.

Primary sources:
- Client source: https://github.com/bretterer/rivian-python-client → `src/rivian/rivian.py`, `src/rivian/const.py`
- HA integration: https://github.com/bretterer/home-assistant-rivian → `custom_components/rivian/coordinator.py`, `__init__.py`
- RivDocs: https://rivian-api.kaedenb.org/app/ and `/app/authentication/`

---

## 1. Endpoints

```
GRAPHQL_BASEPATH  = https://rivian.com/api/gql
GRAPHQL_GATEWAY   = https://rivian.com/api/gql/gateway/graphql              # auth + vehicle state + commands
GRAPHQL_CHARGING  = https://rivian.com/api/gql/chrg/user/graphql           # wallboxes + live charging session
GRAPHQL_WEBSOCKET = wss://api.rivian.com/gql-consumer-subscriptions/graphql # live telemetry subscription
```
(RivDocs lists two more service graphs not needed here: `/api/gql/orders/graphql`,
`/api/gql/t2d/graphql`.) Each is one POST endpoint per service; JSON body carries
`operationName` + `query` + `variables`.

**Bot protection / infra (verified live 2026-07-17):** hosts sit behind **AWS CloudFront**,
not Cloudflare. `curl -I` on the gateway returns `via: ...cloudfront.net (CloudFront)` and
`x-amz-cf-*` headers — no `cf-ray`, no JS challenge, no cookie wall. CloudFront fronting a
GraphQL API does not interpose a browser/JS challenge on API POSTs. No open-source client
implements any anti-bot workaround (no cookie jar, no TLS-fingerprint evasion) → strong
evidence a plain TLS client is accepted.

---

## 2. Auth handshake (all POST to `GRAPHQL_GATEWAY`)

Base header set (`rivian.py` 53-58):
```
User-Agent: RivianApp/707 CFNetwork/1237 Darwin/20.4.0
Accept: application/json
Content-Type: application/json
Apollographql-Client-Name: com.rivian.ios.consumer-apollo-ios
```
Plus `dc-cid` auto-added if absent (line 659-660): `dc-cid: m-ios-<uuid4>`. This is a
client-generated device-correlation id, **not** a server token — synthesize any stable
`m-ios-<uuid>` on the ESP32. **There is no `dc-sess` header** in the real client; `dc-cid` is
the only device field. (The `dc-sess` seen in some notes is an older-API conflation.)

**Header-identity conflict:** bretterer sends the iOS identity
(`com.rivian.ios.consumer-apollo-ios`); RivDocs documents the Android identity
(`apollographql-client-name: com.rivian.android.consumer`). Both reported working — server
appears identity-agnostic. Pick one, keep it consistent.

### Step 1 — CSRF token (`create_csrf_token`, lines 138-156)
Headers: base only.
```json
{"operationName":"CreateCSRFToken","variables":null,
 "query":"mutation CreateCSRFToken { createCsrfToken { __typename csrfToken appSessionToken } }"}
```
Returns `data.createCsrfToken.{csrfToken, appSessionToken}`. Store as `csrf-token` and `a-sess`.

### Step 2 — Login (`authenticate`, lines 158-186)
Headers: base + `Csrf-Token: <csrf>` + `A-Sess: <appSessionToken>`.
```graphql
mutation Login($email: String!, $password: String!) {
  login(email: $email, password: $password) {
    __typename
    ... on MobileLoginResponse    { accessToken refreshToken userSessionToken }
    ... on MobileMFALoginResponse { otpToken }
  }
}
```
Variables `{email, password}`. Two possible shapes in `data.login`:
- MFA **not** required → `{accessToken, refreshToken, userSessionToken}` — done; `userSessionToken` → `u-sess`.
- MFA **required** → `{otpToken}` only. Client detects via `"otpToken" in login_data` (line 180) and stashes it. Rivian texts the code.

### Step 3 — OTP completion (`validate_otp`, lines 198-226)
Headers: base + `Csrf-Token` + `A-Sess`.
```graphql
mutation LoginWithOTP($email: String!, $otpCode: String!, $otpToken: String!) {
  loginWithOTP(email: $email, otpCode: $otpCode, otpToken: $otpToken) {
    __typename
    ... on MobileLoginResponse { accessToken refreshToken userSessionToken }
  }
}
```
Variables `{email, otpCode, otpToken}` (otpToken from step 2). Returns
`{accessToken, refreshToken, userSessionToken}`.

### Step 4 — "Refresh": there is none
**No dedicated refresh mutation exists in the maintained client.** `refreshToken` is stored but
never sent (grep of `rivian.py`: `refresh` appears only in `__init__` storage + the two login
query strings). RivDocs documents no refresh mutation either.

How HA handles expiry (`coordinator.py` 97-99):
```python
except RivianExpiredTokenError:
    _LOGGER.info("Rivian token expired, refreshing")
    await self.api.create_csrf_token()
```
i.e. on an expired-token error it **re-runs CreateCSRFToken (reusing the still-valid `u-sess`)**
and retries. Practically for the ESP32: persist `csrf-token`, `a-sess`, `u-sess`; on an auth
error mint a new CSRF token and retry; only fall back to full email/password (+OTP) re-login if
that fails. Simpler than a refresh flow — nothing to implement.

### Header matrix (which tokens on which call)
| Call | Csrf-Token | A-Sess | U-Sess |
|---|---|---|---|
| CreateCSRFToken | – | – | – |
| Login / LoginWithOTP | ✓ | ✓ | – |
| getUserInfo, getVehicleState, images, OTA, commandState | – | ✓ | ✓ |
| sendVehicleCommand, wallboxes, enroll/disenroll | ✓ | ✓ | ✓ |
| getLiveSessionData | – | – | ✓ (u-sess only) |

Reads generally need only `A-Sess`+`U-Sess`; mutations add `Csrf-Token`. (Source: per-method
`headers = BASE_HEADERS | {...}` dicts, `rivian.py` 240-473.) `accessToken` is captured but the
gateway authenticates via `a-sess`/`u-sess`, **not** an `Authorization` bearer — no method sends one.

---

## 3. Telemetry

Both mechanisms keyed by **vehicle ID = the VIN** (`get_vehicle_state(vin, ...)`; the variable is
named `vehicleID` but a VIN is passed).

**Enrollment lookup first** — get the VIN before any telemetry. `get_user_information`
(`getUserInfo`, lines 315-336) → `data.currentUser.vehicles[]` `{id, vin, name, ...}`. Query
once, cache.

### (a) Polling query (`get_vehicle_state`, lines 412-445) — RECOMMENDED for ESP32
Endpoint `GRAPHQL_GATEWAY`, headers `A-Sess`+`U-Sess`:
```graphql
query GetVehicleState($vehicleID: String!) { vehicleState(id: $vehicleID) { ...fields } }
```
Fields come from a property set; each scalar expands to `{ timeStamp value }`
(`_build_vehicle_state_fragment`, lines 734-739). Relevant fields (from `const.py`):
- **Charge %:** `batteryLevel` (also `soc` in the live-session graph)
- **Charging state:** `chargerState`, `chargerStatus`, `chargerDerateStatus`
- **Range:** `distanceToEmpty` (+ `rangeThreshold`)
- **Plug status:** `chargePortState` (port door); charging engagement via `chargerState`
- **Extras:** `batteryCapacity`, `batteryLimit`, `powerState`, `timeToEndOfCharge`,
  `remoteChargingAvailable`, `gnssLocation` (→ `{latitude longitude timeStamp isAuthorized}`)

Response shape: `data.vehicleState.batteryLevel = {"timeStamp":"...","value":73}` — every field
is a timestamped record, not a bare scalar. **Embedded-friendly:** request only the 4-5 fields
you need (the client accepts a `properties` subset) → a few hundred bytes → small ArduinoJson buffer.

### (b) WebSocket subscription (`subscribe_for_vehicle_updates` + `_ws_connect`, 596-649) — NOT used
Endpoint `GRAPHQL_WEBSOCKET`, `graphql-transport-ws` protocol:
```json
// on connect:
{"type":"connection_init","payload":{
  "client-name":"com.rivian.ios.consumer-apollo-ios",
  "client-version":"1.13.0-1494",
  "dc-cid":"m-ios-<uuid4>",
  "u-sess":"<userSessionToken>"}}
// after connection_ack:
{"operationName":"VehicleState",
 "query":"subscription VehicleState($vehicleID: String!) { vehicleState(id: $vehicleID) { ...fields } }",
 "variables":{"vehicleID":"<VIN>"}}
```
Note the WS carries `u-sess` in the init **payload** (not an HTTP header), plus
`client-version: 1.13.0-1494`. Subscription-only fields (not in polling) include tire pressures
and some charging-trip fields (`const.py VEHICLE_STATES_SUBSCRIPTION_ONLY_PROPERTIES`).

**Why we skip it:** the WS needs a persistent `wss` connection, the `graphql-transport-ws`
handshake (`connection_init`/`connection_ack`/`next`/`ping`), and reconnect logic — much heavier
on an MCU than a single POST, for no benefit to a status light.

---

## 4. Token lifetimes & rate limits

- **Lifetimes not published.** RivDocs: "not specified." The client treats tokens as opaque and
  reactive — no pre-emptive refresh; waits for `RivianExpiredTokenError` then re-mints CSRF
  (`coordinator.py` 97-99). Community reports put sessions at hours-to-days; nothing
  authoritative. Treat as "unknown, handle the expiry error."
- **Rate limiting exists.** Client maps a `RATE_LIMIT` GraphQL error code to
  `RivianApiRateLimitError` (`rivian.py` 90, `ERROR_CODE_CLASS_MAP`). No documented number. HA's
  cadence is a safe baseline: main vehicle-state poll every **30 s** (`coordinator.py`
  `_update_interval_seconds = 30`); charging/driver coordinators every 15 min when idle;
  **exponential backoff on errors capped at 900 s** (`min(interval * 2**error_count, 900)`, line 69).

---

## 5. Feasibility notes for embedded (ESP32-S3, Arduino/C++)

**TLS / certificates (verified live 2026-07-17):**
- `rivian.com` and `api.rivian.com` serve an **Amazon-issued** cert:
  `issuer = C=US, O=Amazon, CN=Amazon RSA 2048 M04`, `subject CN=rivian.com`. Fronted by CloudFront.
- Chain roots at **Amazon Root CA 1**. Embed that root CA PEM in `WiFiClientSecure` (or the
  Starfield G2 cross-signed root Amazon still uses). Standard RSA-2048, no exotic curve.
- Certs rotate (~200-day validity; live leaf valid Jul 2026 → Jan 2027) → **pin the root CA,
  never the leaf.**
- Python clients use plain aiohttp with default verification, **no pinning, no client cert** →
  normal CA verification is all that's required.

**No JS / browser behavior.** Every step is a JSON POST (or JSON WS frame). No HTML scraping,
cookies, captcha, or JS anywhere. A headless HA integration runs it fine — proof point.

**Payload sizes:**
- Auth requests/responses: < 1 KB each.
- `getUserInfo`: a few KB (vehicles + feature list) — fetch once at boot for the VIN.
- `getVehicleState` with a subset (`batteryLevel`, `chargerState`, `chargePortState`,
  `distanceToEmpty`): a few hundred bytes. **Do not** request the default ~100-field set.
- Skip the WebSocket to avoid persistent-connection + sub-protocol burden.

**Crypto you can avoid:** `sendVehicleCommand` needs an HMAC signed with an enrolled phone
keypair (NIST P-256 / BLE phone enrollment, `generate_vehicle_command_hmac`). **Read-only
telemetry (battery/charge/range/plug) never touches this** — only login + `getVehicleState`.
Vehicle *control* would additionally require phone enrollment + BLE pairing (a much bigger lift).

**ESP32 TLS-heap context** (why "no display" matters): each mbedTLS `SSL_new()` ≈ 37 KB heap
(esp32_https_server #11); mbedTLS leaks ~1.3–2 KB/handshake with a custom root-cert bundle,
exhausting after ~120 requests (Mbed-TLS #3119); BLE + HTTPS together starved heap (170 KB →
45 KB, HTTPS then failed — arduino-esp32 #2175). All were display/audio-sharing scenarios.
Mitigations adopted anyway: persistent TLS session (poll over it, don't reconnect), pin the
root CA (not a big bundle), never init BLE.

---

## 6. Conflicts / staleness caveats
- **App-identity header** differs between sources (iOS `com.rivian.ios.consumer-apollo-ios` in
  bretterer vs Android `com.rivian.android.consumer` in RivDocs). Both reportedly work; not
  resolvable without live testing on the account.
- **`dc-sess`** does not exist in either current source — the device field is `dc-cid`,
  client-generated. Older-API conflation.
- **Token lifetimes / rate-limit numbers** are genuinely undocumented; values above are HA's
  empirical cadence, not published guarantees.
- **Unofficial API, no stability contract** — RivDocs states it "could change or break at any
  moment." Centralize endpoint URLs, headers, and query strings so a server-side change is a
  one-file fix.
