/*
  WiFi Sentinel for ESP8266
  Three tools in one firmware, switched from the web dashboard:
    1. Deauth Guard   - detects deauth / disassoc flood attacks
    2. Crowd Radar    - estimates people nearby from probe requests
    3. Ghost Hunter   - finds new, hidden and look-alike (evil twin) access points

  Board : any ESP8266 (NodeMCU / Wemos D1 mini), ESP8266 core 3.x
  Join  : WiFi "WiFi-Sentinel" / password "pabel1234", open http://192.168.4.1
  Wiring: active buzzer + to D5 (GPIO14), - to GND. Built-in LED is used too.

  Use only on networks and in places where you have permission.
  Made by Tasfiqul Alam Pabel
*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
extern "C" {
#include "user_interface.h"
}
#include "dashboard.h"

#define AP_SSID    "WiFi-Sentinel"
#define AP_PASS    "pabel1234"
#define BUZZER_PIN 14   // D5
#define LED_PIN    2    // D4, active LOW

enum : uint8_t { MODE_DEAUTH = 0, MODE_RADAR = 1, MODE_GHOST = 2 };

ESP8266WebServer server(80);
uint8_t apMac[6];

// ---------- settings ----------
volatile uint8_t mode = MODE_DEAUTH;
uint8_t  cfgChannel   = 6;
uint8_t  cfgThreshold = 5;      // deauth frames per second
bool     cfgBuzzer    = true;
int8_t   cfgMinRssi   = -85;    // radar proximity filter
uint16_t cfgWindow    = 60;     // radar window, seconds
uint8_t  cfgDivisor   = 3;      // random MACs per person
bool     needApRestart = false;

// ---------- small helpers ----------
struct Ring {
  uint16_t v[60];
  uint8_t pos = 0;
  void push(uint16_t x) { v[pos] = x; pos = (pos + 1) % 60; }
  void clear() { memset(v, 0, sizeof(v)); pos = 0; }
};

void macStr(const uint8_t *m, char *out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

void jsonEsc(String &o, const char *s) {
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
}

void ringJson(String &o, const Ring &r) {
  o += '[';
  for (uint8_t i = 0; i < 60; i++) {
    if (i) o += ',';
    o += r.v[(r.pos + i) % 60];
  }
  o += ']';
}

// ---------- 1. Deauth Guard ----------
struct DeauthEvent { uint8_t src[6]; uint8_t dst[6]; int8_t rssi; uint16_t count; uint32_t at; };
#define EV_MAX 10
DeauthEvent events[EV_MAX];
uint8_t evHead = 0, evCount = 0;
volatile uint16_t deauthTick = 0;
volatile uint32_t deauthTotal = 0;
uint8_t lastSrc[6], lastDst[6];
volatile int8_t lastRssi = 0;
uint16_t deauthPps = 0;
uint32_t alertUntil = 0;
Ring deauthRing;

void addDeauthEvent(uint16_t pps) {
  uint32_t nowS = millis() / 1000;
  if (evCount) {
    DeauthEvent &l = events[(evHead + EV_MAX - 1) % EV_MAX];
    if (memcmp(l.src, lastSrc, 6) == 0 && nowS - l.at <= 3) {
      l.count += pps; l.at = nowS; l.rssi = lastRssi;
      return;
    }
  }
  DeauthEvent &e = events[evHead];
  memcpy(e.src, lastSrc, 6); memcpy(e.dst, lastDst, 6);
  e.rssi = lastRssi; e.count = pps; e.at = nowS;
  evHead = (evHead + 1) % EV_MAX;
  if (evCount < EV_MAX) evCount++;
}

// ---------- 2. Crowd Radar ----------
#define RADAR_MAX 80
struct Dev { uint32_t h; uint32_t last; int8_t rssi; bool rnd; };
Dev devs[RADAR_MAX];
uint32_t radarSalt = 0;
Ring radarRing;

uint32_t IRAM_ATTR hashMac(const uint8_t *m) {
  uint32_t h = 2166136261u ^ radarSalt;
  for (uint8_t i = 0; i < 6; i++) { h ^= m[i]; h *= 16777619u; }
  return h ? h : 1;
}

void IRAM_ATTR radarSeen(const uint8_t *mac, int8_t rssi) {
  if (mac[0] & 0x01) return;                       // multicast / broadcast
  if (memcmp(mac, apMac, 6) == 0) return;          // ourselves
  uint32_t h = hashMac(mac), now = millis();
  int found = -1, slot = -1;
  uint32_t oldest = 0xFFFFFFFF;
  for (uint8_t i = 0; i < RADAR_MAX; i++) {
    if (devs[i].h == h) { found = i; break; }
    if (devs[i].h == 0) { if (slot < 0 || oldest != 0) { slot = i; oldest = 0; } }
    else if (oldest != 0 && devs[i].last < oldest) { oldest = devs[i].last; slot = i; }
  }
  int k = found >= 0 ? found : slot;
  if (k < 0) return;
  devs[k].h = h; devs[k].last = now; devs[k].rssi = rssi; devs[k].rnd = (mac[0] & 0x02);
}

struct RadarStats { uint16_t stable, rnd, est, near, mid, far; };
RadarStats radarStats() {
  RadarStats s = {0, 0, 0, 0, 0, 0};
  uint32_t now = millis();
  for (uint8_t i = 0; i < RADAR_MAX; i++) {
    if (!devs[i].h) continue;
    if (now - devs[i].last > (uint32_t)cfgWindow * 1000UL) continue;
    if (devs[i].rssi < cfgMinRssi) continue;
    if (devs[i].rnd) s.rnd++; else s.stable++;
    if (devs[i].rssi > -60) s.near++; else if (devs[i].rssi > -75) s.mid++; else s.far++;
  }
  s.est = s.stable + (s.rnd + cfgDivisor - 1) / cfgDivisor;
  return s;
}

// ---------- sniffer callback (deauth + radar) ----------
void IRAM_ATTR snifferCb(uint8_t *buf, uint16_t len) {
  if (len < 28) return;
  int8_t rssi = (int8_t)buf[0];
  uint8_t fc0 = buf[12], fc1 = buf[13];
  if (mode == MODE_DEAUTH) {
    if (fc0 == 0xC0 || fc0 == 0xA0) {               // deauth / disassoc
      deauthTick++; deauthTotal++;
      memcpy(lastDst, buf + 16, 6);
      memcpy(lastSrc, buf + 22, 6);
      lastRssi = rssi;
    }
  } else if (mode == MODE_RADAR) {
    if (fc0 == 0x40) {                               // probe request
      radarSeen(buf + 22, rssi);
    } else if ((fc0 & 0x0C) == 0x08 && (fc1 & 0x03) == 0x01) {   // data, station -> AP
      if (memcmp(buf + 16, apMac, 6) != 0) radarSeen(buf + 22, rssi);
    }
  }
}

// ---------- 3. Ghost Hunter ----------
#define AP_MAX 40
struct Ap {
  uint8_t bssid[6]; char ssid[33]; int8_t rssi; uint8_t ch; uint8_t enc;
  bool hidden, trusted, present; uint32_t first, last;
};
Ap aps[AP_MAX];
uint8_t apN = 0;
bool baselineLearned = false, scanning = false, scanRequested = true;
uint32_t lastScanStart = 0, ghostBeepUntil = 0;

const char *encName(uint8_t e) {
  switch (e) {
    case ENC_TYPE_NONE: return "Open";
    case ENC_TYPE_WEP:  return "WEP";
    case ENC_TYPE_TKIP: return "WPA";
    case ENC_TYPE_CCMP: return "WPA2";
    case ENC_TYPE_AUTO: return "WPA/WPA2";
  }
  return "?";
}

void processScan(int n) {
  uint32_t nowS = millis() / 1000;
  bool newFound = false;
  for (uint8_t i = 0; i < apN; i++) aps[i].present = false;
  for (int i = 0; i < n; i++) {
    uint8_t *b = WiFi.BSSID(i);
    if (!b) continue;
    int idx = -1;
    for (uint8_t j = 0; j < apN; j++) if (memcmp(aps[j].bssid, b, 6) == 0) { idx = j; break; }
    if (idx < 0) {
      if (apN >= AP_MAX) continue;
      idx = apN++;
      memcpy(aps[idx].bssid, b, 6);
      aps[idx].first = nowS;
      aps[idx].trusted = !baselineLearned;
      if (baselineLearned) newFound = true;
    }
    Ap &a = aps[idx];
    String s = WiFi.SSID(i);
    strncpy(a.ssid, s.c_str(), 32); a.ssid[32] = 0;
    a.rssi = WiFi.RSSI(i); a.ch = WiFi.channel(i);
    a.enc = WiFi.encryptionType(i); a.hidden = WiFi.isHidden(i);
    a.present = true; a.last = nowS;
  }
  baselineLearned = true;                // first scan becomes the trusted baseline
  if (newFound) ghostBeepUntil = millis() + 1200;
}

void ghostLoop() {
  if (scanning) {
    int n = WiFi.scanComplete();
    if (n >= 0) { processScan(n); WiFi.scanDelete(); scanning = false; }
    else if (n == -2) scanning = false;
  } else if (scanRequested || millis() - lastScanStart > 12000) {
    scanRequested = false;
    lastScanStart = millis();
    WiFi.scanNetworks(true, true);       // async, include hidden
    scanning = true;
  }
}

// ---------- mode / radio ----------
void applyMode(uint8_t m) {
  wifi_promiscuous_enable(0);
  mode = m;
  deauthTick = 0;
  deauthRing.clear(); radarRing.clear();
  if (m == MODE_GHOST) {
    scanRequested = true; scanning = false;
  } else {
    wifi_set_channel(cfgChannel);
    wifi_promiscuous_enable(1);
  }
}

void restartAp() {
  WiFi.softAP(AP_SSID, AP_PASS, cfgChannel);
  if (mode != MODE_GHOST) { wifi_set_channel(cfgChannel); wifi_promiscuous_enable(1); }
}

// ---------- once per second ----------
uint32_t lastSec = 0, secCount = 0;
void tickSecond() {
  secCount++;
  if (mode == MODE_DEAUTH) {
    uint16_t t = deauthTick; deauthTick = 0;
    deauthPps = t;
    deauthRing.push(t);
    if (t >= cfgThreshold) { addDeauthEvent(t); alertUntil = millis() + 4000; }
  } else if (mode == MODE_RADAR) {
    uint32_t now = millis();
    for (uint8_t i = 0; i < RADAR_MAX; i++)
      if (devs[i].h && now - devs[i].last > 300000UL) devs[i].h = 0;
    if (secCount % 5 == 0) radarRing.push(radarStats().est);
  }
}

void updateIndicators() {
  uint32_t now = millis();
  bool on = false;
  if (mode == MODE_DEAUTH && (int32_t)(alertUntil - now) > 0) on = (now / 150) % 2 == 0;
  else if (mode == MODE_GHOST && (int32_t)(ghostBeepUntil - now) > 0) on = (now / 100) % 2 == 0;
  digitalWrite(LED_PIN, on ? LOW : HIGH);
  digitalWrite(BUZZER_PIN, (on && cfgBuzzer) ? HIGH : LOW);
}

// ---------- web API ----------
void sendJson(const String &s) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void handleState() {
  String o; o.reserve(4600);
  o += "{\"mode\":"; o += mode;
  o += ",\"up\":"; o += millis() / 1000;
  o += ",\"ch\":"; o += cfgChannel;
  o += ",\"thr\":"; o += cfgThreshold;
  o += ",\"buz\":"; o += cfgBuzzer ? 1 : 0;
  o += ",\"rssi\":"; o += cfgMinRssi;
  o += ",\"win\":"; o += cfgWindow;
  o += ",\"div\":"; o += cfgDivisor;
  char m1[18], m2[18];

  if (mode == MODE_DEAUTH) {
    o += ",\"d\":{\"pps\":"; o += deauthPps;
    o += ",\"total\":"; o += deauthTotal;
    o += ",\"alert\":"; o += ((int32_t)(alertUntil - millis()) > 0) ? 1 : 0;
    o += ",\"hist\":"; ringJson(o, deauthRing);
    o += ",\"ev\":[";
    for (uint8_t i = 0; i < evCount; i++) {
      DeauthEvent &e = events[(evHead + EV_MAX - 1 - i) % EV_MAX];
      macStr(e.src, m1); macStr(e.dst, m2);
      if (i) o += ',';
      o += "{\"s\":\""; o += m1; o += "\",\"d\":\""; o += m2;
      o += "\",\"r\":"; o += e.rssi; o += ",\"c\":"; o += e.count;
      o += ",\"at\":"; o += e.at; o += '}';
    }
    o += "]}";
  } else if (mode == MODE_RADAR) {
    RadarStats s = radarStats();
    o += ",\"r\":{\"stable\":"; o += s.stable;
    o += ",\"rnd\":"; o += s.rnd;
    o += ",\"est\":"; o += s.est;
    o += ",\"near\":"; o += s.near;
    o += ",\"mid\":"; o += s.mid;
    o += ",\"far\":"; o += s.far;
    o += ",\"hist\":"; ringJson(o, radarRing);
    o += '}';
  } else {
    o += ",\"g\":{\"scanning\":"; o += scanning ? 1 : 0;
    o += ",\"aps\":[";
    for (uint8_t i = 0; i < apN; i++) {
      Ap &a = aps[i];
      bool twin = false;
      if (a.present && !a.hidden && a.ssid[0]) {
        for (uint8_t j = 0; j < apN; j++)
          if (j != i && aps[j].present && strcmp(aps[j].ssid, a.ssid) == 0 && aps[j].enc != a.enc) { twin = true; break; }
      }
      macStr(a.bssid, m1);
      if (i) o += ',';
      o += "{\"b\":\""; o += m1; o += "\",\"n\":\""; jsonEsc(o, a.ssid);
      o += "\",\"r\":"; o += a.rssi;
      o += ",\"c\":"; o += a.ch;
      o += ",\"e\":\""; o += encName(a.enc);
      o += "\",\"h\":"; o += a.hidden ? 1 : 0;
      o += ",\"nw\":"; o += (!a.trusted && a.present) ? 1 : 0;
      o += ",\"tw\":"; o += twin ? 1 : 0;
      o += ",\"p\":"; o += a.present ? 1 : 0;
      o += '}';
    }
    o += "]}";
  }
  o += '}';
  sendJson(o);
}

void handleMode() {
  int m = server.arg("m").toInt();
  if (m >= 0 && m <= 2) applyMode(m);
  sendJson("{\"ok\":1}");
}

void handleCfg() {
  if (server.hasArg("thr"))  cfgThreshold = constrain(server.arg("thr").toInt(), 1, 50);
  if (server.hasArg("buz"))  cfgBuzzer = server.arg("buz").toInt() == 1;
  if (server.hasArg("rssi")) cfgMinRssi = constrain(server.arg("rssi").toInt(), -95, -30);
  if (server.hasArg("win"))  cfgWindow = constrain(server.arg("win").toInt(), 30, 300);
  if (server.hasArg("div"))  cfgDivisor = constrain(server.arg("div").toInt(), 1, 8);
  if (server.hasArg("ch")) {
    uint8_t c = constrain(server.arg("ch").toInt(), 1, 13);
    if (c != cfgChannel) { cfgChannel = c; needApRestart = true; }
  }
  sendJson("{\"ok\":1}");
}

void handleBaseline() {
  for (uint8_t i = 0; i < apN; i++) if (aps[i].present) aps[i].trusted = true;
  baselineLearned = true;
  sendJson("{\"ok\":1}");
}

void handleClear() {
  if (mode == MODE_RADAR) { memset(devs, 0, sizeof(devs)); radarRing.clear(); }
  else if (mode == MODE_GHOST) { apN = 0; baselineLearned = false; scanRequested = true; }
  else { evCount = 0; evHead = 0; deauthTotal = 0; deauthRing.clear(); }
  sendJson("{\"ok\":1}");
}

void handleScan() { scanRequested = true; sendJson("{\"ok\":1}"); }

// ---------- setup / loop ----------
void setup() {
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  Serial.begin(115200);
  radarSalt = RANDOM_REG32;

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  WiFi.softAP(AP_SSID, AP_PASS, cfgChannel);
  WiFi.softAPmacAddress(apMac);
  wifi_set_promiscuous_rx_cb(snifferCb);

  server.on("/", []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/state", handleState);
  server.on("/api/mode", handleMode);
  server.on("/api/cfg", handleCfg);
  server.on("/api/baseline", handleBaseline);
  server.on("/api/clear", handleClear);
  server.on("/api/scan", handleScan);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();

  applyMode(MODE_DEAUTH);
  Serial.println(F("\nWiFi Sentinel ready: join WiFi-Sentinel, open http://192.168.4.1"));
}

void loop() {
  server.handleClient();
  if (needApRestart) { needApRestart = false; delay(100); restartAp(); }
  if (millis() - lastSec >= 1000) { lastSec += 1000; tickSecond(); }
  if (mode == MODE_GHOST) ghostLoop();
  updateIndicators();
}
