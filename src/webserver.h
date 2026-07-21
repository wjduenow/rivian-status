// webserver — the config/status/login UI (plan §9), plus the background poll task that keeps a
// shared VehicleStatus snapshot fresh. Single WebServer on port 80; small form POSTs only, so
// one port is enough (§9). Concurrency (plan §8): the poll runs in its own FreeRTOS task and the
// web handlers run in loop()'s context; a mutex guards all rivian_api network calls (they share
// one TLS client) and another guards the snapshot.
#pragma once

#include <Arduino.h>

void webAppBegin();   // start mDNS + HTTP server + poll task. Call after WiFi is up, system time
                      // is set, RivianApi::begin() and Settings::begin() have run.
void webAppLoop();     // call from loop(): services the HTTP server.
