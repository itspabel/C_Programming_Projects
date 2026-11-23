#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>

extern "C" {
  #include "user_interface.h"
  int wifi_send_pkt_freedom(uint8 *buf, int len, bool sys_seq);
}

const char* AP_SSID = "ESP8266-RF-Monitor";
const char* AP_PASS = "12345678";
const char* ADMIN_PIN = "1234"; // নিজের পছন্দমতো PIN বদলে নিন
const byte DNS_PORT = 53;

IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
ESP8266WebServer server(80);

volatile uint32_t liveCounts[14] = {0};
volatile uint32_t liveMgmtCounts[14] = {0};
uint32_t lastScanCounts[14] = {0};
uint32_t lastMgmtCounts[14] = {0};

uint8_t currentChannel = 1;
unsigned long lastHopTime = 0;
unsigned long lastBeaconTime = 0;
uint16_t hopIntervalMs = 240;
bool isScanning = true;
uint8_t bestChannel = 1;

bool alertActive = false;
String alertReason = "Normal";
String baseSsid = "HomeNet";
bool beaconFloodEnabled = false;
uint8_t beaconIndex = 0;
const uint8_t BEACON_COUNT = 12;

String portalTitle = "Network Status & Notice";
String portalNotice = "Local RF & Channel Telemetry Active. Authorized access only.";

// Simple session token for admin actions
String activeToken = "";

String generateToken() {
  String t = "";
  const char chars[] = "0123456789abcdef";
  for (int i = 0; i < 24; i++) {
    t += chars[random(0, 16)];
  }
  return t;
}

bool isAuthorized() {
  if (activeToken.length() == 0) return false;
  if (server.hasArg("token") && server.arg("token") == activeToken) return true;
  if (server.hasHeader("X-Auth-Token") && server.header("X-Auth-Token") == activeToken) return true;
  return false;
}

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

void sendSingleBeacon(uint8_t idx, uint8_t ch) {
  String ssidStr = baseSsid + "_" + (idx + 1 < 10 ? "0" : "") + String(idx + 1);
  if (ssidStr.length() > 32) ssidStr = ssidStr.substring(0, 32);
  uint8_t ssidLen = ssidStr.length();

  uint8_t packet[128] = {
    0x80, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x02, 0x11, 0x22, 0x33, 0x44, 0x00,
    0x02, 0x11, 0x22, 0x33, 0x44, 0x00,
    0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x01, 0x04,
    0x00, ssidLen
  };

  packet[15] = 0x10 + idx;
  packet[21] = 0x10 + idx;

  int pos = 38;
  for (uint8_t i = 0; i < ssidLen; i++) {
    packet[pos++] = (uint8_t)ssidStr[i];
  }

  packet[pos++] = 0x01; packet[pos++] = 0x04;
  packet[pos++] = 0x82; packet[pos++] = 0x84; packet[pos++] = 0x8B; packet[pos++] = 0x96;

  packet[pos++] = 0x03; packet[pos++] = 0x01; packet[pos++] = ch;

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
    alertReason = "Deauth/Disassoc burst (" + String(totalDeauth) + " pkts)";
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
    else if (c == '\n') { out += "\\n"; continue; }
    out += c;
  }
  return out;
}

