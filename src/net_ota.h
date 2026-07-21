// net_ota — ArduinoOTA wireless firmware updates (plan §10 Phase 5). The OTA/mDNS name follows
// the device name (same source as the DHCP hostname), latched at otaBegin(); a runtime rename
// reboots to re-read it. Optional password via include/secrets.h: #define OTA_PASSWORD "...".
#pragma once

#include <Arduino.h>

void otaBegin();     // register ArduinoOTA as the device name; call after WiFi is connected
void otaHandle();    // call frequently from loop()
bool otaActive();    // true while an OTA push is in progress (for the LED phase)
int  otaProgress();  // 0-100 during a push, else -1
