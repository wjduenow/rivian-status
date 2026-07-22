# 01 — rivian-status: a headless ESP32-S3 Rivian charge/range status light

**Board:** **Seeed Studio XIAO ESP32-S3** (ESP32-S3R8, 8 MB PSRAM / 8 MB flash, USB-C, no
display). **LEDs:** an **8-pixel WS2812/NeoPixel stick**, USB-powered from the board's `5V`
pin — no external supply (see §7 for the pixel map, wiring, and power budget).
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
| Board | **Seeed Studio XIAO ESP32-S3** (ESP32-S3R8, 8 MB PSRAM, USB-C). |
| LED hardware | **8-pixel WS2812/NeoPixel stick**, one pixel per indicator (§7). Data on `D10`/GPIO9; powered from the `5V` (USB VBUS) pin; single-supply via a firmware brightness cap. |

### Open decisions (defer, noted where relevant)
- ~~`distanceToEmpty` units~~ — **CONFIRMED 2026-07-21 (Phase 1, live vehicle): the API field is
  in KILOMETERS; the US app displays miles** (391 km API ≈ 244 mi app). Firmware converts km→mi;
  the web-page threshold X is entered in **miles** (§6).
- Whether the low-range light is **charging-aware** (steady = low & not charging, pulse = low
  but charging) or dead simple (low is low). Data for both is free. Lean charging-aware; decide
  in the LED phase (§10 phase 6).

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
1. Persist **`U-Sess`** (+ VIN) in NVS. `Csrf-Token`/`A-Sess` are **re-minted each boot** by
   CreateCSRFToken, so they don't need persisting — only `U-Sess` does.
2. On boot, **reuse the persisted `U-Sess`**: mint a fresh CSRF/A-Sess, then call a read to
   verify. If it works, skip login/OTP entirely.
3. On an auth error → mint a new CSRF token, retry. **Silent, unattended.**
4. Only if that also fails → clear the dead `U-Sess`, surface "re-login needed" (link-health LED
   + web page), owner re-enters password/OTP once.

> **✅ Implemented & verified (Phase 1, 2026-07-21):** `rivian_api` persists `U-Sess` to NVS on
> login/OTP success and reuses it on the next boot. Confirmed live — a board reset reused a
> ~40-min-old `U-Sess` via fresh CSRF and pulled vehicle state with **no login and no OTP**. This
> is what keeps MFA off the critical path for a screenless appliance.

### Header matrix
| Call | Csrf-Token | A-Sess | U-Sess |
|---|---|---|---|
| CreateCSRFToken | – | – | – |
| Login / LoginWithOTP | ✓ | ✓ | – |
| getUserInfo / getVehicleState | – | ✓ | ✓ |

(`accessToken` is captured but the gateway authenticates via `A-Sess`/`U-Sess`, not a bearer.)

---

## 5. Telemetry (polling `getVehicleState`)

**Once at boot:** `getUserInfo` → `data.currentUser.vehicles[].{id, vin, name}` → cache **both the
`id` and the VIN**.
```graphql
query getUserInfo { currentUser { vehicles { id vin name } } }
```
> **Phase 1 correction (2026-07-21, live):** `vehicleState(id:)` takes the vehicle **`id`**
> (e.g. `01-244090061`), **NOT the VIN** — passing the VIN returns `NOT_FOUND`. (`name` can be
> `null`.) The protocol dig guessed "VIN"; reality is the `id`. Firmware keys on `id`.

**Every 30 s:** request only the fields we use (each returns `{ timeStamp value }`):
```graphql
query GetVehicleState($vehicleID: String!) {   # pass the vehicle `id`, NOT the VIN (see above)
  vehicleState(id: $vehicleID) {
    batteryLevel     { timeStamp value } # charge % — FLOAT, e.g. 59.700001 (parse as float!)
    batteryLimit     { timeStamp value } # charge TARGET % (e.g. 70 or 100) — where charging stops
    chargerState     { timeStamp value } # e.g. "charging_active" (Phase 1)
    chargePortState  { timeStamp value } # e.g. "open" (Phase 1)
    distanceToEmpty  { timeStamp value } # RANGE in KILOMETERS (§6); firmware converts to miles
    timeToEndOfCharge{ timeStamp value } # minutes to reach batteryLimit (int)
  }
}
```
`batteryLimit` reflects the owner's charge cap — e.g. `70` when set for battery longevity, `100`
for a full charge (confirmed live 2026-07-21: limit 70 while SoC 61%). "Charge complete" is best
read as `batteryLevel ≈ batteryLimit`, not `chargerState` alone.
Response is a few hundred bytes → parse with ArduinoJson in a small buffer. **Do not** request
the default ~100-field property set.

**Cadence & limits:** poll 30 s. The API rate-limits (`RATE_LIMIT` error code; no published
number). On any error, exponential backoff `min(30 * 2^errCount, 900)` seconds. Reset on success.

---

## 6. Low-range alert (the primary feature)

