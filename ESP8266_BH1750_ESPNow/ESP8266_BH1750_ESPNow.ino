#include <Arduino.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <espnow.h>
extern "C" {
  #include "user_interface.h" // wifi_set_channel, wifi_get_channel
}
#include <EEPROM.h>
#include <BH1750.h>

#ifndef D2
  #define I2C_SDA_PIN 4
#else
  #define I2C_SDA_PIN D2
#endif
#ifndef D1
  #define I2C_SCL_PIN 5
#else
  #define I2C_SCL_PIN D1
#endif

static const char* AP_SSID = "LuxNode-8266";
static const char* AP_PASS = "luxsetup";
static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static const uint32_t SEND_INTERVAL_MS = 2000;
static const int MAX_SEND_FAILS = 5;
static const uint8_t CHANNEL_MIN = 1;
static const uint8_t CHANNEL_MAX = 13;

struct LuxPacket { float lux; };
struct Cfg {
  uint16_t magic;
  uint8_t  version;
  uint8_t  channel;
  uint8_t  reserved[13];
};
static const uint16_t CFG_MAGIC = 0xB175;
static const uint8_t  CFG_VER   = 1;

ESP8266WebServer server(80);
BH1750 lightMeter;
bool sensorReady = false;

Cfg cfg;
uint8_t currentChannel = 1;

volatile bool espnowReady = false;
volatile int consecutiveSendFails = 0;
unsigned long lastSendMs = 0;

float g_lastLux = 0.0f;
bool  g_lastSendOk = false;

