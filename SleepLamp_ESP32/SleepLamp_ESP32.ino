#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <esp_idf_version.h>
#include <ESPmDNS.h>
#include "esp_wifi.h"
// better than shoving everything into this main ino, innit?
#include "fs_select.h"
#include "wifi_manager.h"
#include "config.h"
#include "mimir_tuning.h"
#include "led_control.h"
#include "web_server.h"

/// Globals
Preferences preferences;
AsyncWebServer server(80);

// Wi‑Fi state
volatile WiFiMode_t g_wifiMode = WIFI_MODE_AP;  // default AP
String g_staSsid;
String g_staPass;

// ESPNOW state
volatile float g_lastLux = 0.0f;
volatile uint32_t g_lastLuxMillis = 0;

// Button debounce flag
volatile bool g_buttonPressed = false;
volatile uint32_t g_lastButtonISR = 0;

// Default preferences for amimir mode
#ifndef PREF_KEY_MIMIR_MIN
#define PREF_KEY_MIMIR_MIN "mimir_min"
#endif
#ifndef PREF_KEY_MIMIR_MAX
#define PREF_KEY_MIMIR_MAX "mimir_max"
#endif

// Forward declarations for web_server.h to call
void wifiStartAP();
bool wifiStartSTA(const String& ssid, const String& pass);
String wifiModeString();
void reinitEspNow();
void savePreferenceMimirRange(uint8_t minB, uint8_t maxB);
int getStaChannel();

// ISR
void IRAM_ATTR isrButton() {
  uint32_t now = millis();
  if (now - g_lastButtonISR > 250) {  // debounce
    g_lastButtonISR = now;
    g_buttonPressed = true;
  }
}

/// ESPNOW
#if (ESP_IDF_VERSION_MAJOR >= 5)
void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* incomingData, int len) {
#else
void onEspNowRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
#endif
  float luxValue = 0.0f;
  if (len == (int)sizeof(float)) {
    memcpy((void*)&luxValue, incomingData, sizeof(float));
  } else {
    char buf[32];
    int n = min(len, (int)sizeof(buf) - 1);
    memcpy(buf, incomingData, n);
    buf[n] = '\0';
    luxValue = atof(buf);
  }
  g_lastLux = luxValue;
  g_lastLuxMillis = millis();
  LedControl::updateLux(luxValue);
}

void reinitEspNow() {
  esp_now_deinit();

  if (g_wifiMode == WIFI_MODE_STA) {
    WiFi.mode(WIFI_STA);
  } else if (g_wifiMode == WIFI_MODE_AP) {
    WiFi.mode(WIFI_AP);
  } else {
    WiFi.mode(WIFI_AP_STA);
  }

  esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    Serial.printf("[ESP-NOW] Init failed: %d\n", (int)err);
    return;
  }

  esp_now_register_recv_cb(onEspNowRecv);

  esp_now_peer_info_t peerInfo = {};
  uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };  // Broadcast
  memcpy(peerInfo.peer_addr, bcast, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  err = esp_now_add_peer(&peerInfo);
  if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
    Serial.printf("[ESP-NOW] Add broadcast peer failed: %d\n", (int)err);
  }

  Serial.println("[ESP-NOW] Initialized");
}

String wifiModeString() {
  if (g_wifiMode == WIFI_MODE_STA) return "STA";
  if (g_wifiMode == WIFI_MODE_AP) return "AP";
  if (g_wifiMode == WIFI_MODE_APSTA) return "AP_STA";
  return "UNKNOWN";
}

int getStaChannel() {
  if (WiFi.getMode() == WIFI_MODE_STA && WiFi.status() == WL_CONNECTED) {
    return WiFi.channel();
  }
  return -1;
}

// Access Point - Wi‑Fi (old behavior, no args) + mDNS
void wifiStartAP() {
  Serial.println("[WiFi] wifiStartAP()");

  // track mode
  g_wifiMode = WIFI_MODE_AP;

  // Tear down STA + mDNS
  MDNS.end();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(50);

  WiFi.mode(WIFI_AP);
  WiFi.setHostname(HOSTNAME_AP);

  const uint8_t channel = 1; // default AP channel
  if (!WiFi.softAP(AP_SSID, AP_PASS, channel)) {
    Serial.println("[WiFi] softAP failed");
    return;
  }

  delay(100);  // allow AP interface up

  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WiFi] AP SSID=%s PASS=%s CH=%u IP=%s\n",
                AP_SSID, AP_PASS, channel, ip.toString().c_str());

  if (MDNS.begin(HOSTNAME_AP)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] %s.local -> %s\n",
                  HOSTNAME_AP, ip.toString().c_str());
  } else {
    Serial.println("[mDNS] begin(AP) failed");
  }
}

