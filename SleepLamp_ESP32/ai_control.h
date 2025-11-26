#pragma once


#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "ai_state.h"
#include "config.h"
#include "led_control.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#endif

#ifndef GEMINI_API_KEY
  #define GEMINI_API_KEY ""
#endif
#ifndef GEMINI_MODEL
  #define GEMINI_MODEL "gemini-2.5-flash-lite"
#endif
#ifndef GEMINI_HOST
  #define GEMINI_HOST "generativelanguage.googleapis.com"
#endif

// Preference savers
extern void savePreferenceColor(uint32_t color);
extern void savePreferenceBrightness(uint8_t b);
extern void savePreferenceEffect(uint16_t e);
extern void savePreferenceOn(bool on);
extern void savePreferenceMimir(bool m);
extern void savePreferenceMimirRange(uint8_t minB, uint8_t maxB);

/* ===================== Effect mapping ===================== */
struct EffKV { const char* key; uint16_t id; };
static String normKey(const String& s) {
  String o; o.reserve(s.length());
  for (char c : s) {
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o += c;
  }
  // normalize "theatre" -> "theater" (oi it's chewsday innit)
  o.replace("theatre","theater");
  return o;
}
static const EffKV EFFECT_KV[] PROGMEM = {
  {"static",0},{"blink",1},{"breath",2},{"colorwipe",3},{"colorwipeinv",4},{"colorwiperev",5},{"colorwiperevinv",6},
  {"colorwiperandom",7},{"randomcolor",8},{"singledynamic",9},{"multidynamic",10},{"rainbow",11},{"rainbowcycle",12},
  {"scan",13},{"dualscan",14},{"fade",15},{"theaterchase",16},{"theaterchaserainbow",17},{"runninglights",18},
  {"twinkle",19},{"twinklerandom",20},{"twinklefade",21},{"twinklefaderandom",22},{"sparkle",23},{"flashsparkle",24},
  {"hypersparkle",25},{"strobe",26},{"stroberainbow",27},{"multistrobe",28},{"blinkrainbow",29},{"chasewhite",30},
  {"chasecolor",31},{"chaserandom",32},{"chaserainbow",33},{"chaseflash",34},{"chaseflashrandom",35},
  {"chaserainbowwhite",36},{"chaseblackout",37},{"chaseblackoutrainbow",38},{"colorsweeprandom",39},
  {"runningcolor",40},{"runningredblue",41},{"runningrandom",42},{"larsonscanner",43},{"comet",44},{"fireworks",45},
  {"fireworksrandom",46},{"merrychristmas",47},{"fireflicker",48},{"fireflickersoft",49},{"fireflickerintense",50},
  {"circuscombustus",51},{"halloween",52},{"bicolorchase",53},{"tricolorchase",54},{"icu",55},
  // common synonyms
  {"rainbowwheel",12},{"wheel",12},{"cycle",12},{"scanner",43},{"knightrider",43},{"cylon",43},{"police",41}
};
static int effectIdFromName(const String& name) {
  String k = normKey(name);
  for (size_t i=0; i<sizeof(EFFECT_KV)/sizeof(EFFECT_KV[0]); ++i) {
    if (k == FPSTR(EFFECT_KV[i].key)) return EFFECT_KV[i].id;
  }
  // Fallback linear compare
  for (size_t i=0; i<sizeof(EFFECT_KV)/sizeof(EFFECT_KV[0]); ++i) {
    String kk = String(EFFECT_KV[i].key);
    if (k == kk) return EFFECT_KV[i].id;
  }
  return -1;
}

