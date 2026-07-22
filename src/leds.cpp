// leds — 8-pixel WS2812 status map (plan §7). See leds.h.
//
// Pixel map:
//   0     link health     green heartbeat = OK · amber = re-auth · red = offline
//   1     charging        off = idle · green pulse = charging · steady = at target · red-blink = fault
//   2     plug state      dim white = plugged · off = unplugged
//   3     low-range       steady red = low & not charging · slow red pulse = low but charging · off
//   4-7   fullness bar    4 cells (25/50/75/100 %) lit to SoC, red→amber→green; the cell holding
//                         batteryLimit is tinted blue so a 70 % cap vs a 100 % charge reads at a glance
//
// While an OTA push runs, the whole strip becomes a blue progress bar (plan §8 cue).
#ifdef PHASE3_WEBAPP

#include "leds.h"
#include <FastLED.h>
#include <math.h>

#include "webserver.h"
#include "settings.h"
#include "net_ota.h"

#ifndef LED_DATA_PIN
#define LED_DATA_PIN 9          // D10 = GPIO9 on the XIAO ESP32-S3
#endif
#ifndef LED_COUNT
#define LED_COUNT 8
#endif
#ifndef LED_BRIGHTNESS
#define LED_BRIGHTNESS 40       // conservative cap — single-supply off USB VBUS (plan §7)
#endif

static CRGB leds[LED_COUNT];

// --- enum interpretation (plan §7). chargerState values seen so far: "charging_active"
//     (pushing power) and "charging_ready" (plugged, idle). Others (unplugged/complete/fault)
//     still to be captured — centralised here so they're a one-line fix. ---
static bool sCharging(const VehicleStatus& v) {
  return v.chargerState == "charging_active";              // actively charging (Phase 1)
}
static bool sPlugged(const VehicleStatus& v) {
  // Both observed plugged-in states start with "charging" (charging_ready / charging_active);
  // unplugged states won't. More reliable than chargePortState, whose "open"/"close" values
  // are ambiguous. ⚠️ verify against the real unplugged value when captured.
  return v.chargerState.startsWith("charging");
}
static bool sFault(const VehicleStatus& v) {
  String s = v.chargerState; s.toLowerCase();
  return s.indexOf("fault") >= 0 || s.indexOf("error") >= 0;   // ⚠️ exact value TBD
}
static bool sAtTarget(const VehicleStatus& v) {
  return !isnan(v.batteryLevel) && !isnan(v.batteryLimit) && v.batteryLevel >= v.batteryLimit - 0.5f;
}

// --- animation helpers ---
static float wave(uint16_t periodMs, uint32_t now) {   // smooth 0..1..0 sine
  float ph = (now % periodMs) / (float)periodMs;
  return 0.5f - 0.5f * cosf(ph * 2.0f * PI);
}
static bool blink(uint16_t periodMs, uint32_t now) { return (now % periodMs) < (periodMs / 2); }
static uint8_t u8(float f) { return (uint8_t)(f < 0 ? 0 : f > 255 ? 255 : f); }

static void render() {
  const uint32_t now = millis();
  fill_solid(leds, LED_COUNT, CRGB::Black);

  // OTA push takes over the whole strip: a blue progress bar with a moving highlight.
  if (otaActive()) {
    int p = otaProgress();                              // 0..100, or -1 if unknown
    int lit = (p < 0) ? LED_COUNT : (p * LED_COUNT + 50) / 100;
    for (int i = 0; i < LED_COUNT; i++) leds[i] = (i < lit) ? CRGB(0, 0, 180) : CRGB(0, 0, 12);
    leds[(now / 120) % LED_COUNT] += CRGB(0, 0, 60);
    return;
  }

  const LedState st = ledState();
  const VehicleStatus& v = st.vs;

  // 0 — link health
  if      (st.link == 0) leds[0] = CRGB(0, u8(40 + 215 * wave(1600, now)), 0);   // green heartbeat
  else if (st.link == 1) leds[0] = CRGB(255, 120, 0);                            // amber = re-auth
  else                   leds[0] = CRGB(160, 0, 0);                              // red = offline

  if (!v.valid) return;   // no telemetry yet — only the link pixel is meaningful

  // 1 — charging activity
  if      (sFault(v))     leds[1] = blink(500, now) ? CRGB(190, 0, 0) : CRGB::Black;     // fault red-blink
  else if (sCharging(v))  leds[1] = CRGB(0, u8(30 + 225 * wave(1400, now)), 15);         // charging pulse
  else if (sAtTarget(v))  leds[1] = CRGB(0, 130, 0);                                     // at target steady
  // else idle -> off

  // 2 — plug state
  if (sPlugged(v)) leds[2] = CRGB(45, 45, 45);            // dim white

  // 3 — low-range alert
  float miles = isnan(v.distanceToEmpty) ? NAN : v.distanceToEmpty / 1.60934f;
  bool  low   = !isnan(miles) && miles < Settings::rangeThresholdMiles();
  if      (low && sCharging(v)) leds[3] = CRGB(u8(30 + 170 * wave(2200, now)), 0, 0);    // low but charging
  else if (low)                 leds[3] = CRGB(185, 0, 0);                               // low & not charging

  // 4-7 — fullness bar + charge-target mark
  float soc      = isnan(v.batteryLevel) ? 0.0f : v.batteryLevel;
  CRGB  barColor = CHSV((uint8_t)map((long)soc, 0, 100, 0, 96), 255, 255);   // red→amber→green by SoC
  int   limitCell = isnan(v.batteryLimit) ? -1 : constrain((int)(v.batteryLimit / 25.0f), 0, 3);
  for (int i = 0; i < 4; i++) {
    float frac = constrain((soc - i * 25.0f) / 25.0f, 0.0f, 1.0f);   // fill of this cell
    CRGB c = barColor;
    c.nscale8_video(u8(frac * 255));                                 // partial cell dims; empty = off
    if (i == limitCell) c = (frac > 0) ? (c + CRGB(0, 0, 90)) : CRGB(0, 0, 30);   // target tint / dot
    leds[4 + i] = c;
  }
}

void ledsBegin() {
  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, LED_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);
  fill_solid(leds, LED_COUNT, CRGB::Black);
  FastLED.show();
}

void ledsLoop() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 20) return;   // ~50 Hz
  last = now;
  render();
  FastLED.show();
}

#else   // non-phase3 builds: no LEDs, no FastLED dependency
void ledsBegin() {}
void ledsLoop() {}
#endif
