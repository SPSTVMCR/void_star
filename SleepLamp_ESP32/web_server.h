#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "fs_select.h"
#include "config.h"
#include "led_control.h"

// Externs from main .ino
extern Preferences preferences;
String wifiModeString();
void wifiStartAP();
bool wifiStartSTA(const String& ssid, const String& pass);
void savePreferenceColor(uint32_t color);
void savePreferenceBrightness(uint8_t b);
void savePreferenceEffect(uint16_t e);
void savePreferenceOn(bool on);
void savePreferenceMimir(bool m);
void savePreferenceWiFiMode(const String& mode);
void savePreferenceSTA(const String& ssid, const String& pass);
void savePreferenceMimirRange(uint8_t minB, uint8_t maxB);
int getStaChannel();

namespace WebServerWrap {

void handleSetColor(AsyncWebServerRequest* request) {
  if (!request->hasParam("hex")) {
    request->send(400, "application/json", "{\"error\":\"missing hex\"}");
    return;
  }
  String hex = request->getParam("hex")->value();
  uint32_t color = LedControl::hexToColor(hex);
  LedControl::setColor(color);
  savePreferenceColor(color);
  request->send(200, "application/json", "{\"ok\":true}");
}

void handleSetBrightness(AsyncWebServerRequest* request) {
  if (!request->hasParam("value")) {
    request->send(400, "application/json", "{\"error\":\"missing value\"}");
    return;
  }
  int v = request->getParam("value")->value().toInt();
  v = constrain(v, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
  LedControl::setTargetBrightness((uint8_t)v);
  if (v == 0) {
    LedControl::setOn(false);
    savePreferenceOn(false);
  } else {
    LedControl::setOn(true);
    savePreferenceOn(true);
  }
  savePreferenceBrightness((uint8_t)v);
  request->send(200, "application/json", "{\"ok\":true}");
}

void handleSetEffect(AsyncWebServerRequest* request) {
  if (!request->hasParam("id")) {
    request->send(400, "application/json", "{\"error\":\"missing id\"}");
    return;
  }
  int id = request->getParam("id")->value().toInt();
  id = constrain(id, 0, 255);
  LedControl::setEffect((uint16_t)id);
  savePreferenceEffect((uint16_t)id);
  request->send(200, "application/json", "{\"ok\":true}");
}

void handleToggle(AsyncWebServerRequest* request) {
  bool on = LedControl::toggle();
  savePreferenceOn(on);
  request->send(200, "application/json", String("{\"on\":") + (on ? "true" : "false") + "}");
}

void handlePower(AsyncWebServerRequest* request) {
  if (!request->hasParam("on")) {
    request->send(400, "application/json", "{\"error\":\"missing on\"}");
    return;
  }
  int onv = request->getParam("on")->value().toInt();
  bool on = (onv != 0);
  LedControl::setOn(on);
  savePreferenceOn(on);
  request->send(200, "application/json", String("{\"ok\":true,\"on\":") + (on ? "true" : "false") + "}");
}

void handleSetMode(AsyncWebServerRequest* request) {
  if (!request->hasParam("mimir")) {
    request->send(400, "application/json", "{\"error\":\"missing mimir\"}");
    return;
  }
  int m = request->getParam("mimir")->value().toInt();
  bool mm = (m != 0);
  LedControl::setMimir(mm);
  savePreferenceMimir(mm);
  request->send(200, "application/json", "{\"ok\":true}");
}

// Adjust Mimir brightness range via UI
void handleMimirRange(AsyncWebServerRequest* request) {
  if (!request->hasParam("min") || !request->hasParam("max")) {
    request->send(400, "application/json", "{\"error\":\"missing min/max\"}");
    return;
  }
  int minB = request->getParam("min")->value().toInt();
  int maxB = request->getParam("max")->value().toInt();
  minB = constrain(minB, 0, 255);
  maxB = constrain(maxB, 0, 255);
  if (minB > maxB) {
    int t = minB;
    minB = maxB;
    maxB = t;
  }

  LedControl::setMimirRange((uint8_t)minB, (uint8_t)maxB);
  savePreferenceMimirRange((uint8_t)minB, (uint8_t)maxB);

  char buf[96];
  snprintf(buf, sizeof(buf), "{\"ok\":true,\"mimir_min\":%d,\"mimir_max\":%d}", minB, maxB);
  request->send(200, "application/json", buf);
}

void handleLux(AsyncWebServerRequest* request) {
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"lux\":%.2f}", LedControl::getLux());
  request->send(200, "application/json", buf);
}

void handleStatus(AsyncWebServerRequest* request) {
  String js = LedControl::jsonStatus(wifiModeString());
  request->send(200, "application/json", js);
}

void handleWiFi(AsyncWebServerRequest* request) {
  if (!request->hasParam("mode")) {
    request->send(400, "application/json", "{\"error\":\"missing mode\"}");
    return;
  }
  String mode = request->getParam("mode")->value();
  mode.toUpperCase();
  if (mode == "AP") {
    savePreferenceWiFiMode("AP");
    wifiStartAP();
    request->send(200, "application/json", "{\"ok\":true,\"mode\":\"AP\"}");
    return;
  } else if (mode == "STA") {
    String ssid = request->hasParam("ssid") ? request->getParam("ssid")->value() : "";
    String pass = request->hasParam("pass") ? request->getParam("pass")->value() : "";
    if (ssid.length() == 0) {
      request->send(400, "application/json", "{\"error\":\"missing ssid\"}");
      return;
    }
    bool ok = wifiStartSTA(ssid, pass);
    if (ok) {
      savePreferenceWiFiMode("STA");
      savePreferenceSTA(ssid, pass);
      request->send(200, "application/json", "{\"ok\":true,\"mode\":\"STA\"}");
    } else {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"connect failed, reverted to AP\"}");
    }
    return;
  } else {
    request->send(400, "application/json", "{\"error\":\"invalid mode\"}");
    return;
  }
}

