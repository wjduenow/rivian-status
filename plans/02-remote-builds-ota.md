# Remote builds → scalable OTA

> **Status (2026-07-23): Phase 1 built, not yet exercised on GitHub.** `.github/workflows/firmware.yml`
> + `tools/git_version.py` + `tools/build_manifest.py` exist and are verified locally (a
> secrets-less `pio run -e phase3` compiles clean; `build_manifest.py` emits a valid manifest for
> the real binary). Remaining for Phase 1: push the branch, run `workflow_dispatch` green, then tag
> `v0.1.0` and confirm the Release carries `firmware-status.bin` + `manifest.json`, and flash that
> Release binary via espota as the hardware pass. Phases 2 and 3 are **design only** — nothing
> device-side has changed.

Ported from `sonos-nest`'s `plans/06-scalable-ota.md`, which is shipped and fleet-deployed. That
plan's reasoning applies almost verbatim; this file records what's different for rivian-status and
what the actual state here is. **Read the sonos-nest plan for the full rationale** — especially the
two hardware bugs its Phase 2 hardware pass caught (UI/cache contention during flash writes, and
the `HTTPUpdate` task-WDT starvation), because both are traps this project would hit identically.

## Context — the problem being solved

Today firmware reaches the device exactly one way: **espota push** from this laptop (the `/ota`
skill). The laptop holds the `.bin`, mDNS-discovers the IP, and runs `espota.py`. Two independent
problems hide in that:

1. **Binaries come from a laptop.** Nothing reproducible, nothing provenance-stamped. Before this
   plan the firmware didn't even report a version, so "what's running on the device?" was
   unanswerable without reflashing it.
2. **Distribution is push, manual, and only from here.** Fine for one device on this LAN; nothing
   else.

Phase 1 fixes (1) alone and is worth doing on its own — espota still consumes the binary, it just
comes from a Release instead of `.pio/build/`.

## Differences from sonos-nest

| | sonos-nest | rivian-status |
|---|---|---|
| shipping envs | 3 (`nest`/`sleep-machine`/`sleep-button`) | **1** (`phase3`), matrix kept for later |
| unit id | `nest`/`sleep`/`button` | `status` |
| flash / partitions | 16 MB + 8 MB, explicit `*.csv` | 8 MB, framework `default_8MB.csv` (implicit) |
| fleet coordinator | `sonos-portal/` (Pi/HA add-on) mirrors Releases on the LAN | none — GitHub or nothing |
| device reports version | to the portal via `registrationJson()` | status-page footer only |

The partition point matters and was checked: `pio run -e phase3 -t envdump` resolves to the
framework's `default_8MB.csv` — **app0 + app1, 0x330000 (3.19 MB) each**. The app uses ~1.0 MB, so
there is a genuine second OTA slot with ~3× headroom. A pull-OTA path in Phase 2 is viable without
touching the partition layout.

## Phase 1 — CI builds (GitHub Actions → Releases) — BUILT

**Goal:** a `v*` tag produces a GitHub Release with `firmware-status.bin` + `manifest.json`.
Nothing device-side changes; espota now pulls its `.bin` from a Release instead of a laptop dir.

Three pieces:

### `tools/git_version.py` — provenance

PlatformIO pre-script wired into `[env]` via `extra_scripts`, so every env gets
`-DFW_VERSION="<git describe --tags --always --dirty>"`. Surfaced in the phase3 status-page footer
(`webserver.cpp pageFoot()`), which is the only place a running build's identity is visible over
the network.

The suffixes are the whole point:

| build | FW_VERSION |
|---|---|
| CI, clean tree at tag `v0.1.0` | `v0.1.0` |
| laptop, 3 commits past the tag, uncommitted edits | `v0.1.0-3-gabc1234-dirty` |
| no tags in the repo yet | `abc1234` / `abc1234-dirty` |

Only a CI Release build can report a bare tag, so a later version comparison against a manifest
can't be fooled by an ad-hoc build. This needs full history **and tags** in CI — hence
`fetch-depth: 0` + `fetch-tags: true` in the workflow.

### `.github/workflows/firmware.yml`

- **Triggers:** `push` on `v*` tags (builds + publishes a Release) and `workflow_dispatch` (builds
  + uploads artifacts, **no** Release) so CI can be exercised off a branch before tagging.
- **Only `phase3` is built.** `phase1`/`phase2` deliberately `#error` without `include/secrets.h`,
  and `phase6-ledtest` is a wiring smoke-test — none belong in a Release.
