# Deep-research report — connecting an ESP32 to a Rivian for charge/range status

Question: *Options to connect an ESP32 board to a Rivian vehicle to display/monitor charging
state (charge %, charging status, range, etc.) — cloud API vs local/BLE, auth, libraries,
feasibility on ESP32.*

Method (2026-07-17): fan-out web search across 6 angles → 22 sources fetched → 72 claims
extracted → 25 verified with 3-vote adversarial verification (need 2/3 to kill) → 18 confirmed,
7 refuted → synthesized. For the concrete protocol, see `rivian-cloud-api-protocol.md`.

## Summary

Two programmatic paths exist. The realistic one is **Rivian's cloud GraphQL App API**:
authenticate with a Rivian account (CSRF → email/password login → OTP/MFA when enabled, yielding
`a-sess`/`csrf-token`/`u-sess` session headers) and POST GraphQL to
`https://rivian.com/api/gql/gateway/graphql`, where `GetVehicleState` / `getLiveSessionData`
return state of charge, charger state, power/voltage/amps, time remaining, range added, and
estimated range. A **local BLE API** also exists (same interface the app + keyfob use, direct to
the vehicle, no internet) — but it is **not plug-and-play**: it requires cloud-bootstrapped
phone-key enrollment (registering an ECDH secp256r1 public key server-side) plus cryptographic
pairing before a device gains command authority. Tesla has a working ESP32-BLE project
(`yoziru/esphome-tesla-ble`); **no equivalent turnkey ESP32 Rivian-BLE project exists.** For a
monitor, the cloud API is the fastest route; a managed layer (**Smartcar**) wraps Rivian behind
OAuth owner-consent as an alternative. All Rivian APIs here are unofficial and can change.

## Confirmed findings (high confidence unless noted)

1. **Cloud GraphQL App API is the feasible ESP32 path** — POST to
   `https://rivian.com/api/gql/gateway/graphql`; how the app, community integrations, and Python
   clients all access data. *(3-0)* Sources: RivDocs, home-assistant-rivian,
   bretterer/rivian-python-client, the-mace/rivian-python-api, PyPI.

2. **Auth is a multi-step cloud token flow** — `CreateCSRFToken` (→ csrfToken + appSessionToken)
   → `Login` (email+password) → if MFA, `LoginWithOTP`/`loginWithOTPV2` (OTP) → `userSessionToken`;
   requests carry `Csrf-Token`, `A-Sess`, `U-Sess`. OTP is texted when MFA is on. *(3-0)*

3. **The cloud API returns exactly the needed telemetry** — SoC (`soc`/`batteryLevel`), charger
   state (`vehicleChargerState`/`chargerState`), power/current/voltage, time remaining, energy
   added, range added, estimated range — via `GetVehicleState` + `getLiveSessionData`. HA exposes
   these as ~118 sensor entities. *(3-0)*

4. **A local BLE API exists** — same controls as the app API but sent directly to the vehicle
   over BLE, no internet. Python BLE support lives in `bretterer/rivian-python-client`
   (`src/rivian/ble.py`). *(3-0)*

5. **BLE is NOT plug-and-play** — phone-key enrollment is bootstrapped through a cloud web API
   (`EnrollPhone` GraphQL mutation registering an ECDH secp256r1 public key), required before BLE
   pairing; subsequent commands are HMAC-signed locally. This is the main hurdle for a local
   ESP32 implementation, and no turnkey Rivian-BLE ESP32 project was found. *(3-0)*

6. **Even "BLE-based" HA integrations use BLE only for one-time pairing**, then do all ongoing
   monitoring via the Rivian cloud ("After pairing, Bluetooth is no longer required"). So for a
   monitor-only ESP32, the cloud API alone suffices — no BLE needed. *(3-0)*

7. **Smartcar is a managed cloud alternative** — third-party API covering Rivian EV charging
   management, location, mileage, via OAuth owner-consent (owner logs in with their Rivian
   account); Smartcar handles credentials so passwords don't hit your servers. Cleaner auth for
   an ESP32 (standard OAuth) at the cost of a third-party, likely-paid dependency. *(3-0)*

