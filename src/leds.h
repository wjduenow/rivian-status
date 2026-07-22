// leds — the Phase 6 status light (plan §7). Renders the shared VehicleStatus + link health
// onto the 8-pixel WS2812 stick (FastLED on D10/GPIO9), one indicator per pixel, under a
// firmware brightness cap. Reads webserver's ledState() snapshot and net_ota's otaActive().
// Compiled into the phase3 app only (see leds.cpp guard); no-op stubs elsewhere.
#pragma once

void ledsBegin();   // FastLED init; call once in setup() after webAppBegin().
void ledsLoop();     // call from loop(): re-renders the 8-pixel map (~50 Hz, self rate-limited).