// Sta Mode
bool wifiStartSTA(const String& ssid, const String& pass) {
  Serial.printf("[WiFi] wifiStartSTA('%s')\n", ssid.c_str());
  g_wifiMode = WIFI_MODE_STA;

  MDNS.end();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(50);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] STA connect failed, reverting to AP...");
    wifiStartAP();  
    return false;
  }

  IPAddress ip = WiFi.localIP();
  Serial.printf("[WiFi] STA (%s) IP=%s\n", HOSTNAME_STA, ip.toString().c_str());

  if (MDNS.begin(HOSTNAME_STA)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] %s.local -> %s\n",
                  HOSTNAME_STA, ip.toString().c_str());
  } else {
    Serial.println("[mDNS] begin(STA) failed");
  }

  return true;
}

// Load previous LED states from preferences
void loadPreferences() {
  preferences.begin(PREF_NAMESPACE, true);

  // LED settings
  uint32_t color = preferences.getUInt(PREF_KEY_COLOR, DEFAULT_COLOR_HEX);
  uint8_t brightness = preferences.getUChar(PREF_KEY_BRIGHTNESS, DEFAULT_BRIGHTNESS);
  uint16_t effectId = preferences.getUShort(PREF_KEY_EFFECT, DEFAULT_EFFECT_ID);
  bool isOn = preferences.getBool(PREF_KEY_ON, DEFAULT_ON);
  bool mimir = preferences.getBool(PREF_KEY_MIMIR, DEFAULT_MIMIR);

  // Mimir range
  uint8_t mimirMin = preferences.getUChar(PREF_KEY_MIMIR_MIN, MIMIR_BRIGHT_MIN);
  uint8_t mimirMax = preferences.getUChar(PREF_KEY_MIMIR_MAX, MIMIR_BRIGHT_MAX);

  // Wi‑Fi settings
  String savedMode = preferences.getString(PREF_KEY_WIFI_MODE, "AP");
  g_staSsid = preferences.getString(PREF_KEY_STA_SSID, "");
  g_staPass = preferences.getString(PREF_KEY_STA_PASS, "");

  preferences.end();

  LedControl::init();
  LedControl::setColor(color);
  LedControl::setEffect(effectId);
  LedControl::setOn(isOn);
  LedControl::setMimir(mimir);
  LedControl::setMimirRange(mimirMin, mimirMax);
  LedControl::setTargetBrightness(brightness);

  // Wi‑Fi mode from pref
  if (savedMode == "STA" && g_staSsid.length() > 0) {
    wifiStartSTA(g_staSsid, g_staPass);
  } else {
    wifiStartAP();
  }

  Serial.printf("[Prefs] on=%s, color=%06X, target_brightness=%u, effect=%u, mimir=%s, range=[%u,%u], wifi=%s\n",
                isOn ? "true" : "false", color, brightness, effectId, mimir ? "true" : "false",
                mimirMin, mimirMax, wifiModeString().c_str());
}

// GET A LOAD OF THESE FUNCTIONS
void savePreferenceColor(uint32_t color) {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putUInt(PREF_KEY_COLOR, color);
  preferences.end();
}
void savePreferenceBrightness(uint8_t b) {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putUChar(PREF_KEY_BRIGHTNESS, b);
  preferences.end();
}
void savePreferenceEffect(uint16_t e) {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putUShort(PREF_KEY_EFFECT, e);
  preferences.end();
}
void savePreferenceOn(bool on) {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putBool(PREF_KEY_ON, on);
  preferences.end();
}
void savePreferenceMimir(bool m) {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putBool(PREF_KEY_MIMIR, m);
  preferences.end();
}
void savePreferenceWiFiMode(const String& mode) {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putString(PREF_KEY_WIFI_MODE, mode);
  preferences.end();
}
void savePreferenceSTA(const String& ssid, const String& pass) {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putString(PREF_KEY_STA_SSID, ssid);
  preferences.putString(PREF_KEY_STA_PASS, pass);
  preferences.end();
}
void savePreferenceMimirRange(uint8_t minB, uint8_t maxB) {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putUChar(PREF_KEY_MIMIR_MIN, minB);
  preferences.putUChar(PREF_KEY_MIMIR_MAX, maxB);
  preferences.end();
}

// Setup
void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), isrButton, FALLING);

  Serial.begin(115200);
  for (uint32_t t = millis(); !Serial && millis() - t < 3000;) { delay(10); }

  if (!FS_BEGIN(true)) {
    Serial.printf("[%s] Mount failed (label=\"%s\")\n", FSYS_NAME, FS_PART_LABEL);
  } else {
    Serial.printf("[%s] Mounted (label=\"%s\")\n", FSYS_NAME, FS_PART_LABEL);
  }

  loadPreferences();

  // test
  LedControl::selfTestRGB(40);

  // Start webserver
  WebServerWrap::begin(server);
}

void loop() {
  if (g_buttonPressed) {
    g_buttonPressed = false;
    bool on = LedControl::toggle();
    savePreferenceOn(on);
  }

  LedControl::tick();
}