- **Platform pinned via `env.PLATFORM` (`espressif32@7.0.1`)** by pre-installing it, so the
  intentionally-unpinned `platform = espressif32` in `platformio.ini` resolves to the version this
  repo is developed against instead of drifting to latest. Bump both together.
- `~/.platformio` is cached, keyed on `platformio.ini` + the workflow file.

### `tools/build_manifest.py`

Reads `bins/firmware-<unit>.bin`, sha256s each, emits:

```json
{
  "version": "v0.1.0",
  "units": {
    "status": {
      "bin":  "firmware-status.bin",
      "url":  "https://github.com/wjduenow/rivian-status/releases/download/v0.1.0/firmware-status.bin",
      "sha256": "…",
      "size": 1016816
    }
  }
}
```

Unit ids are parsed from filenames, so adding an env to the CI matrix adds a manifest entry with no
change to the script. `sha256`/`size` are advisory — they let a Phase 2 firmware add image
verification without a schema change.

### The secrets.h decision (important, and it required a code change)

**CI builds with no `include/secrets.h` at all.** Not a copy of `secrets.h.example` — copying it
would `#define WIFI_SSID "your-ssid"` and thereby *disable* the first-boot SoftAP portal on every
shipped unit.

That didn't work as-inherited. `sonos-nest` was already secrets-optional; rivian-status was not:
`main.cpp`'s `authenticate()` helper (and its `readSerialLine()`/`printRaw()` companions) sat
*outside* any `#ifdef` even though only the phase1/phase2 serial harnesses call it, so a
secrets-less `phase3` build died on `'RIVIAN_EMAIL' was not declared in this scope`. Those three
helpers are now guarded by `#if defined(PHASE1_SMOKE_TEST) || defined(PHASE2_POLL_LOOP)`. Phase 3
logs in from the browser, so it loses nothing.

What that buys, and what it costs:

- A Release binary bakes **no WiFi creds** → still first-boot provisions via the SoftAP portal.
- It bakes **no `SEED_USESS`** → still requires a browser login (and one email OTP) on a device
  whose NVS is empty. On a device that already has a session in NVS, a reflash keeps NVS, so
  nothing is asked. Same as today.
- It bakes **no `OTA_PASSWORD`** → `net_ota.cpp` emits its "OTA is unauthenticated" `#warning` and
  the espota listener comes up passwordless. **Keep a Release-flashed unit on a trusted LAN**
  (plan 01 §11), or flash a locally-built binary for one that isn't. If that becomes unacceptable,
  the fix is to move the OTA password into NVS/`settings` alongside the WiFi creds rather than to
  inject a secret into CI.

## Phase 2 — device-pull OTA (DESIGN ONLY, not built)

Add `updateUrl` (manifest URL, empty by default) + `otaAuto` (bool) to `settings`/`/config`; a task
periodically GETs the manifest, string-compares `version` against `FW_VERSION`, and either
self-applies (`otaAuto`) or waits for an Approve click. `HTTPUpdate` ships with the Arduino core,
so no new dependency. Empty `updateUrl` = today's espota-only behavior, so this is purely additive.

Two things to port verbatim from sonos-nest's hardware pass, because they were real resets, not
theory:

1. **Yield during the download.** `HTTPUpdate`'s loop never yields; on sonos-nest that starved
   IDLE0 and the task WDT fired ~5 s in. Fix was a `delay(1)` per chunk in `onProgress`. Here the
   `ledsLoop()`/`webAppLoop()` cadence in `loop()` makes this equally likely.
2. **Quiesce other work while flashing.** An `updaterActive()` flag needs to gate the poll task
   (and probably the LED render) the way `otaActive()` already gates things for espota.

Also port `esp_reset_reason()` into the status page — on sonos-nest that's what made a mid-download
reset diagnosable without serial.

Version comparison must be **strictly newer**, not "different" — sonos-nest shipped `v0.1.3`
specifically to kill a downgrade loop where `otaAuto` plus a lagging mirror flip-flopped a device
between two builds.

## Phase 3 — LAN mirror (DESIGN ONLY, likely NOT worth it here)

sonos-nest's portal mirrors Releases so devices never touch GitHub, and gives a fleet dashboard
with per-device approve. With one device and no existing always-on service in this project, that's
a lot of infrastructure for nothing. If it's ever wanted, the cheap version is pointing this
device's `updateUrl` at the **existing sonos-portal** rather than building a second one.

## Non-goals

Signed / secure-boot OTA. LAN, plain HTTP, hobby appliance — same call as plan 01 §11.