/* ===================== Model instructions ===================== */
static const char* kSystemInstruction =
  "You control a smart RGB lamp via strict JSON ONLY. Output EXACTLY one JSON object with this schema: "
  "{\"actions\":["
  "{\"type\":\"set_brightness\",\"value\":0..255},"
  "{\"type\":\"set_color\",\"hex\":\"#RRGGBB\"},"
  "{\"type\":\"set_effect\",\"id\":0..255},"
  "{\"type\":\"set_effect\",\"name\":\"<EffectName>\"},"
  "{\"type\":\"set_mimir\",\"on\":true|false},"
  "{\"type\":\"set_power\",\"on\":true|false},"
  "{\"type\":\"set_mimir_range\",\"min\":0..255,\"max\":0..255}"
  "]}. "
  "WS2812FX Effects (ID : Name): "
  "0:Static,1:Blink,2:Breath,3:Color Wipe,4:Color Wipe Inv,5:Color Wipe Rev,6:Color Wipe Rev Inv,7:Color Wipe Random,"
  "8:Random Color,9:Single Dynamic,10:Multi Dynamic,11:Rainbow,12:Rainbow Cycle,13:Scan,14:Dual Scan,15:Fade,"
  "16:Theater Chase,17:Theater Chase Rainbow,18:Running Lights,19:Twinkle,20:Twinkle Random,21:Twinkle Fade,"
  "22:Twinkle Fade Random,23:Sparkle,24:Flash Sparkle,25:Hyper Sparkle,26:Strobe,27:Strobe Rainbow,28:Multi Strobe,"
  "29:Blink Rainbow,30:Chase White,31:Chase Color,32:Chase Random,33:Chase Rainbow,34:Chase Flash,35:Chase Flash Random,"
  "36:Chase Rainbow White,37:Chase Blackout,38:Chase Blackout Rainbow,39:Color Sweep Random,40:Running Color,"
  "41:Running Red Blue,42:Running Random,43:Larson Scanner,44:Comet,45:Fireworks,46:Fireworks Random,47:Merry Christmas,"
  "48:Fire Flicker,49:Fire Flicker (Soft),50:Fire Flicker (Intense),51:Circus Combustus,52:Halloween,53:Bicolor Chase,"
  "54:Tricolor Chase,55:ICU. "
  "If the user names an effect, prefer set_effect with \"name\"; otherwise use \"id\". No markdown, no code fences.";

/* ===================== Snippet cleanup, extraction (same as hardened) ===================== */
static String sanitizeModelSnippet(const String& raw) {
  int i=0; while (i<(int)raw.length() && isdigit((unsigned char)raw[i])) i++;
  if (i<(int)raw.length() && (raw[i]=='\n' || raw[i]=='\r')) i++;
  String s = raw.substring(i);
  const size_t MAX = AI_MODEL_SNIPPET_MAX;
  if (s.length() > MAX) s = s.substring(0, MAX);
  return s;
}
static String stripCodeFences(const String& in) {
  String s=in; s.trim();
  if (s.startsWith("```")) {
    int firstNL = s.indexOf('\n'); if (firstNL<0) return s;
    int close = s.lastIndexOf("```"); if (close>firstNL) s = s.substring(firstNL+1, close);
    s.trim();
  }
  return s;
}
static String extractFirstJSONObject(const String& in) {
  int n=in.length(), start=-1, depth=0; bool inStr=false, esc=false;
  for (int i=0;i<n;++i){ char c=in[i];
    if (inStr){ if(esc){esc=false;continue;} if(c=='\\'){esc=true;continue;} if(c=='"'){inStr=false;continue;} continue; }
    if (c=='"'){ inStr=true; continue; }
    if (c=='{'){ if(depth==0) start=i; depth++; }
    else if (c=='}'){ if(depth>0) depth--; if(depth==0 && start>=0) return in.substring(start,i+1); }
  }
  return String();
}
static bool extractGeminiTextJSON(const String& body, String& out) {
  DynamicJsonDocument doc(16384);
  if (!deserializeJson(doc, body)) {
    JsonVariant v = doc["candidates"][0]["content"]["parts"][0]["text"];
    if (!v.isNull()) { out = v.as<const char*>(); if (out.length()) return true; }
  }
  int idx = body.indexOf("\"text\"");
  if (idx >= 0) {
    idx = body.indexOf(':', idx);
    int q = body.indexOf('"', idx);
    if (q >= 0) { q++; String acc; bool esc=false;
      for (int i=q;i<(int)body.length();++i){ char c=body[i];
        if (esc){ if(c=='n') acc+='\n'; else acc+=c; esc=false; continue; }
        if (c=='\\'){ esc=true; continue; }
        if (c=='"'){ out=acc; return out.length()>0; }
        acc+=c;
      }
    }
  }
  return false;
}

