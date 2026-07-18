# 01 — rivian-status: a headless ESP32-S3 Rivian charge/range status light

**Board:** a dedicated ESP32-S3 (no display), a few status LEDs (ideally one WS2812/NeoPixel
or a small strip; discrete LEDs also fine).
**Goal:** an always-on desk/wall appliance that polls the owner's Rivian over WiFi and shows,
via lights: is it plugged in, is it charging, roughly how full, **whether current range is
below a configurable threshold**, and whether the link to Rivian is healthy. All setup —
Rivian login (incl. MFA), WiFi, and the range threshold — happens in a browser. No screen.

This is a **fresh standalone repo**. It does not need the Sonos control stack. It borrows a
few *patterns* (not the whole core) from the `sonos-nest` repo's plan 04 "sonos-button"
(the headless, browser-configured, one-appliance model): WiFi provisioning via SoftAP
captive portal, an embedded config web page, NVS-persisted settings, and ArduinoOTA.

> **Everything about Rivian's API here is unofficial and reverse-engineered.** Rivian
> publishes no public API. Endpoints, headers, and field names can change without notice.
> Centralize them (§8) so a break is a one-file fix. Sources are cited in §11.

---

## 1. Decisions (settled)

| Question | Decision |
|---|---|
| Data path | **Rivian cloud GraphQL App API** (the app's own endpoint). Ruled IN by research. |
| Local / BLE | **Ruled OUT.** Needs cloud-bootstrapped ECDH secp256r1 phone-key enrollment + HMAC signing; no turnkey ESP32 project; BLE likely won't even expose live charge telemetry. |
| Access mode | **Read-only telemetry.** No vehicle commands → **no HMAC / phone-key / BLE crypto** needed at all. |
| Auth | Rivian account email + password, MFA/OTP when enabled. Entered once via the web page. |
| Credential storage | **Store only session tokens + VIN in NVS. Never persist the password.** |
| Config surface | Small embedded web page (login, status, WiFi, range threshold). Single `WebServer`, one port. |
| WiFi provisioning | `secrets.h`/NVS first, **SoftAP captive portal fallback** (same as sonos-button plan 04). |
| Telemetry transport | **Polling `getVehicleState` query**, not the WebSocket subscription (WS is heavier on an MCU for no benefit here). |
| Poll cadence | **30 s**, exponential backoff on error capped at **900 s**. |
| Primary alert | **current range `< X`**, where X is configurable in the web page. |

### Open decisions (defer, noted where relevant)
- **`distanceToEmpty` units (km vs miles) — UNCONFIRMED.** Verify against the real vehicle in
  Phase 2 before trusting the threshold (§6).
- Whether the low-range light is **charging-aware** (steady = low & not charging, pulse = low
  but charging) or dead simple (low is low). Data for both is free. Lean charging-aware; simple
  is fine for Phase 3.
- Exact LED hardware (single RGB vs a few discrete). §7 assumes one WS2812 as the default.

---

## 2. Why the cloud path is feasible on the ESP32 (the research verdict)

- **AWS CloudFront, not Cloudflare.** `rivian.com/api/gql/...` returns `via: CloudFront` /
  `x-amz-cf-*` — no `cf-ray`, no JS challenge, no cookie wall. CloudFront fronting a GraphQL
  API does not interpose a browser challenge on POSTs. None of the open-source clients
  implement any anti-bot workaround → a plain `WiFiClientSecure` POST is accepted. *This was
  the biggest kill-risk; it's gone.*
- **No JS/browser behavior anywhere.** Every step is a JSON POST. A headless Home Assistant
  integration runs this fine — proof point.
- **Heap is comfortable** because there's **no display**. The TLS-heap warnings in the
  research (≈37 KB/TLS connection; mbedTLS leaks ~1.3–2 KB/handshake with a custom cert
  bundle; BLE+HTTPS together starves heap) were all about a chip *sharing* SRAM with LVGL +
  audio. A display-less, BLE-less Rivian board has the whole chip. **Mitigations we adopt
  anyway:** pin the **root** CA (not a big custom bundle — this also sidesteps the
  #3119 leak, which was specific to the custom-bundle path); never init BLE.
- **On "persistent TLS session":** don't over-invest here. At a 30 s cadence, CloudFront's
  idle timeout will very likely close a held-open socket between polls, so you'd reconnect most
  cycles regardless — and with no display, a per-poll handshake (~37 KB, freed after) is
  affordable. **Budget for per-poll reconnects**; if handshake cost ever bites, reach for TLS
  **session resumption / session tickets** rather than literally holding the socket open. Watch
  heap over a multi-day run to confirm no slow leak (see the watchdog note in §8).
- **Read-only ⇒ no crypto.** Vehicle *commands* need an HMAC signed by an enrolled phone
  keypair; we do none. Only login + `getVehicleState` are required.

---

## 3. Endpoints

```
GRAPHQL_GATEWAY   = https://rivian.com/api/gql/gateway/graphql     # auth + getUserInfo + getVehicleState
GRAPHQL_CHARGING  = https://rivian.com/api/gql/chrg/user/graphql   # (live charging session — not needed for v1)
GRAPHQL_WEBSOCKET = wss://api.rivian.com/gql-consumer-subscriptions/graphql   # (subscription path — NOT used)
```
One POST per call: JSON body `{ operationName, query, variables }`.

**Base headers on every gateway call:**
```
User-Agent: RivianApp/707 CFNetwork/1237 Darwin/20.4.0
Accept: application/json
Content-Type: application/json
Apollographql-Client-Name: com.rivian.ios.consumer-apollo-ios
dc-cid: m-ios-<uuid>        # client-generated device-correlation id (NOT a server token); synthesize once, PERSIST to NVS, reuse forever
```
> Header-identity note: bretterer's client sends the iOS identity above; RivDocs documents the
> Android identity `com.rivian.android.consumer`. Both are reported working. Pick one, keep it
> consistent. There is **no `dc-sess` header** in the real client — `dc-cid` is the only device field.
> Generate the `dc-cid` UUID once on first boot with a real entropy source (`esp_random()`),
> write it to NVS, and reuse it across reboots — a churning `dc-cid` looks like a new device
> every restart.

**TLS:** hosts serve an **Amazon-issued** cert (`CN=rivian.com`, issuer *Amazon RSA 2048 M04*)
via CloudFront, chaining to **Amazon Root CA 1**. Embed that root CA PEM in `WiFiClientSecure`
and **pin the root, not the leaf** (leaf rotates ~200 days). Standard RSA-2048; the Python
clients use default verification with no pinning, so normal CA verification suffices.

---

## 4. Auth handshake (4 steps, all POST to `GRAPHQL_GATEWAY`)

### Step 1 — CSRF token
Headers: base only.
```graphql
mutation CreateCSRFToken { createCsrfToken { __typename csrfToken appSessionToken } }
```
→ store `csrfToken` (as `Csrf-Token`) and `appSessionToken` (as `A-Sess`).

### Step 2 — Login
Headers: base + `Csrf-Token` + `A-Sess`.
```graphql
mutation Login($email: String!, $password: String!) {
  login(email: $email, password: $password) {
    __typename
    ... on MobileLoginResponse    { accessToken refreshToken userSessionToken }
    ... on MobileMFALoginResponse { otpToken }
  }
}
```
- If `data.login` has `userSessionToken` → **done**, it becomes `U-Sess`.
- If `data.login` has only `otpToken` → MFA required; Rivian texts a code. Go to Step 3.

### Step 3 — OTP completion (only when MFA)
Headers: base + `Csrf-Token` + `A-Sess`.
```graphql
mutation LoginWithOTP($email: String!, $otpCode: String!, $otpToken: String!) {
  loginWithOTP(email: $email, otpCode: $otpCode, otpToken: $otpToken) {
    __typename
    ... on MobileLoginResponse { accessToken refreshToken userSessionToken }
  }
}
```
→ store `userSessionToken` as `U-Sess`.

### Step 4 — "Refresh": there is no refresh mutation
The maintained client captures `refreshToken` but never sends it. On an **expired-token
error**, it simply **re-runs `CreateCSRFToken` (reusing the still-valid `U-Sess`)** and retries.
Our token model:
1. Persist `Csrf-Token`, `A-Sess`, `U-Sess`, VIN in NVS.
2. On an auth error → mint a new CSRF token, retry. **Silent, unattended.**
3. Only if that also fails → surface "re-login needed" (link-health LED + web page), owner
   re-enters password/OTP once.

### Header matrix
| Call | Csrf-Token | A-Sess | U-Sess |
|---|---|---|---|
| CreateCSRFToken | – | – | – |
| Login / LoginWithOTP | ✓ | ✓ | – |
| getUserInfo / getVehicleState | – | ✓ | ✓ |

(`accessToken` is captured but the gateway authenticates via `A-Sess`/`U-Sess`, not a bearer.)

---

## 5. Telemetry (polling `getVehicleState`)

**Once at boot:** `getUserInfo` → `data.currentUser.vehicles[].{id, vin, name}` → cache the VIN.
```graphql
query getUserInfo { currentUser { vehicles { id vin name } } }
```

**Every 30 s:** request only the fields we use (each returns `{ timeStamp value }`):
```graphql
query GetVehicleState($vehicleID: String!) {
  vehicleState(id: $vehicleID) {
    batteryLevel   { timeStamp value }   # charge %  (0–100)
    chargerState   { timeStamp value }   # charging engagement / state
    chargePortState{ timeStamp value }   # plug/port door state
    distanceToEmpty{ timeStamp value }   # RANGE — the low-range alert input (units TBD, §6)
  }
}
```
Response is a few hundred bytes → parse with ArduinoJson in a small buffer. **Do not** request
the default ~100-field property set.

**Cadence & limits:** poll 30 s. The API rate-limits (`RATE_LIMIT` error code; no published
number). On any error, exponential backoff `min(30 * 2^errCount, 900)` seconds. Reset on success.

---

## 6. Low-range alert (the primary feature)

```
lowRange = distanceToEmpty.value < rangeThresholdX   // X from NVS, set via web page
```
- **X is configurable** on the web page (a number field), persisted to NVS — same idea as
  sonos-button's configurable volume. No reflash to change it.
- **⚠️ Units unconfirmed (km vs miles).** The Rivian US app shows miles, but the raw API field
  may be km. **Phase 2 smoke-test task:** read the live `distanceToEmpty` value and compare to
  what the app displays; label the web-page field with the confirmed unit; set X accordingly.
  Don't ship the threshold until this is verified.
- **Range, not SoC %, is the right signal** — it already bakes in temperature/load/conditions,
  so "range < X" answers "can I make my trip." (SoC % is still shown for the fullness LED.)
- **Note:** the API also exposes a vehicle-reported `rangeThreshold` field alongside
  `distanceToEmpty` (protocol dig §3). We use our own NVS-configured X (clearer, and decoupled
  from whatever the vehicle uses it for), but the field is there if it ever proves useful.

---

## 7. LED mapping (first pass — assumes one WS2812)

| Indicator | Source field(s) | Suggested behavior |
|---|---|---|
| **Charging activity** | `chargerState` | off=idle · pulsing=charging · steady=complete · red-blink=fault |
| **Plug state** | `chargePortState` | dim = plugged, off = unplugged (or fold into charging LED) |
| **Fullness** | `batteryLevel` | color gradient red→amber→green, or N-LED bar at 25/50/75/100 |
| **Low-range alert** | `distanceToEmpty` (+ opt. `chargerState`) | steady red = low & not charging · slow pulse = low but charging · off = above X |
| **Link health** | poll success / auth state | green heartbeat = OK · amber = re-auth needed · red = offline |

With a single RGB LED, multiplex these as color + blink patterns and priority (fault/offline
wins). With a small strip, give each its own pixel. Finalize once the LED hardware is picked.

---

## 8. Firmware shape (fresh PlatformIO / Arduino ESP32-S3)

Suggested module split (keeps the unofficial-API surface centralized):
- `rivian_api.{h,cpp}` — **the only file that knows Rivian's URLs, headers, and GraphQL
  strings.** Exposes `login()`, `completeOtp()`, `ensureAuth()`, `fetchVin()`, `pollState()`;
  returns a small `VehicleStatus { batteryLevel, chargerState, plugged, rangeRaw, ts }`.
- `net_wifi` / `net_ota` — lift the patterns from `sonos-nest/src/core/net/`.
- `settings` — NVS: tokens (csrf/a-sess/u-sess), VIN, `dc-cid`, WiFi creds, `rangeThresholdX`,
  unit. **Enable NVS encryption** so tokens aren't at rest in cleartext (see §11).
- `webserver` — SoftAP captive-portal WiFi join + the config/status/login page (§9).
- `leds` — the state machine mapping `VehicleStatus` + link health → LED output.
- `main` — boot: wifi (or SoftAP) → ensureAuth → fetchVin → poll loop (30 s) → drive LEDs;
  host `ArduinoOTA.handle()` in `loop()`.

**Concurrency — run the poll in its own FreeRTOS task (the default, not the fallback).** A
`getVehicleState` POST can take seconds, and a dead socket can block up to its timeout; if the
poll and `WebServer` share one cooperative loop, the config page goes unresponsive during every
poll/backoff. Give the poll its own task, share `VehicleStatus` + link-health behind a small
mutex (mirrors sonos-nest's `stateLock()`), and set **explicit socket/TLS timeouts** so a
stalled poll can't wedge the UI.

**Uptime hygiene (24/7 appliance):**
- **WiFi auto-reconnect** — the lifted `net_wifi` pattern must reconnect on drop and drive the
  link-health LED; don't assume the connection survives for weeks.
- **Task watchdog + heap floor** — monitor free heap and reboot on a floor breach; cheap
  insurance against a slow TLS-stack leak over long uptime.

---

## 9. Web page (single `WebServer`, one port)

Unlike sonos-nest's sleep-machine (which needed a second port to dodge `WebServer`'s broken
*upload* body parser), we only do **small form POSTs**, so `arg("...")` works — **one port**.

- **`/` status** — plug/charge state, SoC %, current range (with confirmed unit), the low-range
  threshold X, and link health. Read-only view so a screenless board is still inspectable.
- **`/login`** — two-phase form. Submit email+password → server runs CreateCSRFToken + Login.
  If MFA, reveal an **OTP field** → submit → LoginWithOTP. On success, store tokens+VIN, show
  "connected." **Password is never persisted.** Also the **re-auth surface** when a token dies.
- **`/config`** — set **range threshold X** (+ unit once known), poll cadence (optional),
  device name. Persist to NVS.
- **`/wifi`** — the SoftAP captive-portal join page (creds → NVS), plus a recovery entry point.

Reach it at the device IP or an mDNS hostname (set a `DEVICE_HOSTNAME`, e.g. `rivian-status`).

**Security posture of the config surface (accepted, but explicit — see §11):** the page is
**plain HTTP with no auth**. The Rivian **password crosses the local link in cleartext** on
`/login` (and SoftAP is often open), and anyone on the LAN can view status or hit the re-auth
surface. Acceptable for a hobby appliance on a trusted network; surface a short warning on the
`/login` page and keep the account-token-bearing device off untrusted networks.

---

## 10. Phasing

1. **Auth smoke-test (headless, serial).** Hard-code creds in `secrets.h`; run
   CSRF→Login→(OTP)→getUserInfo→getVehicleState once; print the raw response. *Proves the
   CloudFront/TLS/header path works against the real account.* Phase 1 deliverables:
   - **Confirm `distanceToEmpty` units** (km vs miles) against the app display.
   - **Record the raw enum string values** of `chargerState` and `chargePortState` across
     states (unplugged · plugged-idle · charging · complete · fault) — §7's LED state machine
     can't be built without them, and they only appear on the live account.
   - **MFA path:** if the account has MFA on, the serial test needs an **OTP-over-serial input**
     step (Rivian texts the code at login); build that in, or note Phase 1 assumes MFA-off for
     the first pass.
   - Generate + persist the `dc-cid` (§3) so it's stable from the very first real call.
2. **Poll loop + range check.** 30 s poll, backoff, compute `lowRange`. Print state over serial.
3. **LEDs.** Wire the LED state machine to real status. Pick the LED hardware.
4. **Web page.** Status + `/login` (two-phase, token storage, re-auth) + `/config` (threshold).
5. **WiFi provisioning.** SoftAP captive portal fallback; drop `secrets.h` creds.
6. **OTA + polish.** ArduinoOTA, mDNS name, link-health LED behavior, enclosure.

---

## 11. Risks & caveats
- **Unofficial API, no stability contract** — RivDocs states it "could change or break at any
  moment." Centralize URLs/headers/queries in `rivian_api.cpp`.
- **Token lifetime undocumented** (community reports: hours-to-days). Design is reactive — never
  pre-refresh; handle the expiry error (re-CSRF, then re-login as last resort).
- **Rate limits undocumented** — 30 s poll + capped backoff is the proven-safe cadence.
- **`distanceToEmpty` units unconfirmed** — verify in Phase 1/2 before trusting the threshold.
- **App-identity header** (iOS vs Android) — both reported working; not resolvable without live
  testing on the account.
- **MFA at re-login needs the owner** — unavoidable for a screenless board; the web page is the
  entry point, and the reactive token model makes it rare.
- **Token-at-rest** — a stored `u-sess` is a live credential granting full account read (and the
  account can command the vehicle). NVS is unencrypted by default, so a flash dump leaks it.
  **Mitigation adopted:** enable NVS encryption (§8); never persist the password regardless.
- **Cleartext config surface** — the web page is plain HTTP with no auth: the Rivian password
  crosses the local link in the clear on `/login`, and anyone on the LAN can view status or
  trigger re-auth (§9). Accepted for a trusted-network hobby appliance; keep it off untrusted
  networks and warn on the login page.
- **Persistent-TLS assumption corrected** — don't rely on a held-open socket surviving the 30 s
  idle gap; budget for per-poll handshakes (fine with no display) and use session resumption if
  cost bites (§2). Watch heap over a multi-day run; a task watchdog reboots on a heap-floor
  breach (§8).

---

## Sources
Deep-research + protocol dig, 2026-07-17:
- `bretterer/rivian-python-client` — the maintained client the HACS integration uses (endpoints,
  header set, auth mutations, `getVehicleState`): https://github.com/bretterer/rivian-python-client
- `bretterer/home-assistant-rivian` — cadence (30 s), backoff (cap 900 s), re-CSRF-on-expiry:
  https://github.com/bretterer/home-assistant-rivian
- RivDocs (kaedenb) — endpoint map, auth flow, BLE enrollment (why local is ruled out):
  https://rivian-api.kaedenb.org/
- `the-mace/rivian-python-api` — corroborating cloud-only client:
  https://github.com/the-mace/rivian-python-api
- ESP32 TLS-heap references (mitigation rationale): esp32_https_server #11, Mbed-TLS #3119,
  arduino-esp32 #2175.
- Smartcar (managed OAuth alternative, not chosen): https://smartcar.com/brand/rivian
