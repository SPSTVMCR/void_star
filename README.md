# VOID::STAR — Smart Sleep Lamp (ESP32 + ESP8266 Lux Node)

## What is this?
Made for a math STEM project at school: smart, Wi‑Fi‑controlled RGB lamp (ESP32-S3-DevKitC-1) that adapts brightness to ambient light measured by a separate NodeMCU 1.0 ESP8266 lux node.

---

## Features
- ESP32 lamp (WS2812/Neopixel via WS2812FX) with web UI (color, brightness, effects)
- Ambient‑adaptive mode (“a mimir mode”) with adjustable brightness range in the UI
- ESP‑NOW lux feed from an ESP8266 + BH1750 sensor (low power, no router needed)
- Two deployment modes for the lux node:
  - Broadcast (channel 1) — matches ESP32 AP mode
  - Pairing (custom channel) — matches ESP32’s router channel in STA mode

---

## Project Wiki

- Extensive documentation by DeepWiki: https://deepwiki.com/SPSTVMCR/void_star
- Or follow the instructions below for simple usage.

## Project structure
```
.
├── SleepLamp_ESP32/                ← ESP32 lamp (Arduino sketch)
│   ├── SleepLamp_ESP32.ino
│   ├── config.h                    ← pins, LED count, defaults, Wi‑Fi SSIDs, prefs keys
│   ├── fs_select.h                 ← FS macros (LittleFS)
│   ├── led_control.h               ← WS2812FX + Mimir logic (gamma, smoothing)
│   ├── web_server.h                ← Async Web Server routes (REST)
│   ├── mimir_tuning.h              ← tunables (gamma, min/max range, smoothing)
│   └── data/                       ← LittleFS web assets for ESP32
│       ├── index.html
│       ├── script.js
│       ├── bootstrap.min.css
│       └── bootstrap.bundle.min.js
└── ESP8266_BH1750_ESPNow_Web/      ← ESP8266 lux node (Arduino sketch)
    └── ESP8266_BH1750_ESPNow_Web.ino
```

---

## How to build and upload (Arduino IDE on Linux)

### 1) Install board packages
- File → Preferences → “Additional Boards Manager URLs”:
  - ESP32: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
  - ESP8266: https://arduino.esp8266.com/stable/package_esp8266com_index.json
- Tools → Board → Boards Manager…
  - Install “ESP32 by Espressif Systems”
  - Install “ESP8266 by ESP8266 Community”

### 2) Install libraries (Tools → Manage Libraries…)
- For ESP32 lamp:
  - “ESP Async WebServer” (me-no-dev or mathieucarbou fork)
  - “AsyncTCP” (mathieucarbou)
  - “WS2812FX”
  - “FastLED” (only if your config uses it)
- For ESP8266 lux node:
  - “BH1750” (by claws)

### 3) ESP32 lamp build + upload
- Open folder: SleepLamp_ESP32/
- Select board: Tools → Board → ESP32 Arduino → your ESP32 (e.g., “ESP32S3 Dev Module”)
- Partition Scheme: choose one with LittleFS (or default)
- Upload web assets to LittleFS:
  - Install the ESP32 LittleFS uploader tool (“ESP32FS”) into ~/Arduino/tools/ if you don’t have it
  - Tools → ESP32 Sketch Data Upload
- Upload firmware: Sketch → Upload (Ctrl+U)

### 4) ESP8266 lux node build + upload
- Open folder: ESP8266_BH1750_ESPNow_Web/
- Select board: Tools → Board → “NodeMCU 1.0 (ESP‑12E Module)”
- Upload firmware: Sketch → Upload

---

## Hardware and wiring
### Requirements
- ESP32-S3-DevKitC-1 (or similar ESP32 with enough pins)
- WS2812/NeoPixel LED strip (5V recommended. This project was tested with 61 LED/m)
- ESP8266 NodeMCU 1.0 (ESP-12E)
- BH1750 light sensor module
- Proper power supplies for ESP32, ESP8266, and LED strip (Can use a USB power bank for portable use)
- Level shifter (optional, but recommended for WS2812 data line)
- Capacitor (1000 µF, 12V or higher) and resistor (270 Ω) for LED strip per vendor guidelines

### ESP32 lamp
- LED strip (WS2812/NeoPixel) → connect DIN to `LED_PIN` from config.h, common GND, proper 5V power supply
- Recommended: level shifter for data line, and capacitor/resistor per vendor guidelines

### ESP8266 lux node (NodeMCU + BH1750)
- BH1750 VCC → 3.3V
- BH1750 GND → GND
- BH1750 SDA → D2 (GPIO4)
- BH1750 SCL → D1 (GPIO5)
- Power from stable 3.3V

---

## Usage

### ESP32 lamp web UI
- AP mode: connect to the lamp’s AP (see `AP_SSID`/`AP_PASSWORD` in config.h), open http://192.168.4.1/
- STA mode: connect the lamp to your Wi‑Fi via the UI; then browse to the shown IP (also visible on Serial)
- UI allows:
  - Color, brightness, power, effect selection
  - Toggle “Mimir mode” (ambient‑adaptive)
  - Adjust Mimir brightness range (min/max) and it persists
  - Switch Wi‑Fi mode (AP/STA). In STA mode, a Router Info panel shows SSID/RSSI/Channel/IP etc.

### ESP8266 lux node web UI
- Joins/hosts SoftAP “LuxNode‑8266” (password: `luxsetup`) at http://192.168.4.1/
- Two modes:
  - Broadcast (Ch 1): one‑tap button sets ESP‑NOW to channel 1 (use when ESP32 runs AP mode on channel 1)
  - Pairing: open the form, type a channel (1–13) matching the ESP32’s router channel (seen in lamp UI Router Info), Save
- The node sends a float lux value every ~2 seconds via ESP‑NOW broadcast

### ESP‑NOW channel rules (important)
- Packets only arrive if both devices share the same RF channel
  - ESP32 AP mode: channel is typically 1 → put lux node in Broadcast (1)
  - ESP32 STA mode: channel = your router’s 2.4 GHz channel → set lux node Pairing channel to match

---

## Configuration
- See `SleepLamp_ESP32/config.h` for:
  - LED_PIN, NUM_LEDS, defaults (color/brightness/effect)
  - Wi‑Fi AP SSID/PASS, preference keys, smoothing constants, LUX_MIN/MAX
- See `SleepLamp_ESP32/mimir_tuning.h` for:
  - `MIMIR_GAMMA`, `MIMIR_ALPHA`, `MIMIR_MIN_STEP`, default min/max range
- ESP8266 lux node: channel is persisted in EEPROM and applied at boot

---

## Troubleshooting
- No lux updates:
  - Verify ESP‑NOW channel match (see Router Info panel in ESP32 STA mode, or use Broadcast=1)
- UI doesn’t show or static files missing:
  - Re‑upload ESP32 “Sketch Data Upload” (LittleFS) after changing files in `SleepLamp_ESP32/data/`
- ‘D1/D2 not declared’ while compiling NodeMCU:
  - Select board “NodeMCU 1.0 (ESP‑12E Module)” or edit pins to raw GPIO numbers (GPIO5=SCL, GPIO4=SDA)
- Serial monitor baud:
  - 115200 for both projects
