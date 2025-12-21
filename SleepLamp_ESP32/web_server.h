#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "fs_select.h"
#include "config.h"
#include "led_control.h"
#include "ai_control.h"
#include "ai_state.h"

// ---------------- CORS ----------------
static void enableCORS() {
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
}
static void handleOptions(AsyncWebServerRequest* r) { r->send(204); }

// Externs from main ino
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

// ---------------- Apply actions (shared schema) ----------------
static bool applyActionsFromJsonText(const String& jsonText, String& appliedLog, String& err) {
  StaticJsonDocument<8192> doc;
  DeserializationError derr = deserializeJson(doc, jsonText);
  if (derr) { err = String("JSON parse error: ") + derr.c_str(); return false; }

  JsonArray actions = doc["actions"].as<JsonArray>();
  if (actions.isNull()) { err = "Missing actions array"; return false; }

  for (JsonObject obj : actions) {
    const char* type = obj["type"] | "";
    if (!*type) continue;

    if (strcmp(type, "set_brightness") == 0) {
      int v = obj["value"] | -1;
      if (v >= 0 && v <= 255) {
        LedControl::setTargetBrightness((uint8_t)v);
        savePreferenceBrightness((uint8_t)v);
        if (v == 0) { LedControl::setOn(false); savePreferenceOn(false); }
        else { LedControl::setOn(true); savePreferenceOn(true); }
        appliedLog += "brightness=" + String(v) + "; ";
      }
    } else if (strcmp(type, "set_color") == 0) {
      const char* hex = obj["hex"] | "";
      if (hex && strlen(hex) == 7 && hex[0] == '#') {
        uint32_t c = (uint32_t)strtoul(hex + 1, nullptr, 16);
        LedControl::setColor(c);
        savePreferenceColor(c);
        appliedLog += String("color=") + hex + "; ";
      }
    } else if (strcmp(type, "set_effect") == 0) {
      int id = obj["id"] | -1;
      if (id >= 0 && id <= 255) {
        LedControl::setEffect((uint16_t)id);
        savePreferenceEffect((uint16_t)id);
        appliedLog += "effect=" + String(id) + "; ";
      }
    } else if (strcmp(type, "set_mimir") == 0) {
      bool on = obj["on"] | false;
      LedControl::setMimir(on);
      savePreferenceMimir(on);
      appliedLog += String("mimir=") + (on ? "on" : "off") + "; ";
    } else if (strcmp(type, "set_power") == 0) {
      bool on = obj["on"] | false;
      LedControl::setOn(on);
      savePreferenceOn(on);
      appliedLog += String("power=") + (on ? "on" : "off") + "; ";
    } else if (strcmp(type, "set_mimir_range") == 0) {
      int minB = obj["min"] | -1;
      int maxB = obj["max"] | -1;
      if (minB >= 0 && maxB >= 0 && minB <= 255 && maxB <= 255) {
        if (minB > maxB) { int t=minB; minB=maxB; maxB=t; }
        LedControl::setMimirRange((uint8_t)minB, (uint8_t)maxB);
        savePreferenceMimirRange((uint8_t)minB, (uint8_t)maxB);
        appliedLog += "mimir_range=[" + String(minB) + "," + String(maxB) + "]; ";
      }
    }
  }

  if (!appliedLog.length()) { err = "No valid actions applied"; return false; }
  return true;
}

// ---------------- Preset cache ----------------
#ifndef PRESET_CACHE_MAX
#define PRESET_CACHE_MAX 24
#endif

struct PresetCacheItem {
  uint32_t ts = 0;
  String source;
  String note;
  String actionsJson; // stored as compact JSON string
};

static PresetCacheItem g_presets[PRESET_CACHE_MAX];
static uint8_t g_presetCount = 0;
static uint8_t g_presetHead = 0; // next insert index

static void cachePreset(uint32_t ts, const String& source, const String& note, const String& actionsJson) {
  g_presets[g_presetHead].ts = ts;
  g_presets[g_presetHead].source = source;
  g_presets[g_presetHead].note = note;
  g_presets[g_presetHead].actionsJson = actionsJson;

  g_presetHead = (uint8_t)((g_presetHead + 1) % PRESET_CACHE_MAX);
  if (g_presetCount < PRESET_CACHE_MAX) g_presetCount++;
}

