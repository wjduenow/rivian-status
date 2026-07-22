// webserver — the config/status/login UI (plan §9), plus the background poll task that keeps a
// shared VehicleStatus snapshot fresh. Single WebServer on port 80; small form POSTs only, so
// one port is enough (§9). Concurrency (plan §8): the poll runs in its own FreeRTOS task and the
// web handlers run in loop()'s context; a mutex guards all rivian_api network calls (they share
// one TLS client) and another guards the snapshot.
#pragma once

#include <Arduino.h>
#include "rivian_api.h"

void webAppBegin();   // start mDNS + HTTP server + poll task. Call after WiFi is up, system time
                      // is set, RivianApi::begin() and Settings::begin() have run.
void webAppLoop();     // call from loop(): services the HTTP server.

// Snapshot for the LED module (plan §7): the latest poll + link health, mutex-copied.
struct LedState {
  int      link;     // 0 = OK, 1 = re-auth needed, 2 = offline
  bool     everOk;   // any successful poll since boot
  uint32_t ageMs;    // ms since the last good poll (0 if never)
  VehicleStatus vs;  // latest telemetry (vs.valid == false until the first poll)
};
LedState ledState();
