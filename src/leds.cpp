// leds — 8-pixel WS2812 status meter. See leds.h.
//
// Behavior — the whole strip is one meter, filled by SoC / charge-target (batteryLimit):
//   link DOWN (offline / re-auth / no data)            -> pixel 0 pulses red, rest off
//   link UP + charging                                  -> leading meter pixel pulses red (climbs), rest off
//   link UP + not charging + range below threshold      -> all pixels flash red together (low-range alert)
//   link UP + not charging + range OK                   -> meter: green filled + red empty (fill = SoC/target)
//   OTA push in progress                                -> whole strip is a blue progress bar
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

// --- enum interpretation. "charging_active" = actively pushing power; everything else
//     (incl. "charging_ready" = plugged-idle) counts as not charging. ⚠️ centralised here. ---
static bool sCharging(const VehicleStatus& v) {
  return v.chargerState == "charging_active";
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

  // Link DOWN (offline / needs re-auth / no telemetry yet): pulse pixel 0 red, rest off.
  if (st.link != 0 || !st.vs.valid) {
    leds[0] = CRGB(u8(30 + 200 * wave(1200, now)), 0, 0);
    return;
  }

  // Link UP — the whole strip is a meter filled by SoC / charge-target.
  const VehicleStatus& v = st.vs;
  float target = (isnan(v.batteryLimit) || v.batteryLimit <= 1.0f) ? 100.0f : v.batteryLimit;
  float soc    = isnan(v.batteryLevel) ? 0.0f : v.batteryLevel;
  float f      = constrain(soc / target, 0.0f, 1.0f);        // fraction of target
  int   n      = (int)lroundf(f * LED_COUNT);                // filled pixels
  if (soc < target - 0.5f && n >= LED_COUNT) n = LED_COUNT - 1;   // "all full" only at target

  // Charging: the leading meter pixel pulses red, everything else off; it climbs as SoC rises.
  if (sCharging(v)) {
    int lead = constrain(n - 1, 0, LED_COUNT - 1);
    leds[lead] = CRGB(u8(40 + 200 * wave(900, now)), 0, 0);
    return;
  }

  // Not charging + range below the low-range threshold: all pixels flash red (low-range alert).
  float miles = isnan(v.distanceToEmpty) ? NAN : v.distanceToEmpty / 1.60934f;
  if (!isnan(miles) && miles < Settings::rangeThresholdMiles()) {
    fill_solid(leds, LED_COUNT, blink(600, now) ? CRGB(185, 0, 0) : CRGB::Black);
    return;
  }

  // Not charging + range OK: meter — green up to SoC/target, red for the empty remainder.
  for (int i = 0; i < LED_COUNT; i++)
    leds[i] = (i < n) ? CRGB(0, 160, 0) : CRGB(130, 0, 0);
}

void ledsBegin() {
  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, LED_COUNT);
  FastLED.setBrightness(Settings::ledBrightness());   // runtime cap from NVS (/config)
  fill_solid(leds, LED_COUNT, CRGB::Black);
  FastLED.show();
}

void ledsLoop() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 20) return;   // ~50 Hz
  last = now;
  FastLED.setBrightness(Settings::ledBrightness());   // pick up /config changes without a reboot
  render();
  FastLED.show();
}

#else   // non-phase3 builds: no LEDs, no FastLED dependency
void ledsBegin() {}
void ledsLoop() {}
#endif