/* ===================== Apply actions ===================== */
static uint32_t parseHexColor(const String& hex) {
  if (hex.length()!=7 || hex[0]!='#') return LedControl::getColor();
  return (uint32_t)strtoul(hex.substring(1).c_str(), nullptr, 16);
}
static void applyOneAction(const JsonObject& obj, String& logAccum) {
  const char* type = obj["type"] | "";
  if (!*type) return;

  if (strcmp(type,"set_brightness")==0) {
    int v = obj["value"] | -1;
    if (v>=0 && v<=255) {
      LedControl::setTargetBrightness((uint8_t)v);
      savePreferenceBrightness((uint8_t)v);
      if (v==0){ LedControl::setOn(false); savePreferenceOn(false);} else { LedControl::setOn(true); savePreferenceOn(true); }
      logAccum += "brightness=" + String(v) + "; ";
    }
  } else if (strcmp(type,"set_color")==0) {
    String hex = obj["hex"] | "";
    if (hex.length()==7 && hex[0]=='#') {
      uint32_t c = parseHexColor(hex);
      LedControl::setColor(c);
      savePreferenceColor(c);
      logAccum += "color=" + hex + "; ";
    }
  } else if (strcmp(type,"set_effect")==0) {
    int id = obj["id"] | -1;
    if (id < 0) {
      String name = obj["name"] | obj["label"] | obj["effect"] | "";
      if (name.length()) id = effectIdFromName(name);
    }
    if (id >= 0 && id <= 255) {
      LedControl::setEffect((uint16_t)id);
      savePreferenceEffect((uint16_t)id);
      logAccum += "effect=" + String(id) + "; ";
    }
  } else if (strcmp(type,"set_mimir")==0) {
    bool on = obj["on"] | false;
    LedControl::setMimir(on);
    savePreferenceMimir(on);
    logAccum += String("mimir=") + (on?"on":"off") + "; ";
  } else if (strcmp(type,"set_power")==0) {
    bool on = obj["on"] | false;
    LedControl::setOn(on);
    savePreferenceOn(on);
    logAccum += String("power=") + (on?"on":"off") + "; ";
  } else if (strcmp(type,"set_mimir_range")==0) {
    int minB = obj["min"] | -1, maxB = obj["max"] | -1;
    if (minB>=0 && maxB>=0 && minB<=255 && maxB<=255) {
      if (minB>maxB){ int t=minB; minB=maxB; maxB=t; }
      LedControl::setMimirRange((uint8_t)minB,(uint8_t)maxB);
      savePreferenceMimirRange((uint8_t)minB,(uint8_t)maxB);
      logAccum += "mimir_range=[" + String(minB) + "," + String(maxB) + "]; ";
    }
  }
}
static bool parseAndApplyActions(const String& jsonText, String& appliedLog, String& err) {
  StaticJsonDocument<8192> doc;
  DeserializationError derr = deserializeJson(doc, jsonText);
  if (derr) { err = String("JSON parse error: ") + derr.c_str(); return false; }
  JsonArray actions = doc["actions"].as<JsonArray>();
  if (actions.isNull()) { err = "Missing actions array"; return false; }
  for (JsonObject a : actions) applyOneAction(a, appliedLog);
  if (appliedLog.length()==0){ err="No valid actions applied"; return false; }
  return true;
}

/* Gemini */
static void buildRequestBody(const String& prompt, String& outJson) {
  StaticJsonDocument<4096> req;
  JsonObject genCfg = req.createNestedObject("generationConfig");
  genCfg["responseMimeType"] = "application/json";
  JsonArray contents = req.createNestedArray("contents");
  { JsonObject c=contents.createNestedObject(); c["role"]="user"; JsonArray parts=c.createNestedArray("parts"); parts.createNestedObject()["text"]=kSystemInstruction; }
  { JsonObject c=contents.createNestedObject(); c["role"]="user"; JsonArray parts=c.createNestedArray("parts"); parts.createNestedObject()["text"]=prompt; }
  outJson.clear(); serializeJson(req, outJson);
}

