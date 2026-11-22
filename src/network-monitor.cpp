#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

extern "C" {
  #include "user_interface.h"
  int wifi_send_pkt_freedom(uint8 *buf, int len, bool sys_seq);
}

const char* AP_SSID = "ESP8266-RF-Monitor";
const char* AP_PASS = "12345678";

ESP8266WebServer server(80);

volatile uint32_t liveCounts[14] = {0};
volatile uint32_t liveMgmtCounts[14] = {0};
uint32_t lastScanCounts[14] = {0};
uint32_t lastMgmtCounts[14] = {0};

uint8_t currentChannel = 1;
unsigned long lastHopTime = 0;
unsigned long lastBeaconTime = 0;
uint16_t hopIntervalMs = 220;
bool isScanning = true;
uint8_t bestChannel = 1;

bool alertActive = false;
String alertReason = "Normal";
String baseSsid = "HomeNet";
bool beaconFloodEnabled = false;
uint8_t beaconIndex = 0;
const uint8_t BEACON_COUNT = 12; // 10-12 variations

// Promiscuous Callback
void ICACHE_RAM_ATTR snifferCallback(uint8_t *buf, uint16_t len) {
  if (!isScanning || buf == nullptr || len < 24) return;
  liveCounts[currentChannel]++;

  uint8_t fc0 = buf[0];
  uint8_t frameType = (fc0 >> 2) & 0x03;
  uint8_t subtype = (fc0 >> 4) & 0x0F;

  if (frameType == 0 && (subtype == 12 || subtype == 10)) {
    liveMgmtCounts[currentChannel]++;
  }
}

// Send one 802.11 Beacon frame with distinct MAC and SSID suffix
void sendSingleBeacon(uint8_t idx, uint8_t ch) {
  String ssidStr = baseSsid + "_" + (idx + 1 < 10 ? "0" : "") + String(idx + 1);
  if (ssidStr.length() > 32) ssidStr = ssidStr.substring(0, 32);
  uint8_t ssidLen = ssidStr.length();

  // Base Beacon template (Channel 1)
  uint8_t packet[128] = {
    0x80, 0x00,             // Frame Control: Beacon
    0x00, 0x00,             // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination: Broadcast
    0x02, 0x11, 0x22, 0x33, 0x44, 0x00, // Source MAC (byte 5 updated per idx)
    0x02, 0x11, 0x22, 0x33, 0x44, 0x00, // BSSID
    0x00, 0x00,             // Sequence control
    // Timestamp (8 bytes)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00,             // Beacon interval (100 TU)
    0x01, 0x04,             // Capability info (ESS)
    0x00, ssidLen           // SSID Tag + Length
  };

  packet[15] = 0x10 + idx;
  packet[21] = 0x10 + idx;

  int pos = 38;
  for (uint8_t i = 0; i < ssidLen; i++) {
    packet[pos++] = (uint8_t)ssidStr[i];
  }

  // Supported rates tag
  packet[pos++] = 0x01;
  packet[pos++] = 0x04;
  packet[pos++] = 0x82;
  packet[pos++] = 0x84;
  packet[pos++] = 0x8B;
  packet[pos++] = 0x96;

  // DS Parameter Set (Channel)
  packet[pos++] = 0x03;
  packet[pos++] = 0x01;
  packet[pos++] = ch;

  wifi_set_channel(ch);
  wifi_send_pkt_freedom(packet, pos, 0);
}

void evaluateAlerts() {
  alertActive = false;
  alertReason = "Normal";

  uint32_t totalPkts = 0;
  uint32_t totalDeauth = 0;
  uint32_t minTraffic = 0xFFFFFFFF;
  uint8_t bestCh = 1;

  for (uint8_t ch = 1; ch <= 13; ch++) {
    totalPkts += lastScanCounts[ch];
    totalDeauth += lastMgmtCounts[ch];
    if (lastScanCounts[ch] < minTraffic) {
      minTraffic = lastScanCounts[ch];
      bestCh = ch;
    }
  }

  bestChannel = bestCh;

  if (totalDeauth >= 3) {
    alertActive = true;
    alertReason = "Deauth/Disassoc burst detected (" + String(totalDeauth) + " pkts)";
    return;
  }

  if (totalPkts > 1400) {
    alertActive = true;
    alertReason = "High RF packet spike (" + String(totalPkts) + " pkts/cycle)";
  }
}