// ---------------- Lamp REST handlers ----------------

static void handleSetColor(AsyncWebServerRequest* r) {
  if (!r->hasParam("hex")) { r->send(400, "application/json", "{\"error\":\"missing hex\"}"); return; }
  String hex = r->getParam("hex")->value();
  uint32_t color = LedControl::hexToColor(hex);
  LedControl::setColor(color);
  savePreferenceColor(color);
  r->send(200, "application/json", "{\"ok\":true}");
}

static void handleSetBrightness(AsyncWebServerRequest* r) {
  if (!r->hasParam("value")) { r->send(400, "application/json", "{\"error\":\"missing value\"}"); return; }
  int v = r->getParam("value")->value().toInt();
  v = constrain(v, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
  LedControl::setTargetBrightness((uint8_t)v);
  if (v == 0) { LedControl::setOn(false); savePreferenceOn(false); }
  else { LedControl::setOn(true); savePreferenceOn(true); }
  savePreferenceBrightness((uint8_t)v);
  r->send(200, "application/json", "{\"ok\":true}");
}

static void handleSetEffect(AsyncWebServerRequest* r) {
  if (!r->hasParam("id")) { r->send(400, "application/json", "{\"error\":\"missing id\"}"); return; }
  int id = constrain(r->getParam("id")->value().toInt(), 0, 255);
  LedControl::setEffect((uint16_t)id);
  savePreferenceEffect((uint16_t)id);
  r->send(200, "application/json", "{\"ok\":true}");
}

static void handleToggle(AsyncWebServerRequest* r) {
  bool on = LedControl::toggle();
  savePreferenceOn(on);
  String js = String("{\"on\":") + (on ? "true" : "false") + "}";
  r->send(200, "application/json", js);
}

static void handlePower(AsyncWebServerRequest* r) {
  if (!r->hasParam("on")) { r->send(400, "application/json", "{\"error\":\"missing on\"}"); return; }
  bool on = r->getParam("on")->value().toInt() != 0;
  LedControl::setOn(on);
  savePreferenceOn(on);
  String js = String("{\"ok\":true,\"on\":") + (on ? "true" : "false") + "}";
  r->send(200, "application/json", js);
}

static void handleSetMode(AsyncWebServerRequest* r) {
  if (!r->hasParam("mimir")) { r->send(400, "application/json", "{\"error\":\"missing mimir\"}"); return; }
  bool mm = r->getParam("mimir")->value().toInt() != 0;
  LedControl::setMimir(mm);
  savePreferenceMimir(mm);
  r->send(200, "application/json", "{\"ok\":true}");
}

static void handleMimirRange(AsyncWebServerRequest* r) {
  if (!r->hasParam("min") || !r->hasParam("max")) { r->send(400, "application/json", "{\"error\":\"missing min/max\"}"); return; }
  int minB = constrain(r->getParam("min")->value().toInt(), 0, 255);
  int maxB = constrain(r->getParam("max")->value().toInt(), 0, 255);
  if (minB > maxB) { int t=minB; minB=maxB; maxB=t; }
  LedControl::setMimirRange((uint8_t)minB, (uint8_t)maxB);
  savePreferenceMimirRange((uint8_t)minB, (uint8_t)maxB);
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"ok\":true,\"mimir_min\":%d,\"mimir_max\":%d}", minB, maxB);
  r->send(200, "application/json", buf);
}

static void handleLux(AsyncWebServerRequest* r) {
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"lux\":%.2f}", LedControl::getLux());
  r->send(200, "application/json", buf);
}

static void handleStatus(AsyncWebServerRequest* r) {
  String base = LedControl::jsonStatus(wifiModeString());
  r->send(200, "application/json", base);
}

