# CLAUDE.md — rivian-status

Headless **ESP32-S3 Rivian charge/range status light**. Polls the owner's Rivian over WiFi via the
unofficial cloud GraphQL API and shows charge/plug/range/link-health on LEDs. All setup (Rivian
login incl. MFA, WiFi, threshold) happens in a browser — no screen.

**Full design + rationale: `plans/01-rivian-status-plan.md` (the source of truth — read it).**
**API research: `research/` (deep-research report + reverse-engineered protocol dig).**

## Status (2026-07-22)
Phases **1–6 DONE and verified live** against the real vehicle. Phase 6 (LEDs) is implemented
in the `leds` module and running in the shipped `phase3` app; remaining polish is visual tuning
and capturing the last few `chargerState` enum values (unplugged/complete/fault).

| Phase | What | State |
|---|---|---|
| 1 | Auth smoke-test (CSRF→Login→OTP→getUserInfo→getVehicleState) | ✅ |
| 2 | Poll loop (30 s, backoff, lowRange, km→mi) | ✅ |
| 3 | Web UI (`/` status, `/login`, `/config`) + poll task | ✅ |
| 4 | WiFi provisioning (SoftAP captive portal) + DHCP hostname = device name | ✅ |
| 5 | OTA wireless updates (ArduinoOTA) | ✅ |
| 6 | **LEDs** — 8-pixel WS2812 stick, FastLED on D10/GPIO9 (`leds` module) | ✅ live |

**The shipped appliance = the `phase3` env.** `phase1`/`phase2` are serial-only diagnostic
harnesses (they hard-code creds in `secrets.h` and print to serial). New product features go in the
`phase3` path + the shared modules.

## Hardware
- **Board:** Seeed Studio XIAO ESP32-S3 (native USB-C). **The external U.FL antenna MUST be
  plugged in** or WiFi fails with `AUTH_EXPIRE`/`HANDSHAKE_TIMEOUT` (looks like a bad password).
- **LEDs (Phase 6):** 8-pixel WS2812/NeoPixel stick. DIN←`D10`/GPIO9 (~330 Ω), 5V←`5V` pin (USB
  VBUS), GND←GND. Single-supply, safe behind a firmware brightness cap. Pixel map in plan §7.

## Modules (`src/`)
- `rivian_api.{h,cpp}` — **the ONLY file that knows Rivian's URLs/headers/GraphQL** (plan §8).
  Read-only telemetry; persists `u-sess`+`dc-cid` to NVS; reuses the session on boot (no OTP).
- `settings.{h,cpp}` — NVS `cfg`: range threshold (miles), device name (= hostname), WiFi creds.
- `net_wifi.{h,cpp}` — connect (hostname before STA transition!), runtime creds, SoftAP portal.
- `net_ota.{h,cpp}` — ArduinoOTA as `<device name>.local`.
- `webserver.{h,cpp}` — WebServer:80 + the **FreeRTOS poll task** + shared snapshot (mutex-guarded);
  exposes `ledState()` for the LED map.
- `leds.{h,cpp}` — **Phase 6** 8-pixel WS2812 status map (§7) via FastLED on D10/GPIO9; reads
  `ledState()` + `otaActive()`; brightness-capped. Built into `phase3` only (stubs elsewhere). All
  Rivian enum reads (`sCharging`/`sPlugged`/`sFault`) are centralized here.
- `main.cpp` — orchestration, split by `#ifdef PHASE1_SMOKE_TEST / PHASE2_POLL_LOOP / PHASE3_WEBAPP`.
  `ledtest.cpp` is a standalone WS2812 wiring smoke-test (`-DPHASE6_LEDTEST`, its own env).

**Concurrency (plan §8):** the poll runs in its own FreeRTOS task; web handlers run in `loop()`.
`s_apiLock` serializes all `rivian_api` calls (one shared TLS client); `s_stateLock` guards the
snapshot; `s_loginActive` pauses polling across a 2-step MFA login.