```
rangeMiles = distanceToEmpty.value / 1.60934        // API value is KM (confirmed §1); convert
lowRange   = rangeMiles < rangeThresholdX           // X in MILES, from NVS, set via web page
```
- **X is configurable** on the web page (a number field, **in miles**), persisted to NVS — same
  idea as sonos-button's configurable volume. No reflash to change it.
- **✅ Units confirmed (Phase 1, 2026-07-21):** the raw `distanceToEmpty` field is **kilometers**;
  the US app shows **miles** (live check: 391 km API ≈ 244 mi app). Firmware divides by 1.60934
  and the web page labels/accepts the threshold in miles. (If an owner ever wants km, that's a
  display-unit toggle later — the raw field stays km.)
- **Range, not SoC %, is the right signal** — it already bakes in temperature/load/conditions,
  so "range < X" answers "can I make my trip." (SoC % is still shown for the fullness LED.)
- **Note:** the API also exposes a vehicle-reported `rangeThreshold` field alongside
  `distanceToEmpty` (protocol dig §3). We use our own NVS-configured X (clearer, and decoupled
  from whatever the vehicle uses it for), but the field is there if it ever proves useful.

---

## 7. LEDs (locked: 8-pixel WS2812 stick on the XIAO ESP32-S3)

**Hardware (settled).** An **8-pixel WS2812/NeoPixel stick** (bare, no onboard controller —
the XIAO *is* the controller). The whole strip is a **single meter** (not one-pixel-per-role):
it fills by **SoC ÷ charge-target** (`batteryLevel / batteryLimit`), so "full" = you've hit
your set charge limit, not 100 %.

### Behavior (implemented in `leds.cpp render()`)
| State | Strip |
|---|---|
| **Link down** (offline / needs re-auth / no telemetry yet) | **pixel 0 pulses red**, rest off |
| **Up + charging** (`chargerState == "charging_active"`) | the **leading meter pixel pulses red** and climbs as SoC rises; rest off |
| **Up + not charging + range < threshold** (`distanceToEmpty` mi < `rangeThresholdMiles`) | **all pixels flash red together** (low-range alert) |
| **Up + not charging + range OK** | **meter: green filled + red empty** (fill = SoC/target; all green once at target) |
| **OTA push in progress** | whole strip = a **blue progress bar** (`otaProgress()`) |

Brightness-capped (`-DLED_BRIGHTNESS`, single-supply off USB VBUS). "Charging" is `charging_active`
only; `charging_ready` (plugged-idle) reads as not-charging.

**Observed `chargerState` values (growing):** `"charging_active"` (pushing power),
`"charging_ready"` (plugged, idle). Still needed: the unplugged / complete / fault values —
capture by observing those states. `chargePortState` (`"open"`/`"close"`) is ambiguous and
currently unused. The one enum read lives in `leds.cpp` (`sCharging`) for a one-line fix.

### Wiring (single-supply, USB-powered — no external PSU)
```
        XIAO ESP32-S3                      8-pixel WS2812 stick
   ┌───────[ USB-C ]───────┐
5V │ ●───────────────────────────────────► 5V   (VIN)
GND│ ●───────────────────────────────────► GND
3V3│ ●   (do NOT use — 700 mA reg,               ▲
   │      shared with the ESP32)                 │ common ground
D10│ ●──[ ~330 Ω ]──────────────────────► DIN    │
   │  ...                                         │
   └───────────────────────────────────┘         │
                                                  │
   (optional) 470–1000 µF cap across the stick's 5V↔GND if pixel 0 flickers on power-up
```
- **DIN** ← `D10` (GPIO9) through a **~330 Ω** series resistor. GPIO9 is a plain IO — not a
  strapping/USB pin (avoid GPIO0/3/19/20/45/46), so it's safe to drive at boot. `D10` is the
  board's default SPI MOSI, which is free here (no SPI peripheral used).
- **5V** (VIN) ← the XIAO **`5V`** pin — the USB VBUS passthrough (top pin by the USB-C jack).
  Present only when USB-powered; if the board is ever run from a LiPo, this pin is dead and the
  strip loses power (not our case — this is a mains-USB appliance).
- **3.3 V data into a 5 V strip** is marginally under WS2812 spec but reliable at this short
  length; if pixel 0 ever glitches, add a **74AHCT125** shifter on DIN (fallback, not baseline).

