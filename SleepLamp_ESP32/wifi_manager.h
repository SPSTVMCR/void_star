#pragma once
#include <Arduino.h>
#include <WiFi.h>

// Implemented in SleepLamp_ESP32.ino (only there!)
String wifiModeString();                         // "AP", "STA", etc.
void   wifiStartAP();                            // start AP mode (old behavior)
bool   wifiStartSTA(const String& ssid, const String& pass);
int    getStaChannel();                          // STA channel (or -1 if unknown)

// -----------------------------------------------------------------------------
// Configuration constants (no function definitions here)
// -----------------------------------------------------------------------------

// AP SSID and password
#ifndef AP_SSID
  #define AP_SSID "VOIDSTAR"
#endif

#ifndef AP_PASS
  #define AP_PASS "esp32lamp"
#endif

// Hostnames for mDNS in each mode
#ifndef HOSTNAME_STA
  #define HOSTNAME_STA "voidstar"      // voidstar.local in STA mode
#endif

#ifndef HOSTNAME_AP
  #define HOSTNAME_AP  "voidpointer"   // voidpointer.local in AP mode
#endif

#ifndef WIFI_CONNECT_TIMEOUT_MS
  #define WIFI_CONNECT_TIMEOUT_MS 20000UL
#endif