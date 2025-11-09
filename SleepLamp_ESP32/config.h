#pragma once

// Pins and hardware
#define LED_PIN 2
#define BUTTON_PIN 0
#define NUM_LEDS 61
#define LED_COLOR_ORDER GRB
#define LED_CHIPSET WS2812B

// Defaults
#define DEFAULT_COLOR_HEX 0xFFA500
#define DEFAULT_BRIGHTNESS 64
#define DEFAULT_EFFECT_ID 0  // STATIC
#define DEFAULT_ON true
#define DEFAULT_MIMIR false

// Wi-Fi AP credentials
#define AP_SSID "VOIDSTAR"
#define AP_PASSWORD "esp32lamp"

// Preferences (NVS) keys
#define PREF_NAMESPACE "sleepLamp"
#define PREF_KEY_COLOR "color"
#define PREF_KEY_BRIGHTNESS "bright"
#define PREF_KEY_EFFECT "effect"
#define PREF_KEY_ON "on"
#define PREF_KEY_MIMIR "mimir"
#define PREF_KEY_WIFI_MODE "wifiMode"  // "AP" or "STA"
#define PREF_KEY_STA_SSID "staSsid"
#define PREF_KEY_STA_PASS "staPass"

// Brightness smoothing
#define SMOOTHING_ALPHA 0.15f  // smoothing factor (0..1)
#define BRIGHTNESS_MIN 0
#define BRIGHTNESS_MAX 255

// Mimir mapping parameters
#define LUX_MIN 0.0f
#define LUX_MAX 400.0f
#define MIMIR_BRIGHT_MIN 6    // dim minimum for sleep
#define MIMIR_BRIGHT_MAX 180  // cap brightness