void loadConfig();
void saveConfig();
void applyChannel(uint8_t ch);
void setupWeb();
void setupSensor();
void setupEspNow();
bool ensurePeer();
void reinitEspNow(const char* reason);
void onDataSent(uint8_t* mac_addr, uint8_t sendStatus);
void sendLuxReading();

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lux Node (ESP8266)</title>
<style>
  body { font-family: system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif; margin:1rem; color:#eee; background:#121212; }
  .card { background:#1e1e1e; border-radius:8px; padding:1rem; margin-bottom:1rem; }
  .row { display:flex; gap:1rem; flex-wrap:wrap; }
  .btn { padding:.5rem .75rem; border:1px solid #444; border-radius:6px; color:#eee; background:#2a2a2a; cursor:pointer; }
  .btn:hover { background:#333; }
  .btn-primary { border-color:#3a7; background:#296; }
  .btn-primary:hover { background:#2a8; }
  .muted { color:#aaa; font-size:.9rem; }
  .label { display:block; margin:.25rem 0; }
  input[type=number] { width:100%; padding:.4rem .5rem; border-radius:6px; border:1px solid #444; background:#111; color:#eee; }
  .grid { display:grid; grid-template-columns: 1fr 1fr; gap:.5rem 1rem; }
  .hidden { display:none; }
  .kv { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
</style>
</head><body>
  <h1>Lux Node (ESP8266)</h1>
  <div class="card">
    <h3>Mode</h3>
    <div class="row">
      <button id="btnBroadcast" class="btn btn-primary">Broadcast (Ch 1)</button>
      <button id="btnPairing" class="btn">Pairing</button>
      <button id="btnClosePair" class="btn hidden">Close</button>
    </div>
    <div id="pairingForm" class="hidden" style="margin-top:.75rem;">
      <label class="label" for="ch">ESP-NOW Channel (1–13)</label>
      <div class="row">
        <input id="ch" type="number" min="1" max="13" step="1" autocomplete="off">
        <button id="btnSaveCh" class="btn">Save Channel</button>
      </div>
      <div class="muted">Match the ESP32 router channel (STA mode) or use Broadcast (1) for ESP32 AP mode.</div>
    </div>
  </div>

  <div class="card">
    <h3>Status</h3>
    <div class="grid">
      <div class="muted">Mode</div><div id="st_mode" class="kv"></div>
      <div class="muted">Stored Channel</div><div id="st_ch" class="kv"></div>
      <div class="muted">RF Channel</div><div id="st_rfch" class="kv"></div>
      <div class="muted">AP SSID</div><div id="st_ap" class="kv"></div>
      <div class="muted">AP IP</div><div id="st_ip" class="kv"></div>
      <div class="muted">Device MAC</div><div id="st_mac" class="kv"></div>
      <div class="muted">Last Lux</div><div id="st_lux" class="kv"></div>
      <div class="muted">Last Send</div><div id="st_send" class="kv"></div>
    </div>
  </div>

<script>
let pairingFormOpen = false;
let channelEditing = false; // input focused
let channelDirty = false;   // user has typed a value different from saved

function fmtMode(ch) { return (Number(ch) === 1) ? 'Broadcast' : 'Pairing'; }

async function api(path, params) {
  const url = new URL(path, window.location.origin);
  if (params) for (const [k,v] of Object.entries(params)) url.searchParams.set(k, v);
  const res = await fetch(url.toString(), { cache:'no-store' });
  if (!res.ok) throw new Error('HTTP '+res.status);
  return res.json();
}

function showPairing(show) {
  document.getElementById('pairingForm').classList.toggle('hidden', !show);
  document.getElementById('btnClosePair').classList.toggle('hidden', !show);
}

async function refresh() {
  try {
    const st = await api('/get');
    document.getElementById('st_mode').textContent = fmtMode(st.channel);
    document.getElementById('st_ch').textContent = st.channel;
    document.getElementById('st_rfch').textContent = st.rf_ch;
    document.getElementById('st_ap').textContent = st.ap_ssid;
    document.getElementById('st_ip').textContent = st.ap_ip;
    document.getElementById('st_mac').textContent = st.mac;
    document.getElementById('st_lux').textContent = st.last_lux.toFixed(2);
    document.getElementById('st_send').textContent = st.last_send_ok ? 'OK' : '...';

    // Update channel input if not editing or dirty
    const chInput = document.getElementById('ch');
    if (!channelEditing && !channelDirty) {
      chInput.value = st.channel;
    } else {
      // If user finished typing and value equals stored channel, clear dirty
      if (chInput.value == String(st.channel)) channelDirty = false;
    }

    if (!pairingFormOpen) {
      showPairing(st.channel !== 1);
    }
  } catch(e) {
    console.warn(e);
  }
}

// event handlers
document.getElementById('btnBroadcast').addEventListener('click', async () => {
  try {
    await api('/setMode', { mode:'broadcast' });
    pairingFormOpen = false;
    channelDirty = false;
    channelEditing = false;
    showPairing(false);
    await refresh();
  } catch(e) {}
});

document.getElementById('btnPairing').addEventListener('click', () => {
  pairingFormOpen = true;
  showPairing(true);
});

document.getElementById('btnClosePair').addEventListener('click', () => {
  pairingFormOpen = false;
  channelEditing = false;
  channelDirty = false;
  showPairing(false);
});

document.getElementById('btnSaveCh').addEventListener('click', async () => {
  const chInput = document.getElementById('ch');
  const ch = Number(chInput.value || '1');
  if (ch < 1 || ch > 13) { alert('Channel must be 1..13'); return; }
  try {
    await api('/setChannel', { ch });
    channelEditing = false;
    channelDirty = false;
    pairingFormOpen = true;
    await refresh();
  } catch(e) {}
});

// Track editing state
const chField = document.getElementById('ch');
chField.addEventListener('focus', () => { channelEditing = true; });
chField.addEventListener('blur', () => {
  channelEditing = false;
});
chField.addEventListener('input', () => {
  channelDirty = true;
});

refresh();
setInterval(refresh, 2000);
</script>
</body></html>
)HTML";

void applyChannel(uint8_t ch) {
  if (ch < CHANNEL_MIN) ch = CHANNEL_MIN;
  if (ch > CHANNEL_MAX) ch = CHANNEL_MAX;
  if (currentChannel == ch) return;

  Serial.printf("[CFG] Applying channel %u\n", ch);

  WiFi.softAPdisconnect(true);
  delay(50);
  wifi_set_channel(ch);
  delay(10);
  WiFi.softAP(AP_SSID, AP_PASS, ch, false, 1);

  reinitEspNow("channel change");

  currentChannel = ch;
  saveConfig();
}

void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void handleGet() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  char macstr[18];
  snprintf(macstr, sizeof(macstr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  IPAddress ip = WiFi.softAPIP();
  char ipbuf[16]; snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u", ip[0],ip[1],ip[2],ip[3]);

  uint8_t rfCh = wifi_get_channel();

  char buf[288];
  snprintf(buf, sizeof(buf),
    "{\"channel\":%u,\"rf_ch\":%u,\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\",\"mac\":\"%s\","
    "\"last_lux\":%.2f,\"last_send_ok\":%s}",
    currentChannel, rfCh, AP_SSID, ipbuf, macstr,
    g_lastLux, g_lastSendOk ? "true":"false");
  server.send(200, "application/json", buf);
}

void handleSetMode() {
  if (!server.hasArg("mode")) { server.send(400, "application/json", "{\"error\":\"missing mode\"}"); return; }
  String m = server.arg("mode"); m.toLowerCase();
  if (m == "broadcast") {
    applyChannel(1);
    server.send(200, "application/json", "{\"ok\":true,\"mode\":\"broadcast\",\"channel\":1}");
  } else {
    server.send(400, "application/json", "{\"error\":\"invalid mode\"}");
  }
}

void handleSetChannel() {
  if (!server.hasArg("ch")) { server.send(400, "application/json", "{\"error\":\"missing ch\"}"); return; }
  int ch = server.arg("ch").toInt();
  if (ch < CHANNEL_MIN || ch > CHANNEL_MAX) {
    server.send(400, "application/json", "{\"error\":\"ch must be 1..13\"}");
    return;
  }
  applyChannel((uint8_t)ch);
  char buf[64]; snprintf(buf, sizeof(buf), "{\"ok\":true,\"channel\":%d}", ch);
  server.send(200, "application/json", buf);
}

void setupWeb() {
  WiFi.mode(WIFI_AP_STA);
  delay(10);
  wifi_set_channel(currentChannel);
  delay(10);
  WiFi.softAP(AP_SSID, AP_PASS, currentChannel, false, 1);
  Serial.printf("[AP] SSID=%s PASS=%s CH=%u IP=%s\n",
    AP_SSID, AP_PASS, currentChannel, WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/get", HTTP_GET, handleGet);
  server.on("/setMode", HTTP_GET, handleSetMode);
  server.on("/setChannel", HTTP_GET, handleSetChannel);
  server.onNotFound([](){ server.send(404, "application/json", "{\"error\":\"not found\"}"); });
  server.begin();
  Serial.println("[Web] Server started");
}

void setupEspNow() {
  espnowReady = false;

  if (esp_now_init() != 0) { Serial.println(F("[ESP-NOW] Init failed")); return; }
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(onDataSent);

  if (!ensurePeer()) { Serial.println(F("[ESP-NOW] add peer failed")); return; }

  espnowReady = true;
  consecutiveSendFails = 0;
  Serial.printf("[ESP-NOW] Ready. Channel=%u\n", currentChannel);
}

bool ensurePeer() {
  int rc = esp_now_add_peer((uint8_t*)BROADCAST_MAC, ESP_NOW_ROLE_SLAVE, currentChannel, NULL, 0);
  if (rc == 0) return true;
  esp_now_del_peer((uint8_t*)BROADCAST_MAC);
  delay(10);
  rc = esp_now_add_peer((uint8_t*)BROADCAST_MAC, ESP_NOW_ROLE_SLAVE, currentChannel, NULL, 0);
  return (rc == 0);
}

void reinitEspNow(const char* reason) {
  Serial.printf("[ESP-NOW] Reinit (%s)\n", reason ? reason : "unknown");
  esp_now_deinit();
  delay(20);
  wifi_set_channel(currentChannel);
  delay(10);
  setupEspNow();
}

void onDataSent(uint8_t* mac_addr, uint8_t sendStatus) {
  g_lastSendOk = (sendStatus == 0);
  if (sendStatus == 0) {
    consecutiveSendFails = 0;
    Serial.println(F("[ESP-NOW] Delivery: OK"));
  } else {
    consecutiveSendFails++;
    Serial.printf("[ESP-NOW] Delivery: FAIL (status=%u) (failCount=%d)\n", sendStatus, consecutiveSendFails);
  }
}

void setupSensor() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(10);

  sensorReady = false;
  Serial.printf("[BH1750] Init at 0x%02X ...\n", 0x23);
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire)) {
    sensorReady = true;
  } else {
    Serial.printf("[BH1750] Init at 0x%02X ...\n", 0x5C);
    if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x5C, &Wire)) {
      sensorReady = true;
    }
  }
  Serial.println(sensorReady ? F("[BH1750] OK") : F("[BH1750] FAILED"));
}

void sendLuxReading() {
  LuxPacket pkt;
  float lux = NAN;

  if (sensorReady) lux = lightMeter.readLightLevel();

  if (!sensorReady || isnan(lux) || lux < 0.0f || lux > 120000.0f) {
    Serial.printf("[BH1750] Read error (lux=%.2f)\n", lux);
    pkt.lux = -1.0f;
  } else {
    pkt.lux = lux;
  }

  g_lastLux = pkt.lux;

  if (!espnowReady) {
    Serial.println(F("[ESP-NOW] Not ready"));
    return;
  }
  int rc = esp_now_send((uint8_t*)BROADCAST_MAC, (uint8_t*)&pkt, sizeof(pkt));
  if (rc == 0) {
    Serial.printf("[SEND] lux=%.2f -> queued OK (ch=%u)\n", pkt.lux, currentChannel);
  } else {
    Serial.printf("[SEND] lux=%.2f -> queue FAIL (rc=%d)\n", pkt.lux, rc);
    consecutiveSendFails++;
  }
}

void loadConfig() {
  EEPROM.begin(64);
  EEPROM.get(0, cfg);
  if (cfg.magic != CFG_MAGIC || cfg.version != CFG_VER) {
    cfg.magic = CFG_MAGIC;
    cfg.version = CFG_VER;
    cfg.channel = 1;
    EEPROM.put(0, cfg);
    EEPROM.commit();
  }
  currentChannel = cfg.channel;
}

void saveConfig() {
  cfg.magic = CFG_MAGIC;
  cfg.version = CFG_VER;
  cfg.channel = currentChannel;
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println(F("[BOOT] ESP8266 Lux Node + Web UI"));

  loadConfig();

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  delay(10);
  wifi_set_channel(currentChannel);

  setupWeb();
  setupSensor();
  setupEspNow();

  lastSendMs = millis();
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = now;
    sendLuxReading();
  }

  if (!espnowReady || consecutiveSendFails >= MAX_SEND_FAILS) {
    reinitEspNow(!espnowReady ? "not ready" : "send fails");
  }

  delay(2);
}