void handleWifiInfo(AsyncWebServerRequest* request) {
  String mode = wifiModeString();
  if (mode != "STA" || WiFi.status() != WL_CONNECTED) {
    String js = String("{\"mode\":\"") + mode + "\"}";
    request->send(200, "application/json", js);
    return;
  }

  String ssid = WiFi.SSID();
  int32_t rssi = WiFi.RSSI();
  IPAddress ip = WiFi.localIP();
  IPAddress gw = WiFi.gatewayIP();
  IPAddress sn = WiFi.subnetMask();
  IPAddress dns = WiFi.dnsIP(0);
  int ch = getStaChannel();

  char buf[384];
  snprintf(buf, sizeof(buf),
           "{\"mode\":\"STA\",\"ssid\":\"%s\",\"rssi\":%ld,\"channel\":%d,"
           "\"ip\":\"%u.%u.%u.%u\",\"gw\":\"%u.%u.%u.%u\",\"subnet\":\"%u.%u.%u.%u\",\"dns\":\"%u.%u.%u.%u\"}",
           ssid.c_str(), (long)rssi, ch,
           ip[0], ip[1], ip[2], ip[3],
           gw[0], gw[1], gw[2], gw[3],
           sn[0], sn[1], sn[2], sn[3],
           dns[0], dns[1], dns[2], dns[3]);
  request->send(200, "application/json", buf);
}

void begin(AsyncWebServer& server) {
  // Static files from LittleFS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(FSYS, "/index.html", String(), false);
  });
  server.serveStatic("/script.js", FSYS, "/script.js").setCacheControl("max-age=600");
  server.serveStatic("/bootstrap.min.css", FSYS, "/bootstrap.min.css").setCacheControl("max-age=31536000");
  server.serveStatic("/bootstrap.bundle.min.js", FSYS, "/bootstrap.bundle.min.js").setCacheControl("max-age=31536000");

  // API endpoints
  server.on("/setColor", HTTP_GET, handleSetColor);
  server.on("/setBrightness", HTTP_GET, handleSetBrightness);
  server.on("/setEffect", HTTP_GET, handleSetEffect);
  server.on("/toggle", HTTP_GET, handleToggle);
  server.on("/power", HTTP_GET, handlePower);
  server.on("/setMode", HTTP_GET, handleSetMode);
  server.on("/mimirRange", HTTP_GET, handleMimirRange);
  server.on("/lux", HTTP_GET, handleLux);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/wifi", HTTP_GET, handleWiFi);
  server.on("/wifiInfo", HTTP_GET, handleWifiInfo);

  // Fallback 404
  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "application/json", "{\"error\":\"not found\"}");
  });

  server.begin();
  Serial.println("[Web] Server started");
}
}