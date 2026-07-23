# Battery power for the LED strip — decision

**Question:** can the 8-pixel WS2812 strip run on battery when the board is unplugged from USB?
**Date:** 2026-07-22. **Source:** deep-research (102 agents, 24/25 claims verified vs primary docs).

## TL;DR

- **As built, no.** The strip is fed from the XIAO's `5V` pin, which is **USB-VBUS passthrough** —
  dead on battery. (See `plans/01-…md` §7 wiring note.)
- **No single ESP32-S3 dev board fixes this.** Every mainstream one (Adafruit ESP32-S3 Feather,
  SparkFun Thing Plus ESP32-S3, Unexpected Maker FeatherS3/ProS3/TinyS3/Series[D]) regulates the
  LiPo to **3.3 V only**; their "5V/USB" pin is the same VBUS-passthrough trap. **None has an
  onboard 5 V boost.**
- **Fix = board + external LiPo-charger/5V-boost module.** The **Adafruit PowerBoost 500C/1000C**
  (TI TPS61090) makes a real regulated **~5.2 V** rail from a 3.7 V LiPo at 500–1000 mA+, with
  load-share (charges from USB *and* powers the load simultaneously).
- **Decision: stay USB-powered for the shipped appliance.** Always-on WiFi makes battery runtime
  short (see below), and sleep modes fight the always-on web UI/OTA. Battery is a viable *optional*
  add-on (XIAO + PowerBoost + a chunky LiPo) for portable/backup use, not the default.

## Board comparison (5 V-on-battery is the deciding column)

| Board | 5 V on battery? | Antenna | Charge / gauge | Notes |
|---|---|---|---|---|
| **Seeed XIAO ESP32-S3** (current) | ❌ VBUS-only | external U.FL (fragile in enclosures) | basic LiPo pads | what we have |
| Adafruit ESP32-S3 Feather | ❌ VBUS-only | varies (some U.FL) | MAX17048 gauge | 3.3 V reg only |
| SparkFun Thing Plus ESP32-S3 | ❌ VBUS-only | **onboard PCB** ✔ | MCP73831 (~214 mA) + MAX17048 | same S3 → firmware ports (pin renames); best antenna fix |
| UM FeatherS3 / ProS3 / Series[D] | ❌ VBUS-only | PCB (Series[D] = dual + RF switch) | charger; 3.3 V LDOs | UM docs: bring your own boost |

Primary-source verified: the 5 V rail is boost-backed on **none** of them.

## The runtime reality (why USB stays the default)

Always-on WiFi (no power-save) averages **~90–150 mA** + TX bursts:

| LiPo | No sleep | Modem-sleep (~15–20 mA) | Auto light-sleep (~1–2.5 mA) |
|---|---|---|---|
| 1000 mAh | ~6–10 h | ~2 days | ~2–3 weeks |
| 2000 mAh | ~12–20 h | ~3–4 days | weeks |
| 3500 mAh | ~1–1.5 days | ~1 week+ | weeks |

(LED current adds on top when lit; independent of MCU sleep state.)

**The catch:** modem/light-sleep keeps WiFi *associated* but throttles the radio/CPU, which
conflicts with the appliance's **always-reachable web UI + OTA** — keeping HTTP instantly
responsive forces near-active mode. So it's a genuine tradeoff: a big LiPo for ~half-a-day-to-a-day
of no-sleep runtime, **or** add sleep and accept the web/OTA endpoints only wake on a schedule.

## If we build the battery option

**Parts:** XIAO ESP32-S3 (keep) + **Adafruit PowerBoost 500C** (or 1000C for margin) + a LiPo
(2000–3500 mAh for meaningful runtime).

**Wiring:**
```
LiPo ───────────► PowerBoost (JST)
USB ────────────► PowerBoost micro-USB    (charges LiPo + load-share)
PowerBoost 5V ──► WS2812 strip 5V  AND  XIAO 5V pin   (whole thing runs on battery)
PowerBoost GND ─► common GND (strip + XIAO)
XIAO GPIO9 ─────► strip DIN  (through the ~330 Ω, unchanged)
```
- **Zero firmware change** (still FastLED on D10/GPIO9).
- **Flashing moves to OTA** (already set up — `/ota`), since USB now feeds the PowerBoost, not the
  XIAO's data port. (Or plug USB into the XIAO directly for a one-off USB flash.)
- **Enclosure grows** to house the LiPo + PowerBoost — the current `hardware/` box would need a
  battery bay / a second compartment.
- Optional: swap XIAO → **SparkFun Thing Plus ESP32-S3** to also kill the external-antenna
  fragility (onboard PCB antenna), still with the PowerBoost.

## Open questions (if we pursue it)
- Measured average current of *this* firmware (30 s poll + always-on WiFi + 8 px @ ~40/255) with
  and without modem-sleep — the numbers above are datasheet-derived, not bench-measured.
- Can modem-sleep / DTIM light-sleep coexist with the always-on web UI + OTA, or does always-on
  HTTP force full active mode?
- WS2812 reliability on the PowerBoost's 5.2 V rail with 3.3 V data — level shifter / sacrificial
  pixel needed, or fine at this short length + low brightness? (Plan §7 already flags a 74AHCT125
  as the fallback.)

## Sources
- [Adafruit ESP32-S3 Feather — Power Management](https://learn.adafruit.com/adafruit-esp32-s3-feather/power-management) · [Pinouts](https://learn.adafruit.com/adafruit-esp32-s3-feather/pinouts)
- [Adafruit PowerBoost 1000C](https://learn.adafruit.com/adafruit-powerboost-1000c-load-share-usb-charge-boost/overview) · [PowerBoost 500C (#1944)](https://www.adafruit.com/product/1944)
- [SparkFun Thing Plus ESP32-S3 — hardware overview](https://docs.sparkfun.com/SparkFun_Thing_Plus_ESP32-S3/hardware_overview)
- [Espressif — Low-Power Mode in Wi-Fi (ESP32-S3)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/low-power-mode/low-power-mode-wifi.html)
- [Seeed XIAO ESP32-S3 WiFi/antenna docs](https://wiki.seeedstudio.com/xiao_esp32s3_wifi_usage/) · [enclosure WiFi-loss report](https://forum.seeedstudio.com/t/xiao-esp32s3-antenna-wi-fi-not-working/272978)
- [Unexpected Maker Series[D] (dual antenna, no 5V boost)](https://www.cnx-software.com/2025/07/21/unexpected-maker-launches-seriesd-esp32-s3-boards-with-dual-antenna-software-rf-switch/)
