// net_updater — firmware self-update over HTTP: the device *pull* path (plan 02 § Phase 2).
//
// Complements the espota *push* path in net_ota.*. Instead of a laptop pushing a binary at the
// device, the device fetches `manifest.json` from a URL, and if a strictly-newer build is
// published for this unit it flashes itself via the core's HTTPUpdate into the spare OTA slot.
// The 8 MB layout gives app0/app1 3.19 MB each against a ~1.0 MB app, so there is a real second
// slot; a failed or partial download leaves the running firmware untouched.
//
// Ported from sonos-nest src/core/net/updater.cpp, which is fleet-deployed — read that file and
// its plans/06-scalable-ota.md before changing the download or version-compare logic. Two of its
// behaviors exist because of real resets on hardware, not theory (the WDT yield and the quiesce
// window); both are reproduced in applyNow().
//
// Dormant unless configured: an empty updateUrl means AUTO (the compiled-in GitHub default), the
// literal "off" disables checking entirely. Applying is separate from checking — availability is
// always reported so the status page can show it, but nothing self-flashes unless otaAuto is on
// or someone clicks "Update now".
//
// DIVERGENCE from sonos-nest, deliberate: sonos-nest auto-applies only at BOOT, so it can never
// yank firmware out from under a playing sleep-machine. This unit is a status light with nothing
// to interrupt, and it can run for months without a reboot — boot-only would mean auto-update
// almost never fires. So an otaAuto device here also applies on the periodic check.
#pragma once

#include <Arduino.h>

void   updaterBegin();             // one check at boot (call after WiFi is up)
void   updaterTick();              // periodic check; self-rate-limited (~6 h) — call from pollTask
bool   updaterActive();            // true while a pull-flash runs (LEDs show it, espota backs off)
int    updaterProgress();          // 0-100 during a pull-flash, else -1
bool   updaterAvailable();         // a strictly-newer build is published for this unit
String updaterAvailableVersion();  // that version, or "" when up-to-date / disabled
String updaterLastError();         // last failure, for the status page ("" if none)
void   updaterApprove();           // arm an immediate apply ("Update now" on /config)
void   updaterForceCheck();        // re-check on the next tick, bypassing the rate limit

// Effective manifest URL after resolving the stored setting: an explicit URL wins, the literal
// "off" disables (returns ""), empty means AUTO (the compiled-in GitHub latest-release default).
String      updaterEffectiveUrl();
const char* updaterSourceKind();   // "github" | "custom" | "off" — for the config UI
