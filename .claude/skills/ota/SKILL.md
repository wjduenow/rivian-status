---
name: ota
description: Flash rivian-status firmware to the running device over WiFi (ArduinoOTA), no USB/usbipd cable. Use when the user wants to deploy/update/flash the ESP32 firmware wirelessly — e.g. "OTA", "update OTA", "flash over wifi", "push this build to the device", "deploy the firmware".
---

# OTA flash — rivian-status

Push firmware to the running **rivian-status** unit over WiFi instead of USB. The device
advertises as `rivian-status` (its **device name**, set on `/config`; the mDNS/OTA name follows
it) and listens for ArduinoOTA on UDP **3232**. A failed/partial OTA is harmless — the running
firmware is only overwritten after a transfer completes 100%. During the push the LED strip
shows a **blue progress bar** (the `otaActive()` cue) — a handy live confirmation.

Only the **phase3** app supports OTA (it runs `otaBegin()`); phase1/2/ledtest do not.

## Step 0 — check the WSL2 firewall (WSL host only)

OTA has the device connect **back** to the build host to push data. On WSL2 with mirrored
networking, inbound to WSL can be blocked by the Hyper-V firewall, which makes uploads hang at
0%. If the upload hangs, test it (WSL can query Windows via `powershell.exe`):

```bash
powershell.exe -NoProfile -Command "(Get-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}').DefaultInboundAction" 2>/dev/null | tr -d '\r'
```

- **`Allow`** → inbound is open, proceed. (This session's host was already `Allow` — OTA worked.)
- **`Block`** / nothing → have the user run this **once** in an **Administrator PowerShell**:
  ```powershell
  Set-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -DefaultInboundAction Allow
  ```

Skip this step on native Linux/macOS.

## Procedure

1. **Build** the app firmware:
   ```bash
   ~/.platformio/penv/bin/pio run -e phase3
   ```

2. **Find the device IP.** WSL2 has no mDNS resolver (`ping rivian-status.local` fails), so
   query the LAN directly with the bundled discovery script:
   ```bash
   python3 -c "import zeroconf" 2>/dev/null || pip install zeroconf --quiet
   python3 .claude/skills/ota/find_device.py        # prints "rivian-status <ip>"
   ```
   Fallbacks if it finds nothing (just booted / multicast blocked): catch the boot serial line
   `[ota] ready as rivian-status.local @ <ip>`, or use the last-known IP (**192.168.68.59** as of
   2026-07-22 — DHCP can change it). Confirm reachability: `curl -s --max-time 5 http://<ip>/ | head -c 40`.

3. **Upload over WiFi.** The `phase3-ota` env reads the OTA password from the `OTA_PASSWORD`
   shell var (it must match `#define OTA_PASSWORD` in `include/secrets.h`) and defaults its
   target to `rivian-status.local` — override with the IP since WSL can't resolve mDNS:
   ```bash
   OTA_PASSWORD=$(grep -oP '#define\s+OTA_PASSWORD\s+"\K[^"]+' include/secrets.h) \
     ~/.platformio/penv/bin/pio run -e phase3-ota -t upload --upload-port <device-ip>
   ```
   - Success ends with `100% Done...` then `[INFO]: Result: OK` / `Success`. (If espota prints a
     trailing `TimeoutError`/`NameError` after `100% Done...`, that's benign — the device reboots
     before the final ack. It succeeded if you saw 100%.)
   - A wrong/missing password fails auth.

4. **Verify it came back:** wait ~12 s for the reboot, then
   `curl -s --max-time 10 http://<device-ip>/ | sed 's/<[^>]*>/ /g' | grep -oiE "online|offline"`.

## Notes
- **Device name = OTA target.** If the user renamed the unit on `/config`, use that name
  (`<name>.local` / its IP), not `rivian-status`.
- **`ping`/`curl` fails, IP wrong:** DHCP changed the address — re-run the discovery script (Step 2).
- **Hangs at 0%:** inbound-to-host is blocked — do Step 0.
- **Fallback:** USB flashing always works — `fuser -k /dev/ttyACM0; pio run -e phase3 -t upload
  --upload-port /dev/ttyACM0` (see the repo `CLAUDE.md` for the WSL usbipd/serial workflow).
