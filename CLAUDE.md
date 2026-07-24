# CLAUDE.md — rivian-status

Headless **ESP32-S3 Rivian charge/range status light**. Polls the owner's Rivian over WiFi via the
unofficial cloud GraphQL API and shows charge/plug/range/link-health on LEDs. All setup (Rivian
login incl. MFA, WiFi, threshold) happens in a browser — no screen.

**Full design + rationale: `plans/01-rivian-status-plan.md` (the source of truth — read it).**
**CI builds / OTA roadmap: `plans/02-remote-builds-ota.md`.**
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

## Remote builds (CI) — `plans/02-remote-builds-ota.md`
Ported from `sonos-nest` (whose `plans/06-scalable-ota.md` is the fuller rationale — read it before
extending this). **Phase 1 only: binaries come from GitHub Actions instead of a laptop. Nothing
device-side pulls anything yet.**
- `.github/workflows/firmware.yml` — `v*` tag → Release with `firmware-status.bin` +
  `manifest.json`; `workflow_dispatch` → artifacts only (test CI off a branch). Builds **only
  `phase3`**. Pins the platform via `env.PLATFORM` (`espressif32@7.0.1`) — **keep it in sync with
  the locally-installed platform**, since `platformio.ini` leaves `platform` unpinned on purpose.
- `tools/git_version.py` — `[env] extra_scripts` pre-script; injects
  `FW_VERSION = git describe --tags --always --dirty`, shown in the status-page footer. A clean CI
  tag build is the only thing that reports a bare `v0.1.0`; laptop builds carry `-dirty`/`-N-g…`.
- `tools/build_manifest.py` — sha256s the bins into `{version, units:{status:{url,sha256,size}}}`.
- **CI builds with NO `include/secrets.h`** — that's what keeps the SoftAP portal alive and bakes no
  creds into a public binary. **Never add one to CI**, and don't let a `phase3`-reachable code path
  reference `RIVIAN_EMAIL`/`WIFI_SSID`/`SEED_USESS` unguarded (that's why `main.cpp`'s
  `authenticate()`/`readSerialLine()`/`printRaw()` are `#if defined(PHASE1…||PHASE2…)`).
  Consequence: a Release binary has **no `OTA_PASSWORD`** → its espota listener is unauthenticated.
  Trusted LAN only, or flash a locally-built binary.
- Flash a Release build the normal way: download the asset, then espota it (`/ota` skill).

## Hardware
- **Board:** Seeed Studio XIAO ESP32-S3 (native USB-C). **The external U.FL antenna MUST be
  plugged in** or WiFi fails with `AUTH_EXPIRE`/`HANDSHAKE_TIMEOUT` (looks like a bad password).
- **LEDs (Phase 6):** 8-pixel WS2812/NeoPixel stick. DIN←`D10`/GPIO9 (~330 Ω), 5V←`5V` pin (USB
  VBUS), GND←GND. Single-supply, safe behind a firmware brightness cap. Pixel map in plan §7.

## Enclosures (`hardware/`) — 3D-printed, **two versions**
Parametric Python CSG (**trimesh + manifold3d**, NOT OpenSCAD) in the **`img23d` conda env**.
Each version folder holds `case_params.py` (single source of truth, provenance-commented) →
`build_*.py` (each `assert`s its own clearances, so an illegal edit fails loudly instead of
writing a broken STL) → `.stl`, plus a `render_preview.py`.
```bash
cd hardware/status-light/<box|box-v2> && conda run -n img23d python build_all.py
```
| folder | what | printed parts |
|---|---|---|
| `hardware/status-light/box/` | **v1** top-bar desk box; LEDs face **up** through a lid window | `shell.stl` + `lid.stl` |
| `hardware/status-light/box-v2/` | **v2** slip case that press-fits over a **Nekmit flat wall charger**; LEDs face **out** the front | `case.stl` + `cover.stl` |

- **v2 is the newer work — read [`box-v2/NOTES.md`](hardware/status-light/box-v2/NOTES.md) first.**
  It carries the design reasoning a session would otherwise re-derive: why it protrudes ~29 mm
  (charger + screw-head relief gap + stick), why hidden-M3 stick screws force that depth, the
  XIAO-rests-on-the-skirt-floor + separate screw-on skirt cover (so the body prints open-backed,
  no supports), the skirt height being floored by the USB-A pigtail (~15 mm) not the XIAO, and
  the print orientation (front-face-down).
- v1 dimension table: `box/MEASUREMENTS.md`. Shared XIAO mechanical spec (both versions):
  `status-light/board_spec.md`. Vendor CAD: `status-light/ref/`.
- **Keep `hardware/` commits separate from firmware** — this is user-owned mechanical work.

## Modules (`src/`)
- `rivian_api.{h,cpp}` — **the ONLY file that knows Rivian's URLs/headers/GraphQL** (plan §8).
  Read-only telemetry; persists `u-sess`+`dc-cid` to NVS; reuses the session on boot (no OTP).
- `settings.{h,cpp}` — NVS `cfg`: range threshold (miles), LED brightness, mounting (`led_rot`
  0/90/180/270 = where the plug exits, + `led_vert0` enclosure axis and `led_inv` — see plan §7;
  `ledFlipped()`/`ledVertical()` derive from all three via `pixel0Dir()`), device name
  (= hostname), WiFi creds.
- `net_wifi.{h,cpp}` — connect (hostname before STA transition!), runtime creds, SoftAP portal,
  `reconnect()` for the supervisor. Connect also persists compiled-in creds to NVS so a **CI
  Release binary (no `secrets.h`) doesn't strand the unit in the setup portal**.
- `net_ota.{h,cpp}` — ArduinoOTA as `<device name>.local`; `otaRestart()` re-advertises after a
  WiFi reconnect (the responder dies with the netif — otherwise `.local` discovery stays broken).
- `net_updater.{h,cpp}` — **pull-OTA** (plan 02 §2): GETs `manifest.json`, and if a *strictly
  newer* build is published, self-flashes via `HTTPUpdate` into the spare OTA slot. Checked at boot
  + every 6 h from the poll task; applies only with `otaAuto` or the `/config` button. **Don't
  touch `parseVer()`'s field-terminator check** — without it a bare-hash `FW_VERSION` parses as a
  huge version and the device never updates (see plan 02).
- `webserver.{h,cpp}` — WebServer:80 + the **FreeRTOS poll task** + shared snapshot (mutex-guarded);
  exposes `ledState()` for the LED map. Hosts the **WiFi supervisor** (`sleepSupervised()`): all
  poll sleeps run in 500 ms slices, re-kicking the link every 10 s while down and clearing the
  backoff on recovery — without it the 900 s backoff cap leaves the light stale for 15 min after
  the network returns. See plan §8 "Uptime hygiene".
- `leds.{h,cpp}` — **Phase 6** 8-pixel WS2812 status map (§7) via FastLED on D10/GPIO9; reads
  `ledState()` + `otaActive()`; brightness-capped. Built into `phase3` only (stubs elsewhere). All
  Rivian enum reads (`sCharging`/`sPlugged`/`sFault`) are centralized here. `render()` always draws
  in **logical** order; `applyOrientation()` reverses the buffer afterwards per the `/config`
  mounting rotation, so every pattern (incl. the OTA bar) follows the mounting without knowing
  about it. **Invariant: the meter always fills up (vertical) / right (horizontal)** — plan §7.
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
Wireless flashing is packaged as the **`/ota`** skill (`.claude/skills/ota/`) — build phase3,
mDNS-discover the IP (`find_device.py`, since WSL can't resolve `.local`), espota-upload, verify.
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