const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>RF Command Portal</title>
<style>
  :root {
    --bg: #070b14;
    --panel: rgba(17, 24, 39, 0.88);
    --panel-2: rgba(30, 41, 59, 0.68);
    --border: rgba(148, 163, 184, 0.18);
    --text: #f8fafc;
    --muted: #94a3b8;
    --accent: #38bdf8;
    --accent-2: #818cf8;
    --good: #22c55e;
    --warn: #eab308;
    --danger: #ef4444;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; font-family: ui-sans-serif, system-ui, -apple-system, sans-serif; }
  body {
    min-height: 100vh;
    background:
      radial-gradient(circle at 15% 15%, rgba(56, 189, 248, 0.14), transparent 40%),
      radial-gradient(circle at 85% 10%, rgba(129, 140, 248, 0.14), transparent 40%),
      var(--bg);
    color: var(--text);
    padding: 18px 14px 36px;
  }
  .container { max-width: 900px; margin: 0 auto; }
  .topbar {
    display: flex; justify-content: space-between; align-items: center;
    flex-wrap: wrap; gap: 12px; margin-bottom: 16px;
  }
  .brand { display: flex; align-items: center; gap: 10px; }
  .dot {
    width: 10px; height: 10px; border-radius: 50%;
    background: var(--good); box-shadow: 0 0 12px var(--good);
  }
  .brand h1 { font-size: 1.15rem; font-weight: 700; }
  .pill {
    background: rgba(34, 197, 94, 0.12); border: 1px solid rgba(34, 197, 94, 0.35);
    color: #86efac; padding: 6px 12px; border-radius: 999px;
    font-size: 0.82rem; font-weight: 600;
  }
  .alert {
    display: none; background: rgba(239, 68, 68, 0.14);
    border: 1px solid rgba(239, 68, 68, 0.45); color: #fca5a5;
    padding: 12px 14px; border-radius: 14px; margin-bottom: 16px;
    font-size: 0.9rem; font-weight: 600;
  }
  .alert.show { display: block; }
  .notice-hero {
    background: linear-gradient(135deg, rgba(56,189,248,0.12), rgba(129,140,248,0.12));
    border: 1px solid var(--border); border-radius: 18px;
    padding: 16px 18px; margin-bottom: 16px;
  }
  .notice-label { font-size: 0.72rem; text-transform: uppercase; letter-spacing: 0.08em; color: var(--accent); font-weight: 700; margin-bottom: 4px; }
  .notice-title { font-size: 1.05rem; font-weight: 700; margin-bottom: 6px; }
  .notice-body { font-size: 0.9rem; color: #cbd5e1; line-height: 1.5; white-space: pre-line; }
  .card {
    background: var(--panel); border: 1px solid var(--border);
    border-radius: 18px; padding: 16px; margin-bottom: 16px;
  }
  .card-title { font-size: 0.92rem; font-weight: 700; margin-bottom: 12px; color: #e2e8f0; }
  .grid-2 { display: grid; grid-template-columns: 1fr; gap: 14px; margin-bottom: 16px; }
  @media (min-width: 720px) { .grid-2 { grid-template-columns: 1fr 1fr; } }
  button {
    background: var(--panel-2); color: var(--text); border: 1px solid var(--border);
    border-radius: 10px; padding: 8px 13px; font-size: 0.85rem; font-weight: 600; cursor: pointer;
  }
  button.primary { background: linear-gradient(135deg, var(--accent), var(--accent-2)); border: none; color: #06101e; font-weight: 700; }
  button.danger-btn { background: rgba(239,68,68,0.18); border-color: rgba(239,68,68,0.45); color: #fca5a5; }
  input[type="text"], input[type="password"], textarea {
    width: 100%; background: rgba(7, 11, 20, 0.75); color: var(--text);
    border: 1px solid var(--border); border-radius: 10px;
    padding: 9px 11px; font-size: 0.86rem; margin-bottom: 8px;
  }
  textarea { min-height: 64px; resize: vertical; }
  .inline-group { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
  .inline-group input { width: auto; flex: 1; min-width: 130px; margin-bottom: 0; }
  .locked-box {
    border: 1px dashed rgba(148, 163, 184, 0.28);
    border-radius: 14px; padding: 14px; color: var(--muted); font-size: 0.85rem;
  }
  .chart {
    display: grid; grid-template-columns: repeat(13, 1fr); gap: 5px;
    align-items: end; height: 215px; padding-top: 24px;
    border-bottom: 1px solid var(--border);
  }
  .col { display: flex; flex-direction: column; align-items: center; height: 100%; justify-content: flex-end; }
  .val { font-size: 0.68rem; color: var(--muted); margin-bottom: 4px; }
  .bar {
    width: 100%; max-width: 34px; min-height: 4px;
    border-radius: 7px 7px 0 0; background: var(--accent);
    transition: height 0.25s ease, background-color 0.25s ease;
  }
  .lbl { margin-top: 7px; font-size: 0.74rem; font-weight: 700; color: var(--muted); }
  .lbl.best { color: #4ade80; }
  .footer-meta {
    display: flex; justify-content: space-between; flex-wrap: wrap; gap: 8px;
    margin-top: 12px; font-size: 0.8rem; color: var(--muted);
  }
  .auth-msg { font-size: 0.8rem; color: #fca5a5; margin-top: 8px; min-height: 18px; }
  code { color: #7dd3fc; }
</style>
</head>
<body>
<div class="container">
  <div class="topbar">
    <div class="brand">
      <div class="dot"></div>
      <h1>ESP8266 RF & Captive Hub</h1>
    </div>
    <div class="pill" id="bestPill">Least Crowded: CH --</div>
  </div>

  <div class="alert" id="alertBox">ALERT: <span id="alertMsg"></span></div>

  <div class="notice-hero">
    <div class="notice-label">Captive Portal Board</div>
    <div class="notice-title" id="viewTitle">Network Status & Notice</div>
    <div class="notice-body" id="viewNotice">Loading...</div>
  </div>

  <div class="card">
    <div class="card-title" id="authCardTitle">Admin Access (Locked)</div>
    <div id="loginPanel">
      <div class="inline-group">
        <input type="password" id="pinInput" maxlength="20" placeholder="Enter Admin PIN">
        <button class="primary" onclick="loginAdmin()">Unlock Admin</button>
      </div>
      <div class="auth-msg" id="authMsg"></div>
    </div>
    <div id="unlockedPanel" style="display:none;">
      <div class="inline-group">
        <span style="font-size:0.85rem;color:#86efac;font-weight:600;">Admin Session Active</span>
        <button class="danger-btn" onclick="logoutAdmin()">Lock Panel</button>
      </div>
    </div>
  </div>

  <div id="adminControls" style="display:none;">
    <div class="grid-2">
      <div class="card">
        <div class="card-title">Scanner & 12-Beacon Control</div>
        <div class="inline-group" style="margin-bottom:12px;">
          <button class="primary" id="scanBtn" onclick="toggleScan()">Pause Scan</button>
          <button onclick="resetScale()">Reset Scale</button>
          <button class="danger-btn" id="beaconBtn" onclick="toggleBeacons()">12 Beacons: OFF</button>
        </div>
        <div class="inline-group" style="margin-bottom:12px;">
          <span style="font-size:0.82rem;color:var(--muted);">Hop Speed:</span>
          <input type="range" id="hopSlider" min="140" max="600" step="40" value="240" onchange="setHop(this.value)" style="flex:1;">
          <span id="hopText" style="font-size:0.82rem;color:var(--muted);">240ms</span>
        </div>
        <div class="inline-group">
          <input type="text" id="baseInput" maxlength="20" placeholder="Base SSID (e.g. HomeNet)">
          <button onclick="saveBase()">Set Base</button>
        </div>
        <div style="font-size:0.76rem;color:var(--muted);margin-top:8px;">
          Broadcasts <code>Base_01</code>..<code>Base_12</code> when active.
        </div>
      </div>

      <div class="card">
        <div class="card-title">Edit Portal Notice</div>
        <input type="text" id="titleInput" maxlength="60" placeholder="Notice Title">
        <textarea id="noticeInput" maxlength="220" placeholder="Write a notice or status message..."></textarea>
        <button class="primary" onclick="saveNotice()">Publish to Portal</button>
      </div>
    </div>
  </div>

  <div class="card" id="guestNoticeCard">
    <div class="card-title">Control Panel Status</div>
    <div class="locked-box">
      Scanner, beacon broadcast, and notice editor are protected by Admin PIN. Public viewers can only read the status board and channel monitor below.
    </div>
  </div>

  <div class="card">
    <div class="card-title">2.4GHz Channel Activity (CH 1–13)</div>
    <div class="chart" id="chart"></div>
    <div class="footer-meta">
      <span id="telemetryText">Status: Connecting...</span>
      <span id="scaleText">Scale: 50 pkts</span>
    </div>
  </div>
</div>

<script>
let peakScale = 50;
let scanning = true;
let beaconOn = false;
let authToken = localStorage.getItem('esp_admin_token') || '';
let isAdmin = false;

const chart = document.getElementById('chart');
for (let ch = 1; ch <= 13; ch++) {
  const el = document.createElement('div');
  el.className = 'col';
  el.innerHTML = `
    <div class="val" id="v-${ch}">0</div>
    <div class="bar" id="b-${ch}" style="height:4px;"></div>
    <div class="lbl" id="l-${ch}">C${ch}</div>
  `;
  chart.appendChild(el);
}

function setAdminUI(unlocked) {
  isAdmin = unlocked;
  document.getElementById('authCardTitle').textContent = unlocked ? 'Admin Access (Unlocked)' : 'Admin Access (Locked)';
  document.getElementById('loginPanel').style.display = unlocked ? 'none' : 'block';
  document.getElementById('unlockedPanel').style.display = unlocked ? 'block' : 'none';
  document.getElementById('adminControls').style.display = unlocked ? 'block' : 'none';
  document.getElementById('guestNoticeCard').style.display = unlocked ? 'none' : 'block';
}

function getColor(ratio) {
  if (ratio > 0.72) return 'var(--danger)';
  if (ratio > 0.36) return 'var(--warn)';
  return 'var(--good)';
}

async function loginAdmin() {
  const pin = document.getElementById('pinInput').value;
  const msgEl = document.getElementById('authMsg');
  msgEl.textContent = '';
  try {
    const r = await fetch(`/auth?pin=${encodeURIComponent(pin)}`);
    const d = await r.json();
    if (d.ok && d.token) {
      authToken = d.token;
      localStorage.setItem('esp_admin_token', authToken);
      document.getElementById('pinInput').value = '';
      setAdminUI(true);
      refresh();
    } else {
      msgEl.textContent = 'Invalid PIN. Try again.';
    }
  } catch (e) {
    msgEl.textContent = 'Connection error.';
  }
}

async function logoutAdmin() {
  if (authToken) {
    await fetch(`/logout?token=${encodeURIComponent(authToken)}`);
  }
  authToken = '';
  localStorage.removeItem('esp_admin_token');
  setAdminUI(false);
}

async function authFetch(url) {
  const sep = url.includes('?') ? '&' : '?';
  const r = await fetch(`${url}${sep}token=${encodeURIComponent(authToken)}`);
  if (r.status === 401) {
    authToken = '';
    localStorage.removeItem('esp_admin_token');
    setAdminUI(false);
    document.getElementById('authMsg').textContent = 'Session locked or expired.';
    return null;
  }
  return r;
}

async function refresh() {
  try {
    const q = authToken ? `?token=${encodeURIComponent(authToken)}` : '';
    const r = await fetch(`/data${q}`);
    const d = await r.json();

    setAdminUI(!!d.authed);
    scanning = d.scanning;
    beaconOn = d.beaconOn;

    document.getElementById('scanBtn').textContent = scanning ? 'Pause Scan' : 'Resume Scan';
    document.getElementById('beaconBtn').textContent = `12 Beacons: ${beaconOn ? 'ON' : 'OFF'}`;
    document.getElementById('bestPill').textContent = `Least Crowded: CH ${d.bestCh}`;
    document.getElementById('hopSlider').value = d.hopMs;
    document.getElementById('hopText').textContent = `${d.hopMs}ms`;

    document.getElementById('viewTitle').textContent = d.portalTitle;
    document.getElementById('viewNotice').textContent = d.portalNotice;

    if (document.activeElement !== document.getElementById('baseInput')) {
      document.getElementById('baseInput').value = d.baseSsid;
    }
    if (document.activeElement !== document.getElementById('titleInput')) {
      document.getElementById('titleInput').value = d.portalTitle;
    }
    if (document.activeElement !== document.getElementById('noticeInput')) {
      document.getElementById('noticeInput').value = d.portalNotice;
    }

    const alertBox = document.getElementById('alertBox');
    if (d.alert) {
      alertBox.classList.add('show');
      document.getElementById('alertMsg').textContent = d.alertReason;
    } else {
      alertBox.classList.remove('show');
    }

    const maxVal = Math.max(...d.counts);
    if (maxVal > peakScale) peakScale = Math.ceil(maxVal * 1.2);
    document.getElementById('scaleText').textContent = `Scale: ${peakScale} pkts`;
    document.getElementById('telemetryText').textContent =
      `${scanning ? 'Scanning' : 'Paused'} | CH ${d.currentCh} | Deauth/Mgmt: ${d.deauthTotal}`;

    for (let ch = 1; ch <= 13; ch++) {
      const cnt = d.counts[ch - 1];
      const ratio = peakScale > 0 ? Math.min(cnt / peakScale, 1) : 0;
      const bar = document.getElementById(`b-${ch}`);
      const val = document.getElementById(`v-${ch}`);
      const lbl = document.getElementById(`l-${ch}`);

      bar.style.height = `${Math.max(ratio * 100, 2)}%`;
      bar.style.background = getColor(ratio);
      val.textContent = cnt;

      if (ch === d.bestCh) {
        lbl.className = 'lbl best';
        lbl.textContent = `★${ch}`;
      } else {
        lbl.className = 'lbl';
        lbl.textContent = `C${ch}`;
      }
    }
  } catch (e) {
    document.getElementById('telemetryText').textContent = 'Status: Reconnecting to 192.168.4.1...';
  }
}

async function toggleScan() {
  if (!isAdmin) return;
  const res = await authFetch(`/control?scan=${scanning ? 0 : 1}`);
  if (res) refresh();
}
async function toggleBeacons() {
  if (!isAdmin) return;
  const res = await authFetch(`/control?beacon=${beaconOn ? 0 : 1}`);
  if (res) refresh();
}
async function setHop(v) {
  if (!isAdmin) return;
  document.getElementById('hopText').textContent = `${v}ms`;
  await authFetch(`/control?hop=${v}`);
}
async function saveBase() {
  if (!isAdmin) return;
  const b = encodeURIComponent(document.getElementById('baseInput').value.trim() || 'HomeNet');
  const res = await authFetch(`/control?base=${b}`);
  if (res) refresh();
}
async function saveNotice() {
  if (!isAdmin) return;
  const t = encodeURIComponent(document.getElementById('titleInput').value.trim() || 'Notice');
  const n = encodeURIComponent(document.getElementById('noticeInput').value.trim() || '');
  const res = await authFetch(`/notice?title=${t}&body=${n}`);
  if (res) refresh();
}
function resetScale() {
  peakScale = 50;
  refresh();
}

setInterval(refresh, 950);
refresh();
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", PORTAL_HTML);
}

void handleAuth() {
  if (server.hasArg("pin") && server.arg("pin") == String(ADMIN_PIN)) {
    activeToken = generateToken();
    server.send(200, "application/json", "{\"ok\":true,\"token\":\"" + activeToken + "\"}");
  } else {
    server.send(401, "application/json", "{\"ok\":false}");
  }
}

void handleLogout() {
  if (isAuthorized()) {
    activeToken = "";
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleData() {
  uint32_t deauthSum = 0;
  for (uint8_t ch = 1; ch <= 13; ch++) deauthSum += lastMgmtCounts[ch];

  String json = "{";
  json += "\"authed\":" + String(isAuthorized() ? "true" : "false") + ",";
  json += "\"scanning\":" + String(isScanning ? "true" : "false") + ",";
  json += "\"beaconOn\":" + String(beaconFloodEnabled ? "true" : "false") + ",";
  json += "\"currentCh\":" + String(currentChannel) + ",";
  json += "\"bestCh\":" + String(bestChannel) + ",";
  json += "\"hopMs\":" + String(hopIntervalMs) + ",";
  json += "\"deauthTotal\":" + String(deauthSum) + ",";
  json += "\"alert\":" + String(alertActive ? "true" : "false") + ",";
  json += "\"alertReason\":\"" + escapeJson(alertReason) + "\",";
  json += "\"baseSsid\":\"" + escapeJson(baseSsid) + "\",";
  json += "\"portalTitle\":\"" + escapeJson(portalTitle) + "\",";
  json += "\"portalNotice\":\"" + escapeJson(portalNotice) + "\",";
  json += "\"counts\":[";
  for (uint8_t ch = 1; ch <= 13; ch++) {
    json += String(lastScanCounts[ch]);
    if (ch < 13) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleControl() {
  if (!isAuthorized()) {
    server.send(401, "application/json", "{\"ok\":false,\"error\":\"Unauthorized\"}");
    return;
  }
  if (server.hasArg("scan")) {
    isScanning = (server.arg("scan").toInt() == 1);
    wifi_promiscuous_enable(isScanning ? 1 : 0);
  }
  if (server.hasArg("hop")) {
    int ms = server.arg("hop").toInt();
    if (ms >= 140 && ms <= 600) hopIntervalMs = (uint16_t)ms;
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

void handleNotice() {
  if (!isAuthorized()) {
    server.send(401, "application/json", "{\"ok\":false,\"error\":\"Unauthorized\"}");
    return;
  }
  if (server.hasArg("title")) {
    String t = server.arg("title");
    t.trim();
    if (t.length() > 0 && t.length() <= 60) portalTitle = t;
  }
  if (server.hasArg("body")) {
    String n = server.arg("body");
    if (n.length() <= 220) portalNotice = n;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleCaptiveRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  randomSeed(ESP.getCycleCount());

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);

  dnsServer.start(DNS_PORT, "*", apIP);

  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(snifferCallback);
  wifi_promiscuous_enable(1);
  wifi_set_channel(currentChannel);

  server.on("/", handleRoot);
  server.on("/auth", handleAuth);
  server.on("/logout", handleLogout);
  server.on("/data", handleData);
  server.on("/control", handleControl);
  server.on("/notice", handleNotice);

  server.on("/generate_204", handleCaptiveRedirect);
  server.on("/hotspot-detect.html", handleCaptiveRedirect);
  server.on("/ncsi.txt", handleCaptiveRedirect);
  server.on("/fwlink", handleCaptiveRedirect);
  server.onNotFound(handleCaptiveRedirect);

  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (beaconFloodEnabled && (millis() - lastBeaconTime >= 80)) {
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
