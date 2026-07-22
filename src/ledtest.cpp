// rivian-status — Phase 6 LED WIRING SMOKE-TEST (plan §7).
//
// Standalone: drives the 8-pixel WS2812 stick on D10/GPIO9 with FastLED. No WiFi, no
// Rivian — it just proves the DIN wiring, pixel order, and USB power before the real
// `leds` module is built. The phase6-ledtest env compiles ONLY this file.
//
//   pio run -e phase6-ledtest -t upload --upload-port /dev/ttyACM0
//
// Reflash the appliance afterwards with:  pio run -e phase3 -t upload --upload-port /dev/ttyACM0
// (NVS/session persist, so no re-login.)
#ifdef PHASE6_LEDTEST

#include <Arduino.h>
#include <FastLED.h>

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

void setup() {
  Serial.begin(115200);
  delay(2000);                  // let USB-CDC enumerate before the first prints
  Serial.println("\n\n### rivian-status — Phase 6 LED smoke-test ###");
  Serial.printf("%d WS2812 pixels on D10/GPIO%d, brightness cap %d/255\n",
                LED_COUNT, LED_DATA_PIN, LED_BRIGHTNESS);

  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, LED_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);

  // Startup wipe: a single white pixel walks the strip so you can count the pixels and
  // confirm which end is pixel 0 (the DIN end).
  for (int i = 0; i < LED_COUNT; i++) {
    fill_solid(leds, LED_COUNT, CRGB::Black);
    leds[i] = CRGB::White;
    FastLED.show();
    Serial.printf("  pixel %d lit\n", i);
    delay(250);
  }
  Serial.println("wipe done — entering rolling rainbow");
}

void loop() {
  // Rolling rainbow so every pixel and all three color channels get exercised.
  static uint8_t hue = 0;
  fill_rainbow(leds, LED_COUNT, hue++, 256 / LED_COUNT);
  FastLED.show();
  delay(40);
}

#endif  // PHASE6_LEDTEST