String escapeJson(const String& s) {
  String out = "";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP8266 RF Monitor & Beacon Control</title>
<style>
  :root {
    --bg: #0d1117; --card: #161b22; --border: #30363d;
    --text: #e6edf3; --muted: #8b949e; --accent: #2f81f7;
    --good: #2ea043; --warn: #d29922; --high: #f85149;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; font-family: system-ui, sans-serif; }
  body { background: var(--bg); color: var(--text); padding: 16px; }
  .wrap { max-width: 840px; margin: 0 auto; }
  .header { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px; margin-bottom: 14px; }
  .title { font-size: 1.2rem; font-weight: 700; }
  .badge { background: rgba(46,160,67,0.15); color: #56d364; border: 1px solid rgba(46,160,67,0.4); padding: 6px 12px; border-radius: 999px; font-size: 0.85rem; font-weight: 600; }
  .alert-banner {
    display: none; background: rgba(248,81,73,0.15); border: 1px solid rgba(248,81,73,0.5);
    color: #ff7b72; padding: 12px 14px; border-radius: 10px; margin-bottom: 14px; font-weight: 600;
  }
  .alert-banner.show { display: block; }
  .card { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 16px; margin-bottom: 14px; }
  .row { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; justify-content: space-between; }
  .btn-group { display: flex; gap: 8px; flex-wrap: wrap; }
  button {
    background: #21262d; color: var(--text); border: 1px solid var(--border);
    padding: 8px 14px; border-radius: 8px; cursor: pointer; font-weight: 600; font-size: 0.88rem;
  }
  button.primary { background: var(--accent); border-color: var(--accent); color: #fff; }
  button.danger { background: rgba(248,81,73,0.2); border-color: rgba(248,81,73,0.5); color: #ff7b72; }
  input[type="text"] {
    background: #0d1117; color: var(--text); border: 1px solid var(--border);
    padding: 7px 10px; border-radius: 8px; font-size: 0.88rem; width: 170px;
  }
  .chart { display: grid; grid-template-columns: repeat(13, 1fr); gap: 6px; align-items: end; height: 210px; padding-top: 22px; border-bottom: 1px solid var(--border); }
  .col { display: flex; flex-direction: column; align-items: center; height: 100%; justify-content: flex-end; }
  .val { font-size: 0.7rem; color: var(--muted); margin-bottom: 4px; }
  .bar { width: 100%; max-width: 36px; min-height: 4px; border-radius: 6px 6px 0 0; background: var(--accent); transition: height 0.25s ease; }
  .ch-label { margin-top: 8px; font-size: 0.78rem; font-weight: 600; color: var(--muted); }
  .best-tag { color: #56d364; }
  .meta { display: flex; justify-content: space-between; margin-top: 12px; font-size: 0.82rem; color: var(--muted); flex-wrap: wrap; gap: 8px; }
  .note { margin-top: 10px; font-size: 0.8rem; color: var(--muted); line-height: 1.45; }
  code { color: #79c0ff; }
</style>
</head>
<body>
<div class="wrap">
  <div class="header">
    <div class="title">ESP8266 RF Monitor & 12-SSID Beacon</div>
    <div class="badge" id="bestChBadge">Least Crowded: CH --</div>
  </div>

  <div class="alert-banner" id="alertBanner">
    ALERT: <span id="alertText">Anomaly detected</span>
  </div>

  <div class="card row">
    <div class="btn-group">
      <button class="primary" id="toggleBtn" onclick="toggleScan()">Pause Scan</button>
      <button onclick="resetPeak()">Reset Scale</button>
    </div>
    <div class="btn-group" style="align-items:center;">
      <label style="font-size:0.85rem;color:var(--muted);">Hop:</label>
      <input type="range" id="hopRange" min="120" max="600" step="40" value="220" onchange="updateSpeed(this.value)">
      <span id="hopVal" style="font-size:0.85rem;color:var(--muted);">220ms</span>
    </div>
  </div>

  <div class="card">
    <div class="row">
      <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap;">
        <label for="baseInput" style="font-size:0.85rem;color:var(--muted);">Base Name:</label>
        <input type="text" id="baseInput" maxlength="20" placeholder="e.g. HomeNet">
        <button onclick="saveBaseName()">Save Name</button>
      </div>
      <button class="danger" id="beaconBtn" onclick="toggleBeacons()">12 Beacons: OFF</button>
    </div>
    <div class="note">
      Generates 12 beacon names like <code>HomeNet_01</code> .. <code>HomeNet_12</code>. Keep your real router SSID distinct so you can spot it easily. Note: Beacon broadcast does not block router attacks or alter WPA2/WPA3 encryption.
    </div>
  </div>

  <div class="card">
    <div class="chart" id="chart"></div>
    <div class="meta">
      <span id="statusText">Status: Initializing...</span>
      <span id="peakText">Scale Max: 50 pkts</span>
    </div>
  </div>
</div>

<script>
let peakScale = 50;
let scanning = true;
let beaconOn = false;

const chartEl = document.getElementById('chart');
for (let ch = 1; ch <= 13; ch++) {
  const col = document.createElement('div');
  col.className = 'col';
  col.innerHTML = `
    <div class="val" id="v-${ch}">0</div>
    <div class="bar" id="b-${ch}" style="height:4px;"></div>
    <div class="ch-label" id="l-${ch}">C${ch}</div>
  `;
  chartEl.appendChild(col);
}

function barColor(ratio) {
  if (ratio > 0.7) return 'var(--high)';
  if (ratio > 0.35) return 'var(--warn)';
  return 'var(--good)';
}

async function fetchData() {
  try {
    const res = await fetch('/data');
    const data = await res.json();

    scanning = data.scanning;
    beaconOn = data.beaconOn;
    document.getElementById('toggleBtn').textContent = scanning ? 'Pause Scan' : 'Resume Scan';
    document.getElementById('beaconBtn').textContent = `12 Beacons: ${beaconOn ? 'ON' : 'OFF'}`;
    document.getElementById('statusText').textContent =
      `Status: ${scanning ? 'Scanning' : 'Paused'} | CH ${data.currentCh} | Deauth/Mgmt: ${data.deauthTotal}`;
    document.getElementById('bestChBadge').textContent = `Least Crowded: CH ${data.bestCh}`;
    document.getElementById('hopRange').value = data.hopMs;
    document.getElementById('hopVal').textContent = `${data.hopMs}ms`;

    if (document.activeElement !== document.getElementById('baseInput')) {
      document.getElementById('baseInput').value = data.baseSsid;
    }

    const banner = document.getElementById('alertBanner');
    if (data.alert) {
      banner.classList.add('show');
      document.getElementById('alertText').textContent = data.alertReason;
    } else {
      banner.classList.remove('show');
    }

    const maxVal = Math.max(...data.counts);
    if (maxVal > peakScale) peakScale = Math.ceil(maxVal * 1.2);
    document.getElementById('peakText').textContent = `Scale Max: ${peakScale} pkts`;

    for (let ch = 1; ch <= 13; ch++) {
      const count = data.counts[ch - 1];
      const ratio = peakScale > 0 ? Math.min(count / peakScale, 1) : 0;
      const bar = document.getElementById(`b-${ch}`);
      const val = document.getElementById(`v-${ch}`);
      const lbl = document.getElementById(`l-${ch}`);

      bar.style.height = `${Math.max(ratio * 100, 2)}%`;
      bar.style.background = barColor(ratio);
      val.textContent = count;

      if (ch === data.bestCh) {
        lbl.className = 'ch-label best-tag';
        lbl.textContent = `★${ch}`;
      } else {
        lbl.className = 'ch-label';
        lbl.textContent = `C${ch}`;
      }
    }
  } catch (e) {
    document.getElementById('statusText').textContent = 'Status: Reconnecting...';
  }
}

async function toggleScan() {
  await fetch(`/control?scan=${scanning ? 0 : 1}`);
  fetchData();
}

async function updateSpeed(ms) {
  document.getElementById('hopVal').textContent = `${ms}ms`;
  await fetch(`/control?hop=${ms}`);
}

async function saveBaseName() {
  const val = encodeURIComponent(document.getElementById('baseInput').value.trim() || 'HomeNet');
  await fetch(`/control?base=${val}`);
  fetchData();
}

async function toggleBeacons() {
  await fetch(`/control?beacon=${beaconOn ? 0 : 1}`);
  fetchData();
}

function resetPeak() {
  peakScale = 50;
  fetchData();
}

setInterval(fetchData, 900);
fetchData();
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  uint32_t deauthSum = 0;
  for (uint8_t ch = 1; ch <= 13; ch++) deauthSum += lastMgmtCounts[ch];

  String json = "{";
  json += "\"scanning\":" + String(isScanning ? "true" : "false") + ",";
  json += "\"beaconOn\":" + String(beaconFloodEnabled ? "true" : "false") + ",";
  json += "\"currentCh\":" + String(currentChannel) + ",";
  json += "\"bestCh\":" + String(bestChannel) + ",";
  json += "\"hopMs\":" + String(hopIntervalMs) + ",";
  json += "\"deauthTotal\":" + String(deauthSum) + ",";
  json += "\"alert\":" + String(alertActive ? "true" : "false") + ",";
  json += "\"alertReason\":\"" + escapeJson(alertReason) + "\",";
  json += "\"baseSsid\":\"" + escapeJson(baseSsid) + "\",";
  json += "\"counts\":[";
  for (uint8_t ch = 1; ch <= 13; ch++) {
    json += String(lastScanCounts[ch]);
    if (ch < 13) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleControl() {
  if (server.hasArg("scan")) {
    isScanning = (server.arg("scan").toInt() == 1);
    wifi_promiscuous_enable(isScanning ? 1 : 0);
  }
  if (server.hasArg("hop")) {
    int ms = server.arg("hop").toInt();
    if (ms >= 120 && ms <= 600) hopIntervalMs = (uint16_t)ms;
  }
  if (server.hasArg("base")) {
    String b = server.arg("base");
    b.trim();
    if (b.length() > 0 && b.length() <= 20) baseSsid = b;
  }
  if (server.hasArg("beacon")) {
    beaconFloodEnabled = (server.arg("beacon").toInt() == 1);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(snifferCallback);
  wifi_promiscuous_enable(1);
  wifi_set_channel(currentChannel);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/control", handleControl);
  server.begin();
}

void loop() {
  server.handleClient();

  if (beaconFloodEnabled && (millis() - lastBeaconTime >= 65)) {
    lastBeaconTime = millis();
    sendSingleBeacon(beaconIndex, currentChannel);
    beaconIndex = (beaconIndex + 1) % BEACON_COUNT;
    wifi_set_channel(currentChannel);
  }

  if (isScanning && (millis() - lastHopTime >= hopIntervalMs)) {
    lastHopTime = millis();

    noInterrupts();
    lastScanCounts[currentChannel] = liveCounts[currentChannel];
    lastMgmtCounts[currentChannel] = liveMgmtCounts[currentChannel];
    liveCounts[currentChannel] = 0;
    liveMgmtCounts[currentChannel] = 0;
    interrupts();

    currentChannel++;
    if (currentChannel > 13) {
      currentChannel = 1;
      evaluateAlerts();
    }

    wifi_set_channel(currentChannel);
  }
}
