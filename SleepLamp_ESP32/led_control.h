#pragma once
#include <Arduino.h>
#include <math.h>
#include <WS2812FX.h>
#include "config.h"
#include "mimir_tuning.h"

namespace LedControl {

// WS2812FX to drive instead of neopixel lib
WS2812FX ws(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// State
static uint32_t s_color = DEFAULT_COLOR_HEX;
static uint8_t s_currentBrightness = DEFAULT_BRIGHTNESS;
static uint8_t s_targetBrightness = DEFAULT_BRIGHTNESS;
static uint8_t s_savedBrightness = DEFAULT_BRIGHTNESS;  // restore when turning back on
static uint16_t s_effectId = DEFAULT_EFFECT_ID;
static bool s_isOn = DEFAULT_ON;
static bool s_mimir = DEFAULT_MIMIR;
static float s_lastLux = 0.0f;

// Mimir range
static uint8_t s_mimirMin = MIMIR_BRIGHT_MIN;
static uint8_t s_mimirMax = MIMIR_BRIGHT_MAX;

static inline uint8_t clampU8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

uint32_t hexToColor(const String& hex) {
  String s = hex;
  if (s.startsWith("#")) s.remove(0, 1);
  if (s.length() != 6) return s_color;
  uint32_t c = (uint32_t)strtoul(s.c_str(), nullptr, 16);
  return c;
}

void applyColorToFX(uint32_t color) {
  ws.setColor(color);
}

void init() {
  pinMode(LED_PIN, OUTPUT);

  ws.init();
  ws.setBrightness(s_currentBrightness);  // native brightness
  ws.setMode(s_effectId);
  applyColorToFX(s_color);

  if (s_isOn) {
    ws.start();
  } else {
    ws.stop();
    ws.setBrightness(0);
    ws.service();
  }
}

// Brightness smoothing
void smoothBrightness() {
  // If off, force target to 0 (stay off)
  if (!s_isOn) {
    s_targetBrightness = 0;
  }

  // Faster alpha when on mimir mode
  float alpha = s_mimir ? MIMIR_ALPHA : SMOOTHING_ALPHA;
  float y = (float)s_currentBrightness + alpha * ((float)s_targetBrightness - (float)s_currentBrightness);
  uint8_t newB = clampU8((int)roundf(y));
  if (newB != s_currentBrightness) {
    s_currentBrightness = newB;
    ws.setBrightness(s_currentBrightness);
  }
}

void tick() {
  // In Mimir mode, update target using gamma curve and min step threshold
  if (s_mimir) {
    float lux = s_lastLux;
    float cl = constrain(lux, LUX_MIN, LUX_MAX);
    float t = (cl - LUX_MIN) / (float)(LUX_MAX - LUX_MIN);  // 0..1
    t = powf(t, MIMIR_GAMMA);                               // gamma curve

    // Use dynamic UI-adjustable range
    int mapped = (int)roundf((float)s_mimirMin + t * (float)(s_mimirMax - s_mimirMin));
    mapped = clampU8(mapped);

    if (abs(mapped - (int)s_targetBrightness) >= MIMIR_MIN_STEP) {
      s_targetBrightness = (uint8_t)mapped;
    }
  }

  smoothBrightness();
  ws.service();
}

// Set target brightness only (does not auto power on)
void setTargetBrightness(uint8_t b) {
  s_targetBrightness = b;
  // Save nonzero brightness for later restore
  if (b > 0) s_savedBrightness = b;
}

uint8_t getTargetBrightness() {
  return s_targetBrightness;
}
uint8_t getCurrentBrightness() {
  return s_currentBrightness;
}
uint8_t getSavedBrightness() {
  return s_savedBrightness;
}

void setColor(uint32_t color) {
  s_color = color;
  applyColorToFX(s_color);
}

uint32_t getColor() {
  return s_color;
}

void setEffect(uint16_t effectId) {
  s_effectId = effectId;
  ws.setMode(s_effectId);
  applyColorToFX(s_color);
  if (s_isOn) ws.start();
}

uint16_t getEffect() {
  return s_effectId;
}

void setMimir(bool m) {
  s_mimir = m;
}
bool getMimir() {
  return s_mimir;
}

void updateLux(float lux) {
  s_lastLux = lux;
}
float getLux() {
  return s_lastLux;
}

// Power control
void setOn(bool on) {
  if (on == s_isOn) return;

  if (on) {
    // Restore last nonzero brightness
    if (s_savedBrightness == 0) s_savedBrightness = DEFAULT_BRIGHTNESS;
    s_targetBrightness = s_savedBrightness;
    s_isOn = true;
    ws.start();
    // Apply
    s_currentBrightness = s_targetBrightness;
    ws.setBrightness(s_currentBrightness);
  } else {
    // Save current target if nonzero, then turn off
    if (s_targetBrightness > 0) s_savedBrightness = s_targetBrightness;
    s_isOn = false;
    s_targetBrightness = 0;
    ws.setBrightness(0);
    ws.stop();
    ws.service();
  }
}

bool getOn() {
  return s_isOn;
}

bool toggle() {
  setOn(!s_isOn);
  return s_isOn;
}

// Mimir range getters/setters
void setMimirRange(uint8_t minB, uint8_t maxB) {
  if (minB > maxB) {
    uint8_t tmp = minB;
    minB = maxB;
    maxB = tmp;
  }
  s_mimirMin = minB;
  s_mimirMax = maxB;
  // Clamp current/target within range if Mimir active
  if (s_mimir) {
    if (s_targetBrightness < s_mimirMin) s_targetBrightness = s_mimirMin;
    if (s_targetBrightness > s_mimirMax) s_targetBrightness = s_mimirMax;
  }
}
uint8_t getMimirMin() {
  return s_mimirMin;
}
uint8_t getMimirMax() {
  return s_mimirMax;
}

String getEffectName(uint16_t id) {
  const __FlashStringHelper* nm = ws.getModeName(id);
  String s;
  if (nm) s = String(nm);
  else s = "Unknown";
  return s;
}

String jsonStatus(const String& wifiMode) {
  char buf[512];
  uint32_t col = getColor();
  uint8_t r = (col >> 16) & 0xFF;
  uint8_t g = (col >> 8) & 0xFF;
  uint8_t b = (col)&0xFF;

  String effectName = getEffectName(getEffect());

  snprintf(buf, sizeof(buf),
           "{\"color\":\"%02X%02X%02X\",\"brightness\":%u,\"current_brightness\":%u,"
           "\"saved_brightness\":%u,"
           "\"effect_id\":%u,\"effect_name\":\"%s\",\"on\":%s,\"mimir\":%s,"
           "\"lux\":%.2f,\"wifi_mode\":\"%s\",\"mimir_min\":%u,\"mimir_max\":%u}",
           r, g, b,
           getTargetBrightness(),
           getCurrentBrightness(),
           getSavedBrightness(),
           getEffect(),
           effectName.c_str(),
           getOn() ? "true" : "false",
           getMimir() ? "true" : "false",
           getLux(),
           wifiMode.c_str(),
           getMimirMin(), getMimirMax());
  return String(buf);
}

// ---------- Diagnostics ----------
void testFillHex(uint32_t color, uint8_t brightness) {
  bool wasOn = s_isOn;
  ws.stop();
  ws.setMode(FX_MODE_STATIC);
  ws.setColor(color);
  ws.setBrightness(brightness);
  for (int i = 0; i < 3; ++i) {
    ws.service();
    delay(10);
  }
  if (wasOn) {
    ws.setMode(s_effectId);
    ws.setBrightness(s_currentBrightness);
    ws.start();
  } else {
    ws.setBrightness(0);
  }
}

void selfTestRGB(uint8_t brightness) {
  bool wasOn = s_isOn;
  uint16_t prevMode = s_effectId;
  uint8_t prevB = s_currentBrightness;

  ws.stop();
  ws.setMode(FX_MODE_STATIC);
  ws.setBrightness(brightness);

  ws.setColor(0xFF0000);
  for (int i = 0; i < 4; ++i) {
    ws.service();
    delay(100);
  }
  ws.setColor(0x00FF00);
  for (int i = 0; i < 4; ++i) {
    ws.service();
    delay(100);
  }
  ws.setColor(0x0000FF);
  for (int i = 0; i < 4; ++i) {
    ws.service();
    delay(100);
  }

  ws.setColor(0x000000);
  for (int i = 0; i < 2; ++i) {
    ws.service();
    delay(50);
  }

  // Restore
  ws.setMode(prevMode);
  ws.setBrightness(prevB);
  if (wasOn) ws.start();
}
}