static void handleWifi(AsyncWebServerRequest* r) {
  if (!r->hasParam("mode")) { r->send(400, "application/json", "{\"error\":\"missing mode\"}"); return; }
  String mode = r->getParam("mode")->value(); mode.toUpperCase();

  if (mode == "AP") {
    savePreferenceWiFiMode("AP");
    wifiStartAP();
    r->send(200, "application/json", "{\"ok\":true,\"mode\":\"AP\",\"host\":\"http://voidpointer.local/\"}");
    return;
  }

  if (mode == "STA") {
    String ssid = r->hasParam("ssid") ? r->getParam("ssid")->value() : "";
    String pass = r->hasParam("pass") ? r->getParam("pass")->value() : "";
    if (!ssid.length()) { r->send(400, "application/json", "{\"error\":\"missing ssid\"}"); return; }
    bool ok = wifiStartSTA(ssid, pass);
    if (ok) {
      savePreferenceWiFiMode("STA");
      savePreferenceSTA(ssid, pass);
      r->send(200, "application/json", "{\"ok\":true,\"mode\":\"STA\",\"host\":\"http://voidstar.local/\"}");
    } else {
      r->send(500, "application/json", "{\"ok\":false,\"error\":\"connect failed, reverted to AP\"}");
    }
    return;
  }

  r->send(400, "application/json", "{\"error\":\"invalid mode\"}");
}

static void handleWifiInfo(AsyncWebServerRequest* r) {
  String mode = wifiModeString();
  if (mode != "STA" || WiFi.status() != WL_CONNECTED) {
    String js = String("{\"mode\":\"") + mode + "\"}";
    r->send(200, "application/json", js);
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
    dns[0], dns[1], dns[2], dns[3]
  );
  r->send(200, "application/json", buf);
}

// ---- PC model integration endpoints ----

// POST /applyPreset with JSON body { "actions":[...], "source":"...", "ts":..., "note":"..." }
static void handleApplyPreset(AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
  static String body;
  if (index == 0) body = "";
  for (size_t i = 0; i < len; i++) body += (char)data[i];
  if (index + len != total) return;

  uint32_t ts = (uint32_t)(millis() / 1000UL);
  String source, note;

  // Extract ts/source/note cheaply from JSON (we still parse actions below)
  {
    StaticJsonDocument<512> meta;
    if (!deserializeJson(meta, body)) {
      if (meta["ts"].is<uint32_t>()) ts = meta["ts"].as<uint32_t>();
      source = (const char*)(meta["source"] | "");
      note = (const char*)(meta["note"] | "");
    }
  }

  String applied, err;
  bool ok = applyActionsFromJsonText(body, applied, err);

  if (ok) cachePreset(ts, source, note, body);

  StaticJsonDocument<256> resp;
  resp["ok"] = ok;
  if (ok) resp["applied"] = applied;
  else resp["error"] = err;

  String out;
  serializeJson(resp, out);
  r->send(ok ? 200 : 400, "application/json", out);
}

// GET /presets -> cached presets (bounded)
static void handlePresets(AsyncWebServerRequest* r) {
  StaticJsonDocument<4096> doc;
  doc["ok"] = true;
  JsonArray arr = doc.createNestedArray("presets");

  // Return in newest->oldest order
  for (int i = 0; i < g_presetCount; i++) {
    int idx = (int)g_presetHead - 1 - i;
    while (idx < 0) idx += PRESET_CACHE_MAX;
    PresetCacheItem& it = g_presets[idx];

    JsonObject o = arr.createNestedObject();
    o["ts"] = it.ts;
    o["source"] = it.source;
    o["note"] = it.note;

    // Provide compact "summary" fields for hover usage
    o["actions_json"] = it.actionsJson;
  }

  String out;
  serializeJson(doc, out);
  r->send(200, "application/json", out);
}

// POST /logAction: just acknowledge (UI uses this for “lamp-side logging”)
static void handleLogAction(AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
  // We don't persist on ESP32 to avoid flash wear; PC model will handle training storage.
  if (index + len != total) return;
  r->send(200, "application/json", "{\"ok\":true}");
}

// ---------------- AI endpoints (existing) ----------------

static void handleAIStart(AsyncWebServerRequest* r) {
  if (wifiModeString() != "STA") { r->send(400, "application/json", "{\"ok\":false,\"error\":\"Not in STA mode\"}"); return; }
  String prompt;
  if (r->hasParam("prompt", true)) prompt = r->getParam("prompt", true)->value();
  else if (r->hasParam("prompt")) prompt = r->getParam("prompt")->value();
  prompt.trim();
  if (!prompt.length()) { r->send(400, "application/json", "{\"ok\":false,\"error\":\"missing prompt\"}"); return; }
  if (!canStartAIJob()) { r->send(429, "application/json", "{\"ok\":false,\"error\":\"rate limit or job running\"}"); return; }
  if (!startAIJob(prompt)) { r->send(500, "application/json", "{\"ok\":false,\"error\":\"failed to create task\"}"); return; }
  r->send(202, "application/json", "{\"ok\":true,\"status\":\"started\"}");
}