8. **Tesla precedent shows direct ESP32↔vehicle BLE is possible in principle** —
   `yoziru/esphome-tesla-ble` controls charging + reads battery status over BLE, no cloud
   (Tesla's VCSEC always advertises over BLE). But it's Tesla-specific; Rivian would need its own
   enrollment/crypto ported. *(3-0)* Note: verifiers **refuted** the specific claim that the
   Tesla BLE project reads live charge-rate/energy telemetry (1-2) — treat as control+basic-status
   precedent, not confirmed live telemetry.

## Refuted claims (sounded true, failed verification — recorded so we don't re-adopt them)

- "Live charging state is delivered via an App API 'Parallax' streaming domain with fields like
  `charging.session.status`/`soc_slider`/`time_estimation`." **0-3** — not supported; the real
  fields are the `vehicleState`/live-session ones above.
- "Rivian data is accessed *exclusively* through the cloud (no local/BLE path)." **0-3** — false;
  a BLE path does exist (finding 4), it's just hard.
- "Auth uses token-based auth where the password is never stored / only encrypted tokens
  returned." **0-3** — overstated marketing framing from a secondary source; the actual flow is
  as in finding 2.
- "The rivian-python-client is cloud-only with no BLE transport." **0-3** — false; it ships
  `ble.py`.
- (A the-mace-sourced restatement of the auth claim scored 1-2, but the same claim from stronger
  sources confirmed 3-0 — see finding 2.)

## Open questions (still unresolved after research)

1. **Token/refresh lifetime** and whether an ESP32 can silently refresh without re-triggering an
   OTP to the owner's phone (critical for an unattended monitor). *Partially answered by the
   protocol dig: there's no refresh mutation; re-run CreateCSRFToken on expiry reusing `u-sess`;
   lifetimes remain undocumented.*
2. Is the full Rivian **BLE pairing + HMAC command-signing** handshake documented well enough to
   port to ESP32 C++, and has anyone built a non-app Rivian BLE client? *(Not for our read-only
   monitor — moot unless control is ever wanted.)*
3. Does the ESP32 TLS/HTTPS stack + heap comfortably handle the endpoint? *Answered: yes,
   especially with no display — see protocol doc §5.*
4. Smartcar pricing/rate-limit tier for a single-vehicle hobby project, and whether its OAuth
   consent redirect fits a headless device.

## Caveats

All Rivian API details come from unofficial, reverse-engineered community sources (RivDocs /
rivian-api.kaedenb.org, edman007.com, the Python/HA libraries) — no official API, so endpoints,
auth, and field names can change without notice. The cloud path is well-corroborated by multiple
primary sources (client library source code) and is the low-risk choice. The BLE path is real but
under-documented for third parties, requires a cloud enrollment step, and has no ESP32 precedent
for Rivian. Verifiers specifically refuted that Tesla's ESP32 BLE project reads live charge-rate
telemetry, so don't assume Rivian BLE would expose full live charging telemetry either. Smartcar
is a commercial dependency. MFA/OTP handling on a headless ESP32 is a practical wrinkle (the OTP
is texted at login) — the token refresh/re-auth strategy matters. Rivian Gen2 vehicles add UWB
and have integration caveats noted in the HA project.

## Sources (with quality rating)

| URL | Quality | Angle |
|---|---|---|
| https://rivian-api.kaedenb.org/ | blog | Rivian data access options |
| https://rivian-api.kaedenb.org/app/charging | blog | Rivian data access options |
| https://rivian-api.kaedenb.org/app/authentication/ | secondary | cloud API / auth |
| https://rivian-api.kaedenb.org/ble/ | blog | BLE |
| https://rivian-api.kaedenb.org/ble/enroll/ | blog | BLE enrollment |
| https://github.com/bretterer/rivian-python-client | primary | cloud client (the maintained one) |
| https://github.com/bretterer/home-assistant-rivian | primary | HA integration |
| https://pypi.org/project/rivian-python-client/ | primary | cloud client |
| https://github.com/the-mace/rivian-python-api | primary | cloud client + CLI |
| https://smartcar.com/brand/rivian | primary | managed OAuth alternative |
| https://github.com/yoziru/esphome-tesla-ble | primary | Tesla ESP32-BLE precedent |
| https://github.com/OpenEVSE/openevse_esp32_firmware/issues/984 | forum | ESP32 Rivian feature request |
| https://github.com/fhessel/esp32_https_server/issues/11 | forum | ESP32 TLS heap (~37 KB/conn) |
| https://github.com/Mbed-TLS/mbedtls/issues/3119 | forum | mbedTLS handshake leak |
| https://github.com/espressif/arduino-esp32/issues/2175 | forum | BLE+HTTPS heap starvation |
| https://rivianroamer.com/help | secondary | (source of two refuted claims) |

## Stats
6 angles · 22 sources fetched · 72 claims extracted · 25 verified · 18 confirmed / 7 refuted ·
8 findings after synthesis · 105 agent calls.