static void runGeminiJob(const String& prompt) {
  g_aiJob.startedMs = millis(); g_aiJob.ok=false; g_aiJob.done=false; g_aiJob.canceled=false;
  g_aiJob.appliedSummary=""; g_aiJob.modelJsonSnippet=""; g_aiJob.error="";

#ifdef GEMINI_DISABLED
  g_aiJob.error="Gemini disabled"; g_aiJob.done=true; return;
#endif
  if (WiFi.status()!=WL_CONNECTED){ g_aiJob.error="WiFi not connected"; g_aiJob.done=true; return; }
  if (String(GEMINI_API_KEY).length()<8){ g_aiJob.error="Missing GEMINI_API_KEY"; g_aiJob.done=true; return; }

  String reqBody; buildRequestBody(prompt, reqBody);
  WiFiClientSecure client; client.setTimeout(15000); client.setInsecure();
  const String path = String("/v1beta/models/")+GEMINI_MODEL+":generateContent?key="+GEMINI_API_KEY;
  if (!client.connect(GEMINI_HOST,443)){ g_aiJob.error="TLS connect failed"; g_aiJob.done=true; return; }

  client.printf("POST %s HTTP/1.1\r\n", path.c_str());
  client.printf("Host: %s\r\n", GEMINI_HOST);
  client.println("Content-Type: application/json");
  client.printf("Content-Length: %u\r\n", (unsigned)reqBody.length());
  client.println("Connection: close"); client.println(); client.print(reqBody);

  String statusLine = client.readStringUntil('\n');
  if (!statusLine.startsWith("HTTP/1.1 200")) {
    String errBody; while (client.connected() || client.available()) { errBody += client.readString(); yield(); }
    g_aiJob.error = "HTTP error: " + statusLine;
    g_aiJob.modelJsonSnippet = sanitizeModelSnippet(errBody);
    g_aiJob.done = true; return;
  }
  while (true){ String h=client.readStringUntil('\n'); if (h=="\r"||!h.length()) break; yield(); }

  String body; while (client.connected() || client.available()) { body += client.readString(); yield(); }

  if (g_aiJob.canceled){ g_aiJob.error="Canceled"; g_aiJob.done=true; return; }

  String modelText;
  if (!extractGeminiTextJSON(body, modelText)) {
    g_aiJob.error="No model text";
    g_aiJob.modelJsonSnippet=sanitizeModelSnippet(body);
    g_aiJob.done=true; return;
  }

  String normalized = stripCodeFences(modelText); normalized.trim();
  if (!normalized.startsWith("{")) { String obj = extractFirstJSONObject(normalized); if (obj.length()) normalized=obj; }
  g_aiJob.modelJsonSnippet = sanitizeModelSnippet(normalized);

  String appliedLog, parseErr;
  if (!parseAndApplyActions(normalized, appliedLog, parseErr)) {
    String obj = extractFirstJSONObject(body);
    if (obj.length()) {
      String applied2, err2;
      if (parseAndApplyActions(obj, applied2, err2)) {
        g_aiJob.ok=true; g_aiJob.appliedSummary=applied2; g_aiJob.done=true; return;
      } else { parseErr += " | Fallback parse failed: " + err2; }
    }
    g_aiJob.error=parseErr; g_aiJob.appliedSummary=""; g_aiJob.done=true; return;
  }

  g_aiJob.ok=true; g_aiJob.appliedSummary=appliedLog; g_aiJob.done=true;
}

/* ===================== Public API ===================== */
inline bool canStartAIJob() {
  if (g_aiJob.running) return false;
  unsigned long now=millis(); if (g_aiJob.startedMs && (now-g_aiJob.startedMs)<AI_MIN_INTERVAL_MS) return false;
  return true;
}
inline bool startAIJob(const String& prompt) {
  if (!canStartAIJob()) return false;
  g_aiJob.running=true; g_aiJob.done=false; g_aiJob.ok=false; g_aiJob.canceled=false;
  g_aiJob.prompt=prompt; g_aiJob.appliedSummary=""; g_aiJob.modelJsonSnippet=""; g_aiJob.error=""; g_aiJob.startedMs=millis();
  const uint32_t stack=16384;
  BaseType_t rc = xTaskCreatePinnedToCore([](void*){ runGeminiJob(g_aiJob.prompt); g_aiJob.running=false; vTaskDelete(nullptr); }, "AIJobTask", stack, nullptr, 1, nullptr, APP_CPU_NUM);
  if (rc!=pdPASS){ g_aiJob.running=false; g_aiJob.done=true; g_aiJob.error="Task create failed"; return false; }
  return true;
}
inline bool cancelAIJob(){ if(!g_aiJob.running) return false; g_aiJob.canceled=true; return true; }