## Build / flash / run
Envs: `phase1`, `phase2`, `phase3` (the app, incl. LEDs), `phase3-ota` (wireless upload),
`phase6-ledtest` (standalone WS2812 wiring smoke-test on D10/GPIO9).
```bash
pio run -e phase3                                  # build
pio run -e phase3 -t upload --upload-port /dev/ttyACM0   # USB flash
OTA_PASSWORD=<pw> pio run -e phase3-ota -t upload --upload-port <device-ip>  # wireless flash
```
**Serial capture in WSL** (the `pio device monitor` OTP-typing + upload-reset race is a pain — use
this instead):
```bash
# reset the board so setup() re-runs with the reader attached, then cat the port
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 --port /dev/ttyACM0 --before default_reset --after hard_reset flash_id
stty -F /dev/ttyACM0 115200 raw -echo; cat /dev/ttyACM0   # send input: printf 'x\n' > /dev/ttyACM0
```
- Attach the board into WSL first: `usbipd attach --wsl --busid <id>` (Windows side). Shows as
  `/dev/ttyACM0`, VID `303A:1001`.
- **Free the port with `fuser -k /dev/ttyACM0`**, never `pkill -f "cat /dev/ttyACM0"` (that kills
  your own shell → exit 144).
- Live-test the app from the host: `curl http://<device-ip>/` (currently `192.168.68.85`).

## Dev workflow essentials (these cost real time — see [[rivian-hardware-flashing]])
- **No OTP on re-image.** `u-sess` persists to NVS and is reused on boot; a normal reflash keeps
  NVS, so re-imaging never needs an OTP. `secrets.h` carries a standing `SEED_USESS` that auto-seeds
  NVS if it's empty (fresh board / `erase_flash`). **Don't `erase_flash` casually.** OTP is needed
  ONLY when the token expires — then do one login and paste the `#define SEED_USESS ...` line the
  firmware prints back into `secrets.h`.
- **MFA OTP arrives by EMAIL**, and rapid repeated logins get throttled (`INVALID_OTP_TOKEN`) —
  don't hammer login; rely on session reuse.
- Explore API fields **from the laptop** with the persisted `u-sess` (curl CreateCSRFToken →
  getVehicleState). Faster than reflashing. Example lives in git history / ask.

## Unofficial API — confirmed facts (all centralized in `rivian_api.cpp`)
- Endpoint `https://rivian.com/api/gql/gateway/graphql`; plain `WiFiClientSecure` POST works
  (CloudFront, Amazon Root CA 1 pinned). Read-only ⇒ no HMAC/BLE.
- `vehicleState(id:)` takes the **vehicle `id`** (e.g. `01-…`), **NOT the VIN** (VIN → `NOT_FOUND`).
- `batteryLevel` is a **float**; `distanceToEmpty` is **kilometers** (US app shows miles; firmware
  ÷1.60934). `batteryLimit` = charge target %. Enums seen: `chargerState="charging_active"`,
  `chargePortState="open"` (more to capture in Phase 6).

## How to add a feature
- **New telemetry field:** add to `VehicleStatus` (`rivian_api.h`) → request+parse in `pollWith()`
  (`rivian_api.cpp`, parse floats with `| (float)NAN`, not `| -1`) → surface in the status page
  (`webserver.cpp handleStatus`) and/or the LED map.
- **New config option:** add to `settings.{h,cpp}` (NVS) → add a field to `/config`
  (`webserver.cpp handleConfigGet/Post`). A device-name change must reboot (hostname/mDNS/AP).
- **Tweak the LED map:** edit `leds.cpp render()` (per-pixel behavior, §7) or the enum readers
  `sCharging`/`sPlugged`/`sFault`; pins/count/brightness are `phase3` build flags. Wiring smoke-test:
  `pio run -e phase6-ledtest -t upload`.

## Secrets (`include/secrets.h`, gitignored — template `secrets.h.example`)
`WIFI_SSID/PASS` (dev fallback), `RIVIAN_EMAIL/PASSWORD` (phase1/2 only — never shipped),
`SEED_USESS` (session recovery seed), `OTA_PASSWORD` (must match the upload env var).

## Security (plan §11 — before "shipping")
Tokens + WiFi pass sit in **unencrypted NVS**; the config page is **plain HTTP, no auth**. Enable
NVS encryption and keep the device on a trusted LAN. Fine for a hobby appliance as-is.

## Conventions
- **Plan is source of truth** — update `plans/01-…md` when decisions/findings change.
- Commit trailers: `Co-Authored-By: Claude …` + `Claude-Session: …` (see git log).
- Remote `origin` = github.com/wjduenow/rivian-status; `gh auth setup-git` already configured.