### Power budget (why single-supply is safe)
8 px × ~60 mA at full white ≈ **480 mA** — that *would* exceed the safe USB headroom (a plain
port gives ~500 mA and the ESP32's WiFi TX already spikes ~300–350 mA). The fix is a **firmware
brightness cap** (`FastLED.setBrightness(~48/255)` default) plus status colors that are rarely
white: real draw lands in the low tens of mA, comfortably inside budget. **The brightness cap is
the load-bearing mitigation** — it's a hard default in the `leds` module, not a user setting.

---

## 8. Firmware shape (fresh PlatformIO / Arduino ESP32-S3)

Suggested module split (keeps the unofficial-API surface centralized):
- `rivian_api.{h,cpp}` — **the only file that knows Rivian's URLs, headers, and GraphQL
  strings.** Exposes `login()`, `completeOtp()`, `ensureAuth()`, `fetchVin()`, `pollState()`;
  returns a small `VehicleStatus { batteryLevel, chargerState, plugged, rangeRaw, ts }`.
- `net_wifi` — ✅ built. Hostname (= device name, set before STA transition), NVS-or-`secrets.h`
  creds, runtime `apply()`, and the SoftAP captive portal. Lifted from `sonos-nest/src/core/net/`.
- `net_ota` — ✅ built. ArduinoOTA as `<device name>.local`, optional `OTA_PASSWORD`; `otaHandle()`
  serviced in `loop()`. Verified with a live wireless update.
- `settings` — ✅ built (NVS `cfg`): `rangeThresholdMiles`, `deviceName` (sanitized hostname),
  WiFi creds. Tokens/`dc-cid` live in `rivian_api`'s own NVS. **Enable NVS encryption** so tokens
  + WiFi pass aren't at rest in cleartext (see §11).
- `webserver` — SoftAP captive-portal WiFi join + the config/status/login page (§9).
- `leds` — the state machine mapping `VehicleStatus` + link health → the 8-pixel map (§7).
  Uses **`fastled/FastLED`** on `-DLED_DATA_PIN=9` (D10/GPIO9); enforces the brightness-cap
  default (§7 power budget) as a hard floor, not a user knob.
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

1. **✅ DONE (2026-07-21) — Auth smoke-test (headless, serial).** Hard-code creds in `secrets.h`;
   run CSRF→Login→(OTP)→getUserInfo→getVehicleState once; print the raw response. *Proved the
   CloudFront/TLS/header path works against the real account.* Phase 1 deliverables (all met):
   - **Confirm `distanceToEmpty` units** (km vs miles) against the app display.
   - **Record the raw enum string values** of `chargerState` and `chargePortState` across
     states (unplugged · plugged-idle · charging · complete · fault) — §7's LED state machine
     can't be built without them, and they only appear on the live account.
   - **MFA path:** if the account has MFA on, the serial test needs an **OTP-over-serial input**
     step (Rivian texts the code at login); build that in, or note Phase 1 assumes MFA-off for
     the first pass.
   - Generate + persist the `dc-cid` (§3) so it's stable from the very first real call.
2. **✅ DONE — Poll loop + range check.** 30 s poll, `min(30·2^n,900)` backoff, `lowRange`
   (km→mi). Added `batteryLimit`/`timeToEndOfCharge`. Serial-only. Session persisted/reused.

   *(LEDs moved to the end — the 8-pixel stick hasn't arrived; the software phases below don't
   need it, so do them first and finish with the light.)*
3. **✅ DONE — Web page.** Single `WebServer:80` + a FreeRTOS poll task (mutex-guarded TLS +
   snapshot, per §8). `/` live status (auto-refresh, link-health pill), `/login` (two-phase
   email/password→OTP, password never stored), `/config` (threshold miles + device name → NVS),
   mDNS `<name>.local`. Verified live from a browser: status renders real telemetry, config
   persists. (Browser login→OTP path uses the same `rivian_api` calls proven in Phase 1; not
   re-exercised live to conserve OTPs.) **Device name = DHCP hostname + mDNS + AP name** (single
   source, `Settings`); `setHostname` set before the STA transition (arduino-esp32 quirk, per
   sonos-nest); a rename **reboots** so all three update together — verified live.
4. **✅ DONE — WiFi provisioning.** `net_wifi` module (borrowed from sonos-nest wifi.cpp +
   portal.cpp): tries NVS creds then `secrets.h`; on failure raises a **SoftAP captive portal**
   (open AP `<name>-setup`, wildcard DNS, scan-list join page) that also sets the device name,
   then reboots into STA. Creds persist to NVS. (Portal path compile-verified + mirrors the proven
   sonos-nest code; not exercised live since valid creds are present — would need a creds wipe.)
5. **✅ DONE — OTA.** `net_ota` (ArduinoOTA) advertises as `<device name>.local`, optional
   `OTA_PASSWORD` from `secrets.h`. `phase3-ota` env does espota uploads
   (`OTA_PASSWORD=<pw> pio run -e phase3-ota -t upload`). **Verified live: a full firmware update
   over WiFi (authenticated) succeeded and the device rebooted into it — no USB.** (Enclosure is
   physical, still to do; link-health behavior lands with the LEDs.)
6. **LEDs (last — needs the 8-pixel stick).** Wire the `leds` FreeRTOS state machine to real
   status on the locked stick (§7): FastLED on D10/GPIO9, the per-pixel map, brightness-cap
   default, and the fullness bar + `batteryLimit` mark. Finish the `chargerState`/`chargePortState`
   enum set (idle/unplugged/complete/fault) in this phase; `charging_active`/`open` captured.

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