static void handleAIStatus(AsyncWebServerRequest* r) {
  StaticJsonDocument<1024> doc;
  if (g_aiJob.running) {
    doc["running"] = true;
    doc["prompt"] = g_aiJob.prompt;
  } else if (!g_aiJob.done) {
    doc["idle"] = true;
  } else {
    doc["done"] = true;
    doc["ok"] = g_aiJob.ok;
    doc["prompt"] = g_aiJob.prompt;
    doc["applied"] = g_aiJob.appliedSummary;
    doc["error"] = g_aiJob.error;
    doc["model_snippet"] = g_aiJob.modelJsonSnippet;
    doc["canceled"] = g_aiJob.canceled;
    doc["duration_ms"] = (uint32_t)(millis() - g_aiJob.startedMs);
  }
  String out;
  serializeJson(doc, out);
  r->send(200, "application/json", out);
}

static void handleAICancel(AsyncWebServerRequest* r) {
  bool canceled = cancelAIJob();
  String js = String("{\"ok\":") + (canceled ? "true" : "false") +
              ",\"canceled\":" + (canceled ? "true" : "false") +
              (canceled ? "" : ",\"error\":\"no running job\"") + "}";
  r->send(canceled ? 200 : 400, "application/json", js);
}

// ---------------- Server bootstrap ----------------
namespace WebServerWrap {
void begin(AsyncWebServer& server) {
  enableCORS();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(FSYS, "/index.html", String(), false); });
  server.serveStatic("/script.js", FSYS, "/script.js").setCacheControl("max-age=60");
  server.serveStatic("/bootstrap.min.css", FSYS, "/bootstrap.min.css").setCacheControl("max-age=31536000");
  server.serveStatic("/bootstrap.bundle.min.js", FSYS, "/bootstrap.bundle.min.js").setCacheControl("max-age=31536000");

  // Lamp REST
  server.on("/setColor", HTTP_GET, handleSetColor);
  server.on("/setBrightness", HTTP_GET, handleSetBrightness);
  server.on("/setEffect", HTTP_GET, handleSetEffect);
  server.on("/toggle", HTTP_GET, handleToggle);
  server.on("/power", HTTP_GET, handlePower);
  server.on("/setMode", HTTP_GET, handleSetMode);
  server.on("/mimirRange", HTTP_GET, handleMimirRange);
  server.on("/lux", HTTP_GET, handleLux);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/wifi", HTTP_GET, handleWifi);
  server.on("/wifiInfo", HTTP_GET, handleWifiInfo);

  // PC model integration
  server.on("/applyPreset", HTTP_POST, [](AsyncWebServerRequest* r) {}, nullptr, handleApplyPreset);
  server.on("/presets", HTTP_GET, handlePresets);
  server.on("/logAction", HTTP_POST, [](AsyncWebServerRequest* r) {}, nullptr, handleLogAction);

  // AI
  server.on("/aiCommand", HTTP_POST, handleAIStart);
  server.on("/aiCommand", HTTP_GET, handleAIStart);
  server.on("/aiStatus", HTTP_GET, handleAIStatus);
  server.on("/aiCancel", HTTP_POST, handleAICancel);
  server.on("/aiCancel", HTTP_GET, handleAICancel);

#ifdef HTTP_OPTIONS
  server.on("/aiCommand", HTTP_OPTIONS, handleOptions);
  server.on("/aiStatus", HTTP_OPTIONS, handleOptions);
  server.on("/aiCancel", HTTP_OPTIONS, handleOptions);
  server.on("/applyPreset", HTTP_OPTIONS, handleOptions);
  server.on("/logAction", HTTP_OPTIONS, handleOptions);
  server.on("/presets", HTTP_OPTIONS, handleOptions);
#endif

  server.onNotFound([](AsyncWebServerRequest* r) { r->send(404, "application/json", "{\"error\":\"not found\"}"); });

  server.begin();
  Serial.println("[Web] Server started");
}
}