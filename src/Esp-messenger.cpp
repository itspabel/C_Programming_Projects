// ESP Messenger - offline text messenger on an ESP8266 access point
//   http://192.168.4.1        messenger (private chats + one group)
//   http://192.168.4.1/admin  admin console (default login admin / admin)
// Messages wait in NodeMCU RAM only until delivered, then are deleted.
// Browsers keep the history in localStorage. Factory reset: hold FLASH (GPIO0) 5 s.
// Credit: Tasfiqul Alam Pabel
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
extern "C" {
#include "user_interface.h"
}
// Present in the SDK library but missing from the Arduino header
extern "C" bool wifi_softap_deauth(uint8_t mac[6]);

#define CFG_MAGIC 0xA51C0DE4UL
#define RESET_PIN 0
#define MAX_BLOCK 8
#define MAX_STA 8
#define SESS_N 3
#define SESS_TIMEOUT 1800000UL   // admin session: 30 min idle

#define MAX_ID 16                // known messenger devices
#define MAX_MSG 20               // messages waiting for delivery
#define MAX_RC 64                // status receipts waiting for senders
#define MAX_TXT 420              // bytes per message (about 140 characters)
#define MAX_PENDING 6            // undelivered messages per sender
#define ONLINE_MS 8000UL
#define EVICT_MS 300000UL
#define RC_TTL_MS 600000UL

struct Cfg {
  uint32_t magic;
  char ssid[33];
  char pass[64];
  uint8_t channel;   // 1..13
  uint8_t hidden;    // 0/1
  uint8_t maxConn;   // 1..8
  uint8_t phy;       // 0=b 1=g 2=n
  uint8_t txp;       // 0..20 (20 = 20.5 dBm)
  char aUser[17];
  char aPass[33];
  uint8_t nBlock;
  uint8_t block[MAX_BLOCK][6];
  uint8_t grpOn;     // group chat enabled
  uint8_t captive;   // 1 = Wi-Fi sign-in popup, 0 = quiet
  uint16_t ttlMin;   // minutes an undelivered message is kept
};

struct Sta { uint8_t mac[6]; uint32_t ip; uint32_t since; bool used; };
struct Sess { char tok[17]; uint32_t last; bool active; };
struct Ident { bool used; char id[9]; char key[17]; char name[40]; uint32_t ip; uint32_t seen; uint32_t lastCid; uint32_t lastMid; uint8_t lastN; };
struct Msg { bool used; bool grp; uint8_t from; uint16_t need; uint16_t got; uint32_t mid; uint32_t ts; uint32_t cts; char text[MAX_TXT + 1]; };
struct Rc { bool used; uint8_t to; uint8_t by; char k; uint32_t rid; uint32_t mid; uint32_t ts; };

Cfg cfg;
Sta stas[MAX_STA];
Sess sess[SESS_N];
Ident idn[MAX_ID];
Msg msgs[MAX_MSG];
Rc rcs[MAX_RC];
uint32_t midCtr = 1, ridCtr = 1, bootId = 1;
ESP8266WebServer server(80);
DNSServer dns;
IPAddress apIP(192, 168, 4, 1);
bool apRestart = false;
uint32_t apRestartAt = 0;
uint32_t pressStart = 0;
uint32_t lastScan = 0;
uint8_t fails = 0;
uint32_t lockUntil = 0;

const char ADMIN_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP Messenger Admin</title>
<style>
:root{--bg:#0b1020;--card:#141b30;--line:#242d4a;--tx:#e8ecf8;--mu:#8b95b8;--ac:#4f8cff;--ok:#2ecc8f;--wa:#f5b942;--er:#ff5d6c}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--tx);font:15px system-ui,sans-serif}
.w{max-width:520px;margin:0 auto;padding:16px}
header{display:flex;align-items:center;gap:12px;margin:8px 0 16px}
.logo{width:40px;height:40px;border-radius:12px;background:var(--ac);display:grid;place-items:center;color:#fff;flex:none}
h1{font-size:18px;margin:0}small,.mu{color:var(--mu)}
.pill{margin-left:auto;display:flex;gap:6px;align-items:center}
.chip{display:flex;align-items:center;gap:5px;background:var(--card);border:1px solid var(--line);border-radius:99px;padding:5px 10px;font-size:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:16px;margin-bottom:14px}
.card h2{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--mu);margin:0 0 4px}
label{display:flex;justify-content:space-between;font-size:13px;color:var(--mu);margin:14px 0 6px}
label b{color:var(--tx)}
.in{display:flex;align-items:center;background:#0e1427;border:1px solid var(--line);border-radius:12px}
.in:focus-within{border-color:var(--ac)}
.in input{flex:1;min-width:0;background:none;border:0;color:var(--tx);padding:12px;font-size:15px;outline:0}
.ib{background:none;border:0;color:var(--mu);padding:0 12px;height:44px;cursor:pointer;display:grid;place-items:center}
.row{display:flex;align-items:center;justify-content:space-between;margin-top:16px;gap:12px}
.sw{width:46px;height:26px;border-radius:99px;background:#2a3454;position:relative;cursor:pointer;transition:.2s;flex:none}
.sw i{position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#fff;transition:.2s}
.sw.on{background:var(--ac)}.sw.on i{left:23px}
.seg{display:flex;background:#0e1427;border:1px solid var(--line);border-radius:12px;padding:3px}
.seg button{flex:1;background:none;border:0;color:var(--mu);padding:9px 0;border-radius:9px;font-size:14px;cursor:pointer}
.seg button.on{background:var(--ac);color:#fff}
.dd{position:relative}
.dd-btn{width:100%;display:flex;justify-content:space-between;align-items:center;background:#0e1427;border:1px solid var(--line);color:var(--tx);border-radius:12px;padding:12px;font-size:15px;cursor:pointer}
.dd-list{display:none;position:absolute;z-index:5;left:0;right:0;top:calc(100% + 6px);max-height:240px;overflow:auto;margin:0;padding:6px;list-style:none;background:#0e1427;border:1px solid var(--line);border-radius:12px}
.dd.open .dd-list{display:block}
.dd-list li{display:flex;justify-content:space-between;padding:10px;border-radius:8px;cursor:pointer}
.dd-list li:hover,.dd-list li.on{background:#1b2547}
.bars{display:none;align-items:flex-end;gap:4px;height:90px;margin-top:12px}
.bars div{flex:1;display:flex;flex-direction:column;justify-content:flex-end;align-items:center;height:100%;cursor:pointer;font-size:10px;color:var(--mu)}
.bars span{width:100%;background:#2a3454;border-radius:4px 4px 0 0;min-height:3px;margin-bottom:3px}
.bars .sel span{background:var(--ac)}.bars .rec span{background:var(--ok)}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:6px;border-radius:9px;background:#2a3454;outline:0}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;border-radius:50%;background:var(--ac);border:3px solid #fff}
input[type=range]::-moz-range-thumb{width:18px;height:18px;border-radius:50%;background:var(--ac);border:3px solid #fff}
.step{display:flex;align-items:center;gap:12px}
.step button{width:42px;height:42px;border-radius:12px;border:1px solid var(--line);background:#0e1427;color:var(--tx);cursor:pointer;display:grid;place-items:center}
.step b{min-width:28px;text-align:center;font-size:18px}
.btn{width:100%;border:0;border-radius:12px;padding:13px;font-size:15px;font-weight:600;color:#fff;background:var(--ac);cursor:pointer;margin-top:14px}
.btn.ghost{background:#0e1427;border:1px solid var(--line);color:var(--tx);font-weight:500}
.btn:disabled{opacity:.5}
.alert{display:flex;gap:10px;align-items:flex-start;padding:12px;border-radius:12px;margin-bottom:10px;border:1px solid;font-size:14px}
.alert svg{flex:none;margin-top:1px}
.alert.info{background:#14264d;border-color:#2b4a8f;color:#b9d0ff}
.alert.ok{background:#0f3328;border-color:#1f7a5a;color:#9af0cc}
.alert.warn{background:#3a2f12;border-color:#8a6d1e;color:#ffdd8a}
.alert.err{background:#3b161c;border-color:#8f2b38;color:#ffb3bb}
.pb{height:8px;background:#0e1427;border-radius:9px;overflow:hidden;margin-top:12px}
.pb i{display:block;height:100%;width:0;background:var(--ok);transition:width .15s}
.big{font-size:30px;font-weight:700}
.ov{display:none;position:fixed;inset:0;background:rgba(5,8,18,.85);place-items:center;z-index:9;padding:24px;text-align:center}
.ov.show{display:grid}
.ov>div{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:24px;max-width:340px;width:100%}
.sp{width:34px;height:34px;border:4px solid #2a3454;border-top-color:var(--ac);border-radius:50%;margin:0 auto 14px;animation:r 1s linear infinite}
@keyframes r{to{transform:rotate(360deg)}}
.dev{display:flex;align-items:center;gap:10px;padding:12px 0;border-top:1px solid var(--line)}
.dev:first-of-type{margin-top:10px}
.di{width:38px;height:38px;border-radius:10px;background:#0e1427;display:grid;place-items:center;color:var(--ac);flex:none}
.dm{flex:1;min-width:0}.dm b{display:block}.dm small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.da{display:flex;gap:6px}
.sb{display:flex;align-items:center;gap:4px;border:1px solid var(--line);background:#0e1427;color:var(--tx);border-radius:9px;padding:7px 9px;font-size:12px;cursor:pointer}
.sb.d{color:var(--er);border-color:#5a2530}
.empty{color:var(--mu);text-align:center;padding:18px 0 6px}
.sub{font-size:12px;color:var(--mu);margin:16px 0 0}
.two{display:flex;gap:10px}.two .btn{margin-top:16px}
.lg{margin-top:12vh}
footer{text-align:center;color:var(--mu);font-size:12px;padding:18px 0 26px}
footer b{color:var(--tx);font-weight:600}
.gr{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin:12px 0 4px}.gr div{background:#0e1427;border:1px solid var(--line);border-radius:12px;padding:10px 4px;text-align:center}.gr b{display:block;font-size:20px}.gr small{font-size:11px}
</style></head><body><div class="w">
<div id="alerts"></div>

<div id="login" style="display:none"><div class="card lg">
<div style="display:flex;gap:12px;align-items:center"><div class="logo" data-ic="lock"></div><div><h1>Admin sign in</h1><small>ESP Link console</small></div></div>
<label>Username</label><div class="in"><span class="ib" data-ic="user"></span><input id="lu" autocomplete="username" autocapitalize="off"></div>
<label>Password</label><div class="in"><span class="ib" data-ic="lock"></span><input id="lp" type="password" autocomplete="current-password"><button class="ib" id="eyeL" type="button"></button></div>
<button class="btn" id="lgb" type="button">Sign in</button></div></div>

<div id="app" style="display:none">
<header><div class="logo" data-ic="wifi"></div><div><h1>ESP Messenger</h1><small>Admin console</small></div>
<div class="pill"><span class="chip"><span data-ic="users"></span><b id="cl">0</b></span><span class="chip"><span data-ic="clock"></span><b id="up">0:00</b></span><button class="sb" id="lo" type="button" title="Sign out"></button></div></header>

<div class="card"><h2>Connected devices</h2>
<div id="dl"></div>
<div id="bw" style="display:none"><p class="sub">Blocked devices</p><div id="bl"></div></div></div>

<div class="card"><h2>Messenger</h2>
<div class="gr"><div><b id="sq">0</b><small>Queued</small></div><div><b id="su">0</b><small>Users</small></div><div><b id="so">0</b><small>Online</small></div><div><b id="sh">0</b><small>Free KB</small></div></div>
<label>Keep undelivered messages <b id="ttv"></b></label><input type="range" id="ttl" min="1" max="240">
<p class="sub">After this time an undelivered message is deleted from the NodeMCU and the sender sees it as not delivered.</p>
<div class="row"><div><b>Group chat</b><br><small>Allow messages to all devices</small></div><div class="sw" id="gsw"><i></i></div></div>
<div class="row"><div><b>Wi-Fi sign-in popup</b><br><small>Turn off to stop the popup on phones</small></div><div class="sw" id="csw"><i></i></div></div>
<button class="btn ghost" id="msv" type="button">Save messenger settings</button>
<div class="two"><button class="btn ghost" id="mcl" type="button">Clear queue</button><button class="btn ghost" id="mrs" type="button">Reset users</button></div>
<p class="sub">The admin can see counts only, never message text.</p></div>

<div class="card"><h2>Network</h2>
<label>Network name (SSID)</label><div class="in"><input id="ssid" maxlength="32" autocomplete="off"></div>
<label>Password</label><div class="in"><input id="pass" type="password" maxlength="63" placeholder="Empty = open network" autocomplete="off"><button class="ib" id="eye" type="button"></button></div>
<div class="row"><div><b>Hidden network</b><br><small>Do not broadcast the SSID</small></div><div class="sw" id="hid"><i></i></div></div></div>

<div class="card"><h2>Performance</h2>
<label>Wi-Fi mode</label><div class="seg" id="phy"><button data-v="0">802.11b</button><button data-v="1">802.11g</button><button data-v="2">802.11n</button></div>
<label>Channel</label>
<div class="dd" id="dd"><button type="button" class="dd-btn" id="ddb"><span id="ddl">Channel 6</span><span data-ic="chev"></span></button><ul class="dd-list" id="ddu"></ul></div>
<div class="bars" id="bars"></div>
<button class="btn ghost" id="scan" type="button">Scan nearby networks</button>
<label>TX power <b id="txv"></b></label><input type="range" id="txp" min="0" max="20">
<label>Max clients <small>fewer = faster</small></label>
<div class="step"><button id="mn" type="button" data-ic="minus"></button><b id="mx">4</b><button id="pl" type="button" data-ic="plus"></button></div></div>
<button class="btn" id="save" type="button">Save and restart Wi-Fi</button>

<div class="card" style="margin-top:14px"><h2>Speed test</h2>
<div class="big"><span id="mbps">0.00</span> <small>Mbps</small></div><small>Phone to ESP8266 download</small>
<div class="pb"><i id="pb"></i></div>
<button class="btn ghost" id="spd" type="button">Run speed test</button></div>

<div class="card"><h2>Admin account</h2>
<label>Username</label><div class="in"><input id="au" maxlength="16" autocomplete="off" autocapitalize="off"></div>
<label>New password</label><div class="in"><input id="ap" type="password" maxlength="32" placeholder="6 to 32 characters" autocomplete="new-password"><button class="ib" id="eyeA" type="button"></button></div>
<label>Confirm password</label><div class="in"><input id="ap2" type="password" maxlength="32" autocomplete="new-password"></div>
<button class="btn ghost" id="asv" type="button">Update admin account</button></div>
</div>
<footer>Tasfiqul Alam Pabel</footer></div>

<div class="ov" id="ov"><div><div class="sp"></div><b>Restarting Wi-Fi</b><p class="mu" id="ovm"></p></div></div>
<div class="ov" id="cf"><div><p id="cfm"></p><div class="two"><button class="btn ghost" id="cfn" type="button">Cancel</button><button class="btn" id="cfy" type="button">Confirm</button></div></div></div>

<script>
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
const P={wifi:"M5 12.55a11 11 0 0 1 14.08 0M1.42 9a16 16 0 0 1 21.16 0M8.53 16.11a6 6 0 0 1 6.95 0M12 20h.01",eye:"M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8zM12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6z",eyeoff:"M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19M1 1l22 22",chev:"M6 9l6 6 6-6",users:"M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2M9 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8zM23 21v-2a4 4 0 0 0-3-3.87M16 3.13a4 4 0 0 1 0 7.75",clock:"M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM12 6v6l4 2",minus:"M5 12h14",plus:"M12 5v14M5 12h14",info:"M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM12 16v-4M12 8h.01",ok:"M20 6L9 17l-5-5",warn:"M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0zM12 9v4M12 17h.01",lock:"M19 11H5a2 2 0 0 0-2 2v7a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7a2 2 0 0 0-2-2zM7 11V7a5 5 0 0 1 10 0v4",user:"M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2M12 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8z",phone:"M7 2h10a2 2 0 0 1 2 2v16a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2zM12 18h.01",ban:"M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM4.93 4.93l14.14 14.14",x:"M18 6L6 18M6 6l12 12",out:"M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4M16 17l5-5-5-5M21 12H9",undo:"M3 7v6h6M3 13a9 9 0 1 0 3-7.7L3 8"};
P.err=P.warn;
const ic=(n,s)=>'<svg viewBox="0 0 24 24" width="'+(s||18)+'" height="'+(s||18)+'" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="'+P[n]+'"/></svg>';
$$('[data-ic]').forEach(e=>e.innerHTML=ic(e.dataset.ic));
$('#lo').innerHTML=ic('out',15);
const S={ch:6,phy:2,mx:4,hid:0,rec:0,counts:null};let timer=null;
function note(t,m,ms){const d=document.createElement('div');d.className='alert '+t;d.innerHTML=ic(t);const s=document.createElement('span');s.textContent=m;d.appendChild(s);$('#alerts').prepend(d);setTimeout(()=>d.remove(),ms||6000)}
function ask(m,cb){$('#cfm').textContent=m;$('#cf').classList.add('show');$('#cfn').onclick=()=>$('#cf').classList.remove('show');$('#cfy').onclick=()=>{$('#cf').classList.remove('show');cb()}}
async function api(u,o){const r=await fetch(u,o);if(r.status===401&&u!=='/api/login'){showLogin();throw new Error('auth')}return r}
const post=(u,o)=>api(u,{method:'POST',body:new URLSearchParams(o||{})});
function eyeBtn(b,i){let sh=0;$(b).innerHTML=ic('eye');$(b).onclick=()=>{sh=!sh;$(i).type=sh?'text':'password';$(b).innerHTML=ic(sh?'eyeoff':'eye')}}
eyeBtn('#eye','#pass');eyeBtn('#eyeL','#lp');eyeBtn('#eyeA','#ap');
function showLogin(){clearInterval(timer);$('#app').style.display='none';$('#login').style.display='block'}
function showApp(){$('#login').style.display='none';$('#app').style.display='block'}
function setPhy(v){S.phy=v;$$('#phy button').forEach(b=>b.classList.toggle('on',+b.dataset.v===v))}
function setCh(n){S.ch=n;$('#ddl').textContent='Channel '+n;$$('#ddu li').forEach(l=>l.classList.toggle('on',+l.dataset.n===n));drawBars()}
function setMx(n){S.mx=Math.max(1,Math.min(8,n));$('#mx').textContent=S.mx}
function setTx(v){$('#txp').value=v;$('#txv').textContent=(v>=20?'20.5':v)+' dBm'}
function setHid(b){S.hid=b?1:0;$('#hid').classList.toggle('on',!!b)}
function drawBars(){const b=$('#bars');if(!S.counts)return;b.style.display='flex';b.innerHTML='';const m=Math.max(1,...S.counts);for(let n=1;n<=13;n++){const d=document.createElement('div');d.className=(n===S.ch?'sel ':'')+(n===S.rec?'rec':'');d.innerHTML='<span style="height:'+(S.counts[n]/m*70+3)+'px"></span>'+n;d.onclick=()=>setCh(n);b.appendChild(d)}}
for(let n=1;n<=13;n++){const l=document.createElement('li');l.dataset.n=n;l.innerHTML='<span>Channel '+n+'</span><small>'+(2407+5*n)+' MHz</small>';l.onclick=()=>{setCh(n);$('#dd').classList.remove('open')};$('#ddu').appendChild(l)}
$('#ddb').onclick=()=>$('#dd').classList.toggle('open');
$$('#phy button').forEach(b=>b.onclick=()=>setPhy(+b.dataset.v));
$('#hid').onclick=()=>setHid(!S.hid);
$('#mn').onclick=()=>setMx(S.mx-1);$('#pl').onclick=()=>setMx(S.mx+1);
$('#txp').oninput=e=>setTx(+e.target.value);

async function doLogin(){const b=$('#lgb');b.disabled=true;
try{const r=await api('/api/login',{method:'POST',body:new URLSearchParams({user:$('#lu').value.trim(),pass:$('#lp').value})});const j=await r.json();
if(!j.ok){note('err',j.msg)}else{$('#lp').value='';init()}}catch(e){note('err','Request failed')}b.disabled=false}
$('#lgb').onclick=doLogin;$('#lp').onkeydown=e=>{if(e.key==='Enter')doLogin()};$('#lu').onkeydown=e=>{if(e.key==='Enter')$('#lp').focus()};
$('#lo').onclick=async()=>{await fetch('/api/logout',{method:'POST'});showLogin()};

async function init(){
try{const r=await api('/api/me');const m=await r.json();showApp();$('#au').value=m.user;
if(m.def)note('warn','Default admin password is still in use. Change it in Admin account below.',60000);
const c=await(await api('/api/config')).json();
$('#ssid').value=c.ssid;$('#pass').value=c.pass;setHid(c.hidden);setPhy(c.phy);setCh(c.channel);setMx(c.maxConn);setTx(c.txp);mset(c);
poll();clearInterval(timer);timer=setInterval(poll,3000)}catch(e){}}

function dur(s){if(s<60)return s+'s';const m=Math.floor(s/60);if(m<60)return m+'m';return Math.floor(m/60)+'h '+(m%60)+'m'}
function mk(txt,ico,cls,fn){const b=document.createElement('button');b.type='button';b.className='sb '+(cls||'');b.innerHTML=ic(ico,14)+'<span>'+txt+'</span>';b.onclick=fn;return b}
async function act(u,mac,msg){try{const r=await post(u,{mac:mac});const j=await r.json();note(j.ok?'ok':'err',j.ok?msg:j.msg);poll()}catch(e){}}
async function poll(){stats();try{const d=await(await api('/api/devices')).json();
$('#cl').textContent=d.devices.length;const m=Math.floor(d.up/60);$('#up').textContent=Math.floor(m/60)+':'+String(m%60).padStart(2,'0');
const L=$('#dl');L.innerHTML='';
if(!d.devices.length)L.innerHTML='<div class="empty">No devices connected</div>';
d.devices.forEach(x=>{const r=document.createElement('div');r.className='dev';r.innerHTML='<span class="di">'+ic('phone')+'</span><div class="dm"><b></b><small></small></div><div class="da"></div>';
r.querySelector('b').textContent=x.ip+(x.name?' \u00b7 '+x.name:'')+(x.me?' (this device)':'');r.querySelector('small').textContent=x.mac+'  \u00b7  '+dur(x.t);
if(!x.me){const a=r.querySelector('.da');a.appendChild(mk('Kick','x','',()=>act('/api/kick',x.mac,'Device disconnected')));a.appendChild(mk('Block','ban','d',()=>ask('Block '+x.mac+'? It will not be able to connect until unblocked.',()=>act('/api/block',x.mac,'Device blocked'))))}
L.appendChild(r)});
$('#bw').style.display=d.blocked.length?'block':'none';const B=$('#bl');B.innerHTML='';
d.blocked.forEach(mac=>{const r=document.createElement('div');r.className='dev';r.innerHTML='<span class="di" style="color:var(--er)">'+ic('ban')+'</span><div class="dm"><b></b></div><div class="da"></div>';r.querySelector('b').textContent=mac;r.querySelector('.da').appendChild(mk('Unblock','undo','',()=>act('/api/unblock',mac,'Device unblocked')));B.appendChild(r)})
}catch(e){}}

$('#scan').onclick=async()=>{const b=$('#scan');b.disabled=true;note('warn','Scanning. Connected devices may drop for a few seconds.');try{const j=await(await api('/api/scan')).json();S.counts=[0].concat(j.counts);S.rec=j.rec;drawBars();note('ok','Recommended channel: '+j.rec)}catch(e){note('err','Scan failed, reconnect and retry')}b.disabled=false};
$('#save').onclick=async()=>{const ssid=$('#ssid').value.trim(),pass=$('#pass').value;
if(!ssid)return note('err','SSID cannot be empty');
if(pass&&pass.length<8)return note('err','Password must be 8 to 63 characters, or empty for open');
try{const r=await post('/api/save',{ssid:ssid,pass:pass,channel:S.ch,hidden:S.hid,maxConn:S.mx,phy:S.phy,txp:$('#txp').value});const j=await r.json();if(!j.ok)return note('err',j.msg);
$('#ovm').textContent='Reconnect to "'+ssid+'", then open 192.168.4.1';$('#ov').classList.add('show');setTimeout(()=>$('#ov').classList.remove('show'),15000)}catch(e){note('err','Request failed')}};
$('#asv').onclick=async()=>{const u=$('#au').value.trim(),p=$('#ap').value;
if(!/^[A-Za-z0-9_.-]{3,16}$/.test(u))return note('err','Username: 3 to 16 letters, numbers, . _ -');
if(p.length<6)return note('err','Password must be 6 to 32 characters');
if(p!==$('#ap2').value)return note('err','Passwords do not match');
try{const j=await(await post('/api/admin',{user:u,pass:p})).json();if(!j.ok)return note('err',j.msg);$('#ap').value='';$('#ap2').value='';note('ok','Admin account updated. Other sessions were signed out.')}catch(e){}};
$('#spd').onclick=async()=>{const b=$('#spd');b.disabled=true;const t0=performance.now(),tot=2048*1024;let got=0;
try{const r=await api('/api/speed?kb=2048&t='+Date.now(),{cache:'no-store'});const rd=r.body.getReader();
for(;;){const x=await rd.read();if(x.done)break;got+=x.value.length;const dt=(performance.now()-t0)/1000;$('#mbps').textContent=(got*8/dt/1e6).toFixed(2);$('#pb').style.width=(got/tot*100)+'%'}}
catch(e){note('err','Speed test failed')}b.disabled=false};

let MS={ttl:30,grp:1,cap:1};
function mset(c){MS.ttl=c.ttl;MS.grp=c.grp;MS.cap=c.cap;$('#ttl').value=c.ttl;$('#ttv').textContent=c.ttl+' min';$('#gsw').classList.toggle('on',!!c.grp);$('#csw').classList.toggle('on',!!c.cap)}
$('#ttl').oninput=e=>{MS.ttl=+e.target.value;$('#ttv').textContent=MS.ttl+' min'};
$('#gsw').onclick=()=>{MS.grp=MS.grp?0:1;$('#gsw').classList.toggle('on',!!MS.grp)};
$('#csw').onclick=()=>{MS.cap=MS.cap?0:1;$('#csw').classList.toggle('on',!!MS.cap)};
$('#msv').onclick=async()=>{try{const j=await(await post('/api/m/cfg',{ttl:MS.ttl,grp:MS.grp,cap:MS.cap})).json();note(j.ok?'ok':'err',j.ok?'Messenger settings saved':j.msg)}catch(e){}};
$('#mcl').onclick=()=>ask('Delete all queued messages? Senders will see them as not delivered.',async()=>{try{const j=await(await post('/api/m/clear')).json();note(j.ok?'ok':'err',j.ok?'Queue cleared':j.msg);stats()}catch(e){}});
$('#mrs').onclick=()=>ask('Forget all messenger users and queued messages? Devices register again automatically.',async()=>{try{const j=await(await post('/api/m/reset')).json();note(j.ok?'ok':'err',j.ok?'Messenger users reset':j.msg);stats()}catch(e){}});
async function stats(){try{const s=await(await api('/api/m/stats')).json();$('#sq').textContent=s.msgs;$('#su').textContent=s.users;$('#so').textContent=s.online;$('#sh').textContent=Math.round(s.heap/1024)}catch(e){}}
init();
</script></body></html>)rawliteral";

const char MSG_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><title>ESP Messenger</title>
<style>
:root{--bg:#0b1020;--card:#141b30;--line:#242d4a;--tx:#e8ecf8;--mu:#8b95b8;--ac:#4f8cff;--ok:#2ecc8f;--wa:#f5b942;--er:#ff5d6c;--me:#2a5bd7}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{height:100%;margin:0}
body{background:var(--bg);color:var(--tx);font:15px system-ui,sans-serif}
.view{display:flex;flex-direction:column;height:100vh;height:100dvh;max-width:560px;margin:0 auto}
.top{display:flex;align-items:center;gap:10px;padding:12px 14px;border-bottom:1px solid var(--line);background:var(--card)}
.logo{width:38px;height:38px;border-radius:12px;background:var(--ac);display:grid;place-items:center;color:#fff;flex:none}
.tt{flex:1;min-width:0}.tt h1{font-size:17px;margin:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.tt small{color:var(--mu);font-size:12px}
.ib2{background:none;border:0;color:var(--tx);width:38px;height:38px;border-radius:10px;display:grid;place-items:center;cursor:pointer;flex:none}
.ib2:active{background:#1b2547}
.scroll{flex:1;overflow-y:auto;padding:6px 0}
.item{display:flex;align-items:center;gap:12px;padding:10px 14px;cursor:pointer}
.item:active{background:#10172b}
.av{width:44px;height:44px;border-radius:50%;display:grid;place-items:center;font-weight:700;color:#fff;flex:none;position:relative}
.av.s{width:36px;height:36px;font-size:14px}
.dot{position:absolute;right:0;bottom:0;width:12px;height:12px;border-radius:50%;background:var(--ok);border:2px solid var(--bg)}
.im{flex:1;min-width:0}.im b{display:block}.im small{display:block;color:var(--mu);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.rt{display:flex;flex-direction:column;align-items:flex-end;gap:4px;font-size:11px;color:var(--mu);min-width:40px}
.bd{background:var(--ac);color:#fff;border-radius:99px;min-width:20px;height:20px;padding:0 6px;display:grid;place-items:center;font-size:11px;font-weight:700}
.empty{color:var(--mu);text-align:center;padding:40px 20px;line-height:1.6}
.msgs{flex:1;overflow-y:auto;padding:10px 12px;display:flex;flex-direction:column;gap:4px}
.ds{align-self:center;background:var(--card);border:1px solid var(--line);color:var(--mu);font-size:11px;border-radius:99px;padding:3px 10px;margin:8px 0}
.b{max-width:80%;padding:8px 11px 5px;border-radius:16px;word-break:break-word;white-space:pre-wrap;line-height:1.35;cursor:default}
.b.me{align-self:flex-end;background:var(--me);border-bottom-right-radius:5px}
.b.th{align-self:flex-start;background:var(--card);border:1px solid var(--line);border-bottom-left-radius:5px}
.b.err{outline:1px solid var(--er)}
.sn{font-size:11px;font-weight:700;margin-bottom:2px}
.mt{display:flex;gap:4px;justify-content:flex-end;align-items:center;font-size:10.5px;opacity:.8;margin-top:2px}
.mt .rd{color:#7ee8ff}
.cb{display:none;background:#3a2f12;color:#ffdd8a;font-size:12px;text-align:center;padding:5px}
.comp{display:flex;gap:8px;padding:10px 12px;border-top:1px solid var(--line);background:var(--card);align-items:center}
.in{display:flex;align-items:center;background:#0e1427;border:1px solid var(--line);border-radius:22px;flex:1}
.in:focus-within{border-color:var(--ac)}
.in input{flex:1;min-width:0;background:none;border:0;color:var(--tx);padding:12px 14px;font-size:15px;outline:0}
.cnt{font-size:11px;color:var(--mu);padding-right:12px}
.sendb{width:46px;height:46px;border-radius:50%;border:0;background:var(--ac);color:#fff;display:grid;place-items:center;cursor:pointer;flex:none}
.sendb:disabled{opacity:.4}
.dd{position:relative}
.dd-list{display:none;position:absolute;right:0;top:44px;min-width:180px;background:#0e1427;border:1px solid var(--line);border-radius:12px;padding:6px;list-style:none;margin:0;z-index:15}
.dd.open .dd-list{display:block}
.dd-list li{padding:11px 10px;border-radius:8px;display:flex;gap:10px;align-items:center;cursor:pointer}
.dd-list li:active{background:#1b2547}
#alerts{position:fixed;left:0;right:0;top:8px;z-index:30;max-width:560px;margin:0 auto;padding:0 12px;pointer-events:none}
.alert{display:flex;gap:10px;align-items:flex-start;padding:12px;border-radius:12px;margin-bottom:8px;border:1px solid;font-size:14px;pointer-events:auto;box-shadow:0 6px 20px rgba(0,0,0,.4)}
.alert svg{flex:none;margin-top:1px}
.alert.info{background:#14264d;border-color:#2b4a8f;color:#b9d0ff}
.alert.ok{background:#0f3328;border-color:#1f7a5a;color:#9af0cc}
.alert.warn{background:#3a2f12;border-color:#8a6d1e;color:#ffdd8a}
.alert.err{background:#3b161c;border-color:#8f2b38;color:#ffb3bb}
.ov{display:none;position:fixed;inset:0;background:rgba(5,8,18,.85);place-items:center;z-index:40;padding:24px}
.ov.show{display:grid}
.ov>div{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:22px;max-width:340px;width:100%}
.ov h2{margin:0 0 6px;font-size:18px}.mu{color:var(--mu)}
.ov .in{border-radius:12px;margin-top:12px}
.ib{background:none;border:0;color:var(--mu);padding:0 0 0 12px;display:grid;place-items:center}
.btn{width:100%;border:0;border-radius:12px;padding:13px;font-size:15px;font-weight:600;color:#fff;background:var(--ac);cursor:pointer;margin-top:14px}
.btn.ghost{background:#0e1427;border:1px solid var(--line);color:var(--tx);font-weight:500}
.two{display:flex;gap:10px}
footer{text-align:center;color:var(--mu);font-size:12px;padding:12px 0 18px}
footer.mini{font-size:10.5px;padding:4px 0 8px;background:var(--card)}
footer b{color:var(--tx);font-weight:600}
</style></head><body>
<div id="alerts"></div>

<section class="view" id="vHome">
<header class="top"><div class="logo" data-ic="chat"></div><div class="tt"><h1>ESP Messenger</h1><small id="myname"></small></div><button class="ib2" id="bn" type="button" data-ic="user" title="Your name"></button></header>
<div class="scroll" id="chats"></div>
<footer>Tasfiqul Alam Pabel</footer>
</section>

<section class="view" id="vChat" style="display:none">
<header class="top"><button class="ib2" id="bk" type="button" data-ic="back"></button><div class="av s" id="cav"></div><div class="tt"><h1 id="ctitle"></h1><small id="csub"></small></div>
<div class="dd" id="mn"><button class="ib2" id="mb" type="button" data-ic="more"></button><ul class="dd-list"><li id="mclr"></li></ul></div></header>
<div class="cb" id="cb">Reconnecting to the NodeMCU</div>
<div class="msgs" id="msgs"></div>
<div class="comp"><div class="in"><input id="txt" maxlength="140" placeholder="Message" autocomplete="off" enterkeyhint="send"><span class="cnt" id="cnt">0/140</span></div><button class="sendb" id="snd" type="button" data-ic="send" disabled></button></div>
<footer class="mini">Tasfiqul Alam Pabel</footer>
</section>

<div class="ov" id="nmod"><div><h2>Your name</h2><div class="mu">Other devices will see this name.</div>
<div class="in"><span class="ib" data-ic="user"></span><input id="nin" maxlength="12" autocomplete="off" placeholder="Name"></div>
<button class="btn" id="nsv" type="button">Save</button></div></div>
<div class="ov" id="cf"><div><div id="cfm"></div><div class="two"><button class="btn ghost" id="cfn" type="button">Cancel</button><button class="btn" id="cfy" type="button">Confirm</button></div></div></div>

<script>
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
const P={chat:"M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z",user:"M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2M12 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8z",users:"M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2M9 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8zM23 21v-2a4 4 0 0 0-3-3.87M16 3.13a4 4 0 0 1 0 7.75",send:"M22 2L11 13M22 2l-7 20-4-9-9-4 20-7z",back:"M15 18l-6-6 6-6",more:"M12 5h.01M12 12h.01M12 19h.01",clock:"M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM12 6v6l4 2",check:"M20 6L9 17l-5-5",check2:"M1 12.5l4.5 4.5L16 6.5M9 17l.5.5L22 6",trash:"M3 6h18M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6",info:"M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM12 16v-4M12 8h.01",ok:"M20 6L9 17l-5-5",warn:"M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0zM12 9v4M12 17h.01"};
P.err=P.warn;P.alert=P.warn;
const ic=(n,s,w)=>'<svg viewBox="0 0 24 24" width="'+(s||18)+'" height="'+(s||18)+'" fill="none" stroke="currentColor" stroke-width="'+(w||2)+'" stroke-linecap="round" stroke-linejoin="round"><path d="'+P[n]+'"/></svg>';
$$('[data-ic]').forEach(e=>e.innerHTML=ic(e.dataset.ic,e.id==='snd'?20:22));
$('#mclr').innerHTML=ic('trash',18)+'<span>Clear chat</span>';
function note(t,m,ms){const d=document.createElement('div');d.className='alert '+t;d.innerHTML=ic(t);const s=document.createElement('span');s.textContent=m;d.appendChild(s);$('#alerts').prepend(d);setTimeout(()=>d.remove(),ms||5000)}
let stOK=true;
function ld(k,d){try{const v=localStorage.getItem(k);return v?JSON.parse(v):d}catch(e){stOK=false;return d}}
function sv(k,v){try{localStorage.setItem(k,JSON.stringify(v))}catch(e){stOK=false}}
function rhex(n){let s='';const a=new Uint8Array(n/2);if(window.crypto&&crypto.getRandomValues)crypto.getRandomValues(a);else for(let i=0;i<a.length;i++)a[i]=Math.random()*256|0;a.forEach(b=>s+=('0'+b.toString(16)).slice(-2));return s}
let me=ld('em_me',null);
if(!me||!/^[0-9a-f]{8}$/.test(me.id)||!/^[0-9a-f]{16}$/.test(me.key)){me={id:rhex(8),key:rhex(16),name:'',cid:Math.floor(Date.now()/1000)};sv('em_me',me)}
let CT=ld('em_ct',{}),UN=ld('em_un',{}),SEEN=ld('em_seen',[]),ON={},GON=1,conn=true,cc=null,ackQ=[],rackQ=[],needR=1,peerSig='',flushing=false;
const hist={};
function H(c){if(!hist[c])hist[c]=ld('em_h_'+c,[]);return hist[c]}
try{Object.keys(localStorage).forEach(k=>{if(k.indexOf('em_h_')===0)H(k.slice(5))})}catch(e){stOK=false}
function saveH(c){const a=hist[c];if(a.length>300)a.splice(0,a.length-300);sv('em_h_'+c,a)}
function nextCid(){me.cid=(me.cid||Math.floor(Date.now()/1000))+1;sv('em_me',me);return me.cid}
const fmtT=t=>new Date(t).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'});
function dlabel(t){const d=new Date(t),n=new Date();const a=new Date(d.getFullYear(),d.getMonth(),d.getDate()).getTime(),b=new Date(n.getFullYear(),n.getMonth(),n.getDate()).getTime();const df=Math.round((b-a)/864e5);if(df===0)return'Today';if(df===1)return'Yesterday';return d.toLocaleDateString([],{day:'numeric',month:'short',year:d.getFullYear()===n.getFullYear()?undefined:'numeric'})}
const bytes=s=>new TextEncoder().encode(s).length;
function col(id){let h=0;for(let i=0;i<id.length;i++)h=(h*31+id.charCodeAt(i))>>>0;return'hsl('+(h%360)+',55%,45%)'}
const ini=n=>(Array.from((n||'?').trim())[0]||'?').toUpperCase();
const cname=id=>CT[id]?CT[id].n:'Device';
const lastOf=c=>{const a=H(c);return a.length?a[a.length-1]:null};
function totalUnread(){let n=0;for(const k in UN)n+=UN[k]||0;document.title=(n?'('+n+') ':'')+'ESP Messenger'}
function avatar(el,id,name){el.innerHTML='';if(id==='g'){el.style.background='var(--ac)';el.innerHTML=ic('users',el.classList.contains('s')?18:22)}else{el.style.background=col(id);el.textContent=ini(name)}}

function renderHome(){
$('#myname').textContent=(me.name||'Tap the user icon to set your name')+(conn?'':'  \u00b7  offline');
const L=$('#chats');L.innerHTML='';const items=[];
if(GON)items.push({c:'g',n:'Everyone',g:1});
Object.keys(CT).forEach(id=>items.push({c:id,n:CT[id].n,on:!!ON[id]}));
items.sort((a,b)=>{if(a.g)return-1;if(b.g)return 1;const la=lastOf(a.c),lb=lastOf(b.c);return((lb?lb.t:0)-(la?la.t:0))||a.n.localeCompare(b.n)});
if(!items.length){L.innerHTML='<div class="empty">No other devices yet.<br>Open this page on another phone.</div>';totalUnread();return}
items.forEach(it=>{const l=lastOf(it.c);const r=document.createElement('div');r.className='item';
r.innerHTML='<div class="av"></div><div class="im"><b></b><small></small></div><div class="rt"><span></span></div>';
const av=r.querySelector('.av');avatar(av,it.c,it.n);
if(it.on){const d=document.createElement('i');d.className='dot';av.appendChild(d)}
r.querySelector('b').textContent=it.n;
r.querySelector('small').textContent=l?((l.o?'You: ':(it.g&&l.fn?l.fn+': ':''))+l.x):(it.g?'Message everyone':(it.on?'online':'offline'));
const rt=r.querySelector('.rt');rt.firstChild.textContent=l?(dlabel(l.t)==='Today'?fmtT(l.t):dlabel(l.t)):'';
if(UN[it.c]){const b=document.createElement('span');b.className='bd';b.textContent=UN[it.c];rt.appendChild(b)}
r.onclick=()=>openChat(it.c);L.appendChild(r)});totalUnread()}

function names(ids){return ids.map(cname).join(', ')}
function stat(m){
if(m.st===9)return{i:'alert',c:'',x:'',k:'err',d:'Not sent: '+(m.err||'failed')};
if(m.st===0)return{i:'clock',c:'',x:'',k:'info',d:'Waiting to send'};
if(m.g){if(m.n===0)return{i:'check',c:'',x:'',k:'info',d:'Sent. No other devices are registered yet.'};
if(m.d.length>=m.n)return{i:'check2',c:'',x:m.n+'/'+m.n,k:'ok',d:'Delivered to everyone: '+names(m.d)};
if(m.ex)return{i:'alert',c:'',x:m.d.length+'/'+m.n,k:'warn',d:'Expired. Delivered to '+(m.d.length?names(m.d):'nobody')};
return{i:m.d.length?'check2':'check',c:'',x:m.d.length+'/'+m.n,k:'info',d:'Sent. Delivered to '+m.d.length+' of '+m.n}}
if(m.r.length)return{i:'check2',c:'rd',x:'',k:'ok',d:'Read'};
if(m.d.length)return{i:'check2',c:'',x:'',k:'ok',d:'Delivered'};
if(m.ex)return{i:'alert',c:'',x:'',k:'err',d:'Not delivered. The message expired on the NodeMCU.'};
return{i:'check',c:'',x:'',k:'info',d:'Sent. Waiting for the other device.'}}

function updSub(){if(!cc)return;let s;if(cc==='g'){s=(Object.keys(ON).length+1)+' online'}else if(ON[cc])s='online';else{const l=CT[cc]&&CT[cc].ls;s=l?'last seen '+(dlabel(l)==='Today'?fmtT(l):dlabel(l)+' '+fmtT(l)):'offline'}$('#csub').textContent=s;$('#cb').style.display=conn?'none':'block'}
function renderChat(force){if(!cc)return;const box=$('#msgs');const near=box.scrollHeight-box.scrollTop-box.clientHeight<80;box.innerHTML='';let last='';
H(cc).forEach(m=>{const dl=dlabel(m.t);if(dl!==last){last=dl;const d=document.createElement('div');d.className='ds';d.textContent=dl;box.appendChild(d)}
const b=document.createElement('div');b.className='b '+(m.o?'me':'th')+(m.st===9?' err':'');
if(!m.o&&cc==='g'){const s=document.createElement('div');s.className='sn';s.style.color=col(m.f);s.textContent=m.fn||cname(m.f);b.appendChild(s)}
const t=document.createElement('div');t.textContent=m.x;b.appendChild(t);
const mt=document.createElement('div');mt.className='mt';const tm=document.createElement('span');tm.textContent=fmtT(m.t);mt.appendChild(tm);
if(m.o){const s=stat(m);const i=document.createElement('span');i.className=s.c;i.innerHTML=ic(s.i,14,2.4);mt.appendChild(i);if(s.x){const x=document.createElement('span');x.textContent=s.x;mt.appendChild(x)}b.style.cursor='pointer';b.onclick=()=>note(s.k,s.d)}
b.appendChild(mt);box.appendChild(b)});
if(force||near)box.scrollTop=box.scrollHeight;updSub()}

function render(f){if(cc)renderChat(f);else renderHome();needR=0}
function openChat(c){cc=c;UN[c]=0;sv('em_un',UN);$('#vHome').style.display='none';$('#vChat').style.display='flex';
$('#ctitle').textContent=c==='g'?'Everyone':cname(c);avatar($('#cav'),c,cname(c));
try{history.pushState({c:1},'')}catch(e){}renderChat(true);sendReads();totalUnread()}
function closeChat(){cc=null;$('#mn').classList.remove('open');$('#vChat').style.display='none';$('#vHome').style.display='flex';renderHome()}
$('#bk').onclick=()=>{if(history.state&&history.state.c)history.back();else closeChat()};
window.addEventListener('popstate',()=>{if(cc)closeChat()});
$('#mb').onclick=e=>{e.stopPropagation();$('#mn').classList.toggle('open')};
document.addEventListener('click',()=>$('#mn').classList.remove('open'));
function ask(m,yes){$('#cfm').textContent=m;$('#cf').classList.add('show');$('#cfn').onclick=()=>$('#cf').classList.remove('show');$('#cfy').onclick=()=>{$('#cf').classList.remove('show');yes()}}
$('#mclr').onclick=()=>{$('#mn').classList.remove('open');ask('Delete this chat from this device?',()=>{hist[cc]=[];saveH(cc);renderChat(true)})};

function updCnt(){const v=$('#txt').value;$('#cnt').textContent=v.length+'/140';$('#snd').disabled=!v.trim()}
$('#txt').oninput=updCnt;
$('#txt').onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();sendMsg()}};
$('#snd').onclick=sendMsg;
function sendMsg(){const text=$('#txt').value.replace(/[\r\n]+/g,' ').trim();if(!text||!cc)return;
if(bytes(text)>420)return note('err','Message is too long');
H(cc).push({o:1,c:nextCid(),m:0,x:text,t:Date.now(),st:0,g:cc==='g'?1:0,n:0,d:[],r:[],ex:0});saveH(cc);
$('#txt').value='';updCnt();renderChat(true);flush()}

async function postSend(chat,m){
try{const r=await fetch('/api/m/send',{method:'POST',body:new URLSearchParams({id:me.id,key:me.key,to:chat,text:m.x,cid:m.c,ct:Math.floor(m.t/1000)})});
const j=await r.json();
if(j.ok){m.m=j.mid;m.n=j.n;m.st=1;saveH(chat);return true}
if(r.status===429||r.status===503)return false;
m.st=9;m.err=j.msg;saveH(chat);return true}catch(e){return false}}
async function flush(){if(flushing)return;flushing=true;
try{for(const c of Object.keys(hist)){for(const m of hist[c]){if(m.o&&m.st===0){const ok=await postSend(c,m);needR=1;if(!ok)return}}}}finally{flushing=false}}

function applyRc(rc){for(const ch in hist){for(const m of hist[ch]){if(m.o&&m.st===1&&m.m===rc.m){
if(rc.k==='d'){if(m.d.indexOf(rc.b)<0)m.d.push(rc.b)}
else if(rc.k==='r'){if(m.r.indexOf(rc.b)<0)m.r.push(rc.b);if(m.d.indexOf(rc.b)<0)m.d.push(rc.b)}
else if(rc.k==='x')m.ex=1;
saveH(ch);return true}}}return false}
function markLost(){for(const ch in hist){let ch2=0;for(const m of hist[ch]){if(m.o&&m.st===1&&!m.ex&&(m.g?m.d.length<m.n:!m.d.length)){m.ex=1;ch2=1}}if(ch2)saveH(ch)}}
function applyPeers(ps){const prev=ON;ON={};let ch=0;ps.forEach(p=>{const o=CT[p.id];if(p.on)ON[p.id]=1;const ls=p.on?0:(o&&o.ls?o.ls:(prev[p.id]?Date.now():0));if(!o||o.n!==p.name||o.ls!==ls){CT[p.id]={n:p.name,ls:ls};ch=1}});if(ch)sv('em_ct',CT)}
function gotMsg(x){const key=x.f+':'+x.m;
if(SEEN.indexOf(key)<0){const chat=x.g?'g':x.f;const now=Date.now();let t=x.c*1000;if(!(t>0)||Math.abs(t-now)>864e5)t=now;
H(chat).push({o:0,f:x.f,fn:cname(x.f),x:x.t,t:t,g:x.g,m:x.m,rd:0});saveH(chat);
SEEN.push(key);if(SEEN.length>300)SEEN.shift();sv('em_seen',SEEN);
if(!(cc===chat&&!document.hidden)){UN[chat]=(UN[chat]||0)+1;sv('em_un',UN)}needR=1}
if(ackQ.indexOf(x.m)<0)ackQ.push(x.m)}
async function sendReads(){if(!cc||cc==='g'||document.hidden)return;const a=H(cc).filter(m=>!m.o&&!m.rd&&m.m).slice(0,10);if(!a.length)return;
try{const r=await fetch('/api/m/read',{method:'POST',body:new URLSearchParams({id:me.id,key:me.key,to:cc,ids:a.map(m=>m.m).join(',')})});const j=await r.json();if(j.ok){a.forEach(m=>m.rd=1);saveH(cc)}}catch(e){}}
document.addEventListener('visibilitychange',()=>{if(!document.hidden&&cc){UN[cc]=0;sv('em_un',UN);totalUnread();sendReads()}});

function setConn(v){if(v!==conn){conn=v;needR=1}}
async function tick(){
try{const body=new URLSearchParams({id:me.id,key:me.key,name:me.name||'Device'});
const a=ackQ.slice(0,12),ra=rackQ.slice(0,24);if(a.length)body.set('ack',a.join(','));if(ra.length)body.set('rack',ra.join(','));
const r=await fetch('/api/m/poll',{method:'POST',body:body});const j=await r.json();
if(!j.ok){setConn(false);if(!tick.warned){tick.warned=1;note('err',j.msg||'Rejected by the NodeMCU')}}
else{tick.warned=0;setConn(true);ackQ=ackQ.filter(x=>a.indexOf(x)<0);rackQ=rackQ.filter(x=>ra.indexOf(x)<0);
if(GON!==j.g){GON=j.g;needR=1}
const ob=ld('em_boot',null);if(ob!==null&&ob!==j.b){markLost();needR=1}sv('em_boot',j.b);
const s=JSON.stringify(j.peers);if(s!==peerSig){peerSig=s;applyPeers(j.peers);needR=1}
j.msgs.forEach(gotMsg);
j.rc.forEach(rc=>{if(applyRc(rc)){needR=1;if(rackQ.indexOf(rc.i)<0)rackQ.push(rc.i)}});
await flush();await sendReads();if(needR)render(false)}}
catch(e){setConn(false);if(needR)render(false)}
setTimeout(tick,document.hidden?4000:1500)}

function nameModal(){$('#nin').value=me.name||'';$('#nmod').classList.add('show')}
$('#bn').onclick=nameModal;
$('#nsv').onclick=()=>{const v=$('#nin').value.replace(/[\r\n]+/g,' ').trim();if(!v)return note('err','Enter a name');me.name=v;sv('em_me',me);$('#nmod').classList.remove('show');needR=1;render(false)};
$('#nin').onkeydown=e=>{if(e.key==='Enter')$('#nsv').click()};
if(!stOK)note('warn','History cannot be saved in this view. Open 192.168.4.1 in Chrome or Safari.',12000);
try{localStorage.setItem('em_t','1')}catch(e){stOK=false}
if(!me.name)nameModal();
renderHome();tick();
</script></body></html>
)rawliteral";

void saveCfg() { EEPROM.put(0, cfg); EEPROM.commit(); }

void defaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CFG_MAGIC;
  strcpy(cfg.ssid, "ESP-Chat");
  strcpy(cfg.pass, "12345678");
  cfg.channel = 6; cfg.hidden = 0; cfg.maxConn = 4; cfg.phy = 2; cfg.txp = 20;
  strcpy(cfg.aUser, "admin");
  strcpy(cfg.aPass, "admin");
  cfg.nBlock = 0;
  cfg.grpOn = 1; cfg.captive = 1; cfg.ttlMin = 30;
}

void loadCfg() {
  EEPROM.begin(512);
  EEPROM.get(0, cfg);
  if (cfg.magic != CFG_MAGIC) { defaults(); saveCfg(); }
  if (cfg.ttlMin < 1 || cfg.ttlMin > 240) cfg.ttlMin = 30;
}

// ---------- helpers ----------
String macStr(const uint8_t* m) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}

bool parseMac(const String& s, uint8_t* out) {
  if (s.length() != 17) return false;
  for (int i = 0; i < 6; i++) {
    char h[3] = { s[i * 3], s[i * 3 + 1], 0 };
    if (!isxdigit(h[0]) || !isxdigit(h[1])) return false;
    if (i < 5 && s[i * 3 + 2] != ':') return false;
    out[i] = (uint8_t)strtol(h, nullptr, 16);
  }
  return true;
}

int blockIdx(const uint8_t* m) {
  for (int i = 0; i < cfg.nBlock; i++) if (!memcmp(cfg.block[i], m, 6)) return i;
  return -1;
}

String jesc(const char* s) {
  String o;
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') { o += '\\'; o += *s; }
    else if ((uint8_t)*s >= 32) o += *s;
  }
  return o;
}

void jsonMsg(int code, bool ok, const char* msg) {
  server.send(code, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + ",\"msg\":\"" + msg + "\"}");
}

// ---------- admin sessions ----------
String newSession() {
  int slot = 0; uint32_t oldest = 0xFFFFFFFFUL;
  for (int i = 0; i < SESS_N; i++) {
    if (!sess[i].active || millis() - sess[i].last > SESS_TIMEOUT) { slot = i; break; }
    if (sess[i].last < oldest) { oldest = sess[i].last; slot = i; }
  }
  snprintf(sess[slot].tok, sizeof(sess[slot].tok), "%08X%08X", (unsigned)RANDOM_REG32, (unsigned)RANDOM_REG32);
  sess[slot].last = millis();
  sess[slot].active = true;
  return String(sess[slot].tok);
}

bool authed() {
  if (!server.hasHeader("Cookie")) return false;
  String c = server.header("Cookie");
  int i = c.indexOf("sid=");
  if (i < 0) return false;
  String t = c.substring(i + 4);
  int e = t.indexOf(';');
  if (e >= 0) t = t.substring(0, e);
  t.trim();
  if (t.length() != 16) return false;
  for (int k = 0; k < SESS_N; k++) {
    if (sess[k].active && t == sess[k].tok) {
      if (millis() - sess[k].last > SESS_TIMEOUT) { sess[k].active = false; return false; }
      sess[k].last = millis();
      return true;
    }
  }
  return false;
}

bool guard() {
  if (authed()) return true;
  jsonMsg(401, false, "Not signed in");
  return false;
}

bool defaultCreds() { return !strcmp(cfg.aUser, "admin") && !strcmp(cfg.aPass, "admin"); }

// ---------- AP ----------
void startAP() {
  WiFi.persistent(false);
  WiFi.softAPdisconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP_STA);            // STA idle, only used for channel scan
  WiFi.disconnect();
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setPhyMode(cfg.phy == 0 ? WIFI_PHY_MODE_11B : (cfg.phy == 1 ? WIFI_PHY_MODE_11G : WIFI_PHY_MODE_11N));
  WiFi.setOutputPower(cfg.txp >= 20 ? 20.5f : (float)cfg.txp);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(cfg.ssid, strlen(cfg.pass) ? cfg.pass : nullptr, cfg.channel, cfg.hidden, cfg.maxConn);
  Serial.printf("\nAP up: %s ch%d phy%d max%d\n", cfg.ssid, cfg.channel, cfg.phy, cfg.maxConn);
}

// Refresh the station table once a second and deauth blocked MACs
void scanStations() {
  bool seen[MAX_STA] = {false};
  uint8_t kill[MAX_STA][6]; int nk = 0;
  struct station_info* si = wifi_softap_get_station_info();
  while (si) {
    if (blockIdx(si->bssid) >= 0) {
      if (nk < MAX_STA) memcpy(kill[nk++], si->bssid, 6);
    } else {
      int k = -1;
      for (int i = 0; i < MAX_STA; i++) if (stas[i].used && !memcmp(stas[i].mac, si->bssid, 6)) { k = i; break; }
      if (k < 0) {
        for (int i = 0; i < MAX_STA; i++) if (!stas[i].used) { k = i; break; }
        if (k >= 0) { memcpy(stas[k].mac, si->bssid, 6); stas[k].used = true; stas[k].since = millis(); }
      }
      if (k >= 0) { stas[k].ip = si->ip.addr; seen[k] = true; }
    }
    si = STAILQ_NEXT(si, next);
  }
  wifi_softap_free_station_info();
  for (int i = 0; i < MAX_STA; i++) if (!seen[i]) stas[i].used = false;
  for (int i = 0; i < nk; i++) wifi_softap_deauth(kill[i]);
}

bool isSelf(const uint8_t* mac) {
  IPAddress me = server.client().remoteIP();
  for (int i = 0; i < MAX_STA; i++)
    if (stas[i].used && !memcmp(stas[i].mac, mac, 6) && IPAddress(stas[i].ip) == me) return true;
  return false;
}

// ---------- messenger core ----------
static inline uint16_t bitOf(int i) { return (uint16_t)(1u << i); }

int popc(uint16_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

bool isHex(const String& s, unsigned n) {
  if (s.length() != n) return false;
  for (unsigned i = 0; i < n; i++) if (!isxdigit((unsigned char)s[i])) return false;
  return true;
}

void utf8Trim(String& s, unsigned maxb) {
  if (s.length() <= maxb) return;
  unsigned cut = maxb;
  while (cut > 0 && (((uint8_t)s[cut]) & 0xC0) == 0x80) cut--;
  s = s.substring(0, cut);
}

String cleanText(const String& in) {
  String o;
  o.reserve(in.length());
  for (unsigned i = 0; i < in.length(); i++) { char c = in[i]; if ((uint8_t)c >= 32) o += c; }
  o.trim();
  return o;
}

int findId(const char* id) {
  for (int i = 0; i < MAX_ID; i++) if (idn[i].used && !strcmp(idn[i].id, id)) return i;
  return -1;
}

void addRc(int to, int by, char k, uint32_t mid) {
  int slot = -1;
  for (int i = 0; i < MAX_RC; i++) if (!rcs[i].used) { slot = i; break; }
  if (slot < 0) {                       // full: overwrite the oldest
    uint32_t best = 0xFFFFFFFFUL;
    for (int i = 0; i < MAX_RC; i++) if (rcs[i].rid < best) { best = rcs[i].rid; slot = i; }
  }
  rcs[slot].used = true; rcs[slot].to = to; rcs[slot].by = by; rcs[slot].k = k;
  rcs[slot].rid = ++ridCtr; rcs[slot].mid = mid; rcs[slot].ts = millis();
}

void dropMsg(int i) { msgs[i].used = false; msgs[i].text[0] = 0; }

bool fullyDelivered(const Msg& m) { return (uint16_t)(m.need & ~m.got) == 0; }

void evictIdent(int x) {
  for (int i = 0; i < MAX_MSG; i++) {
    if (!msgs[i].used) continue;
    if (msgs[i].from == x) { dropMsg(i); continue; }
    msgs[i].need &= ~bitOf(x);
    msgs[i].got &= ~bitOf(x);
    if (msgs[i].need == 0 || fullyDelivered(msgs[i])) dropMsg(i);
  }
  for (int i = 0; i < MAX_RC; i++) if (rcs[i].used && (rcs[i].to == x || rcs[i].by == x)) rcs[i].used = false;
  memset(&idn[x], 0, sizeof(Ident));
}

void expireAll() {
  uint32_t now = millis();
  uint32_t ttl = (uint32_t)cfg.ttlMin * 60000UL;
  for (int i = 0; i < MAX_MSG; i++) {
    if (msgs[i].used && now - msgs[i].ts > ttl) {
      addRc(msgs[i].from, msgs[i].from, 'x', msgs[i].mid);   // tell the sender it expired
      dropMsg(i);
    }
  }
  for (int i = 0; i < MAX_RC; i++) if (rcs[i].used && now - rcs[i].ts > RC_TTL_MS) rcs[i].used = false;
}

void clearQueue() {
  for (int i = 0; i < MAX_MSG; i++) if (msgs[i].used) { addRc(msgs[i].from, msgs[i].from, 'x', msgs[i].mid); dropMsg(i); }
}

// returns ident index, -1 rejected, -2 table full
int authIdent(const String& id, const String& key, const String* name) {
  if (!isHex(id, 8) || !isHex(key, 16)) return -1;
  uint32_t now = millis();
  int ix = findId(id.c_str());
  if (ix >= 0) {
    if (strcmp(idn[ix].key, key.c_str()) != 0) return -1;
  } else {
    for (int i = 0; i < MAX_ID; i++) if (!idn[i].used) { ix = i; break; }
    if (ix < 0) {
      uint32_t oldest = 0; int o = -1;
      for (int i = 0; i < MAX_ID; i++) {
        uint32_t age = now - idn[i].seen;
        if (age > EVICT_MS && age >= oldest) { oldest = age; o = i; }
      }
      if (o < 0) return -2;
      evictIdent(o);
      ix = o;
    }
    memset(&idn[ix], 0, sizeof(Ident));
    idn[ix].used = true;
    id.toCharArray(idn[ix].id, sizeof(idn[ix].id));
    key.toCharArray(idn[ix].key, sizeof(idn[ix].key));
    strcpy(idn[ix].name, "Device");
  }
  idn[ix].seen = now;
  idn[ix].ip = (uint32_t)server.client().remoteIP();
  if (name) {
    String n = cleanText(*name);
    utf8Trim(n, sizeof(idn[ix].name) - 1);
    if (n.length() > 0) n.toCharArray(idn[ix].name, sizeof(idn[ix].name));
  }
  return ix;
}

// ---------- admin handlers ----------
void hAdminPage() { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html", ADMIN_HTML); }

void hLogin() {
  if ((int32_t)(lockUntil - millis()) > 0) { jsonMsg(429, false, "Too many attempts. Wait a minute."); return; }
  if (server.arg("user") == cfg.aUser && server.arg("pass") == cfg.aPass) {
    fails = 0;
    String t = newSession();
    server.sendHeader("Set-Cookie", "sid=" + t + "; Path=/; HttpOnly; SameSite=Strict");
    jsonMsg(200, true, "ok");
  } else {
    if (++fails >= 5) { fails = 0; lockUntil = millis() + 60000UL; }
    jsonMsg(401, false, "Wrong username or password");
  }
}

void hLogout() {
  if (authed()) {
    String c = server.header("Cookie"); int i = c.indexOf("sid=");
    String t = c.substring(i + 4, i + 20);
    for (int k = 0; k < SESS_N; k++) if (t == sess[k].tok) sess[k].active = false;
  }
  server.sendHeader("Set-Cookie", "sid=; Path=/; Max-Age=0");
  jsonMsg(200, true, "ok");
}

void hMe() {
  if (!guard()) return;
  server.send(200, "application/json", String("{\"ok\":true,\"user\":\"") + jesc(cfg.aUser) + "\",\"def\":" + (defaultCreds() ? "true" : "false") + "}");
}

void hConfig() {
  if (!guard()) return;
  String j = "{\"ssid\":\"" + jesc(cfg.ssid) + "\",\"pass\":\"" + jesc(cfg.pass) + "\",\"channel\":" + cfg.channel +
             ",\"hidden\":" + cfg.hidden + ",\"maxConn\":" + cfg.maxConn + ",\"phy\":" + cfg.phy + ",\"txp\":" + cfg.txp +
             ",\"ttl\":" + cfg.ttlMin + ",\"grp\":" + cfg.grpOn + ",\"cap\":" + cfg.captive + "}";
  server.send(200, "application/json", j);
}

void hSave() {
  if (!guard()) return;
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  int ch = server.arg("channel").toInt(), hid = server.arg("hidden").toInt();
  int mc = server.arg("maxConn").toInt(), phy = server.arg("phy").toInt(), tx = server.arg("txp").toInt();
  if (ssid.length() < 1 || ssid.length() > 32) return jsonMsg(400, false, "SSID must be 1 to 32 characters");
  if (pass.length() != 0 && (pass.length() < 8 || pass.length() > 63)) return jsonMsg(400, false, "Password must be 8 to 63 characters");
  if (ch < 1 || ch > 13) return jsonMsg(400, false, "Invalid channel");
  if (mc < 1 || mc > 8) return jsonMsg(400, false, "Max clients must be 1 to 8");
  if (phy < 0 || phy > 2) return jsonMsg(400, false, "Invalid mode");
  if (tx < 0 || tx > 20) return jsonMsg(400, false, "Invalid TX power");
  memset(cfg.ssid, 0, sizeof(cfg.ssid)); memset(cfg.pass, 0, sizeof(cfg.pass));
  ssid.toCharArray(cfg.ssid, sizeof(cfg.ssid));
  pass.toCharArray(cfg.pass, sizeof(cfg.pass));
  cfg.channel = ch; cfg.hidden = hid ? 1 : 0; cfg.maxConn = mc; cfg.phy = phy; cfg.txp = tx;
  saveCfg();
  jsonMsg(200, true, "ok");
  apRestart = true; apRestartAt = millis() + 1500;
}

void hAdmin() {
  if (!guard()) return;
  String u = server.arg("user"), p = server.arg("pass");
  if (u.length() < 3 || u.length() > 16) return jsonMsg(400, false, "Username must be 3 to 16 characters");
  for (unsigned i = 0; i < u.length(); i++) {
    char c = u[i];
    if (!(isalnum(c) || c == '_' || c == '.' || c == '-')) return jsonMsg(400, false, "Username has invalid characters");
  }
  if (p.length() < 6 || p.length() > 32) return jsonMsg(400, false, "Password must be 6 to 32 characters");
  memset(cfg.aUser, 0, sizeof(cfg.aUser)); memset(cfg.aPass, 0, sizeof(cfg.aPass));
  u.toCharArray(cfg.aUser, sizeof(cfg.aUser));
  p.toCharArray(cfg.aPass, sizeof(cfg.aPass));
  saveCfg();
  for (int k = 0; k < SESS_N; k++) sess[k].active = false;
  String t = newSession();
  server.sendHeader("Set-Cookie", "sid=" + t + "; Path=/; HttpOnly; SameSite=Strict");
  jsonMsg(200, true, "ok");
}

void hMCfg() {
  if (!guard()) return;
  int ttl = server.arg("ttl").toInt(), grp = server.arg("grp").toInt(), cap = server.arg("cap").toInt();
  if (ttl < 1 || ttl > 240) return jsonMsg(400, false, "Retention must be 1 to 240 minutes");
  cfg.ttlMin = ttl; cfg.grpOn = grp ? 1 : 0; cfg.captive = cap ? 1 : 0;
  saveCfg();
  jsonMsg(200, true, "ok");
}

void hMStats() {
  if (!guard()) return;
  int nm = 0, nu = 0, no = 0;
  uint32_t now = millis();
  for (int i = 0; i < MAX_MSG; i++) if (msgs[i].used) nm++;
  for (int i = 0; i < MAX_ID; i++) if (idn[i].used) { nu++; if (now - idn[i].seen < ONLINE_MS) no++; }
  server.send(200, "application/json", "{\"msgs\":" + String(nm) + ",\"users\":" + String(nu) + ",\"online\":" + String(no) + ",\"heap\":" + String(ESP.getFreeHeap()) + "}");
}

void hMClear() { if (!guard()) return; clearQueue(); jsonMsg(200, true, "ok"); }

void hMReset() {
  if (!guard()) return;
  memset(msgs, 0, sizeof(msgs));
  memset(rcs, 0, sizeof(rcs));
  memset(idn, 0, sizeof(idn));
  bootId = (uint32_t)RANDOM_REG32;      // makes every browser mark old messages as not delivered
  jsonMsg(200, true, "ok");
}

void hScan() {
  if (!guard()) return;
  int n = WiFi.scanNetworks();
  int cnt[14] = {0};
  for (int i = 0; i < n; i++) { int c = WiFi.channel(i); if (c >= 1 && c <= 13) cnt[c]++; }
  WiFi.scanDelete();
  int best = 1, bestScore = 9999; const int cand[3] = {1, 6, 11};
  for (int k = 0; k < 3; k++) {
    int c = cand[k], s = 0;
    for (int d = -4; d <= 4; d++) { int x = c + d; if (x >= 1 && x <= 13) s += cnt[x]; }
    if (s < bestScore) { bestScore = s; best = c; }
  }
  String j = "{\"rec\":" + String(best) + ",\"counts\":[";
  for (int c = 1; c <= 13; c++) { j += cnt[c]; if (c < 13) j += ","; }
  j += "]}";
  server.send(200, "application/json", j);
}

void hSpeed() {
  if (!guard()) return;
  static uint8_t buf[1024];
  static bool init = false;
  if (!init) { for (int i = 0; i < 1024; i++) buf[i] = (uint8_t)i; init = true; }
  uint32_t kb = server.arg("kb").toInt();
  if (kb < 64) kb = 64;
  if (kb > 4096) kb = 4096;
  server.setContentLength(kb * 1024UL);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/octet-stream", "");
  WiFiClient c = server.client();
  for (uint32_t i = 0; i < kb && c.connected(); i++) { c.write(buf, sizeof(buf)); yield(); }
}

String nameByIp(uint32_t ip) {
  uint32_t now = millis();
  for (int i = 0; i < MAX_ID; i++)
    if (idn[i].used && idn[i].ip == ip && now - idn[i].seen < ONLINE_MS * 3) return jesc(idn[i].name);
  return "";
}

void hDevices() {
  if (!guard()) return;
  IPAddress me = server.client().remoteIP();
  String j = "{\"up\":" + String(millis() / 1000) + ",\"max\":" + String(cfg.maxConn) + ",\"devices\":[";
  bool first = true;
  for (int i = 0; i < MAX_STA; i++) {
    if (!stas[i].used) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"mac\":\"" + macStr(stas[i].mac) + "\",\"ip\":\"" + IPAddress(stas[i].ip).toString() +
         "\",\"name\":\"" + nameByIp(stas[i].ip) +
         "\",\"t\":" + String((millis() - stas[i].since) / 1000) + ",\"me\":" + (IPAddress(stas[i].ip) == me ? "true" : "false") + "}";
  }
  j += "],\"blocked\":[";
  for (int k = 0; k < cfg.nBlock; k++) { if (k) j += ","; j += "\"" + macStr(cfg.block[k]) + "\""; }
  j += "]}";
  server.send(200, "application/json", j);
}

void hKick() {
  if (!guard()) return;
  uint8_t m[6];
  if (!parseMac(server.arg("mac"), m)) return jsonMsg(400, false, "Invalid MAC");
  if (isSelf(m)) return jsonMsg(400, false, "You cannot kick your own device");
  wifi_softap_deauth(m);
  jsonMsg(200, true, "ok");
}

void hBlock() {
  if (!guard()) return;
  uint8_t m[6];
  if (!parseMac(server.arg("mac"), m)) return jsonMsg(400, false, "Invalid MAC");
  if (isSelf(m)) return jsonMsg(400, false, "You cannot block your own device");
  if (blockIdx(m) < 0) {
    if (cfg.nBlock >= MAX_BLOCK) return jsonMsg(400, false, "Block list is full (8)");
    memcpy(cfg.block[cfg.nBlock++], m, 6);
    saveCfg();
  }
  wifi_softap_deauth(m);
  jsonMsg(200, true, "ok");
}

void hUnblock() {
  if (!guard()) return;
  uint8_t m[6];
  if (!parseMac(server.arg("mac"), m)) return jsonMsg(400, false, "Invalid MAC");
  int i = blockIdx(m);
  if (i < 0) return jsonMsg(400, false, "Not in block list");
  for (int k = i; k < cfg.nBlock - 1; k++) memcpy(cfg.block[k], cfg.block[k + 1], 6);
  cfg.nBlock--;
  saveCfg();
  jsonMsg(200, true, "ok");
}

// ---------- messenger handlers (public, protected by per-device key) ----------
void hMsgPage() { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html", MSG_HTML); }

void authFail(int code) {
  if (code == -2) jsonMsg(503, false, "Too many devices on the messenger");
  else jsonMsg(403, false, "This device identity was rejected");
}

// Presence + incoming messages + receipts. The client acknowledges what it stored
// in the next poll (ack = message ids, rack = receipt ids) before anything is deleted.
void hMPoll() {
  String id = server.arg("id"), key = server.arg("key"), name = server.arg("name");
  expireAll();
  int me = authIdent(id, key, &name);
  if (me < 0) return authFail(me);

  String ack = server.arg("ack");
  if (ack.length()) {
    const char* p = ack.c_str();
    while (*p) {
      char* e;
      uint32_t m = strtoul(p, &e, 10);
      if (e == p) { p++; continue; }
      p = e;
      if (*p == ',') p++;
      for (int i = 0; i < MAX_MSG; i++) {
        if (msgs[i].used && msgs[i].mid == m && (msgs[i].need & bitOf(me)) && !(msgs[i].got & bitOf(me))) {
          msgs[i].got |= bitOf(me);
          addRc(msgs[i].from, me, 'd', m);
          if (fullyDelivered(msgs[i])) dropMsg(i);   // everyone has it: delete from the NodeMCU
        }
      }
    }
  }
  String rack = server.arg("rack");
  if (rack.length()) {
    const char* p = rack.c_str();
    while (*p) {
      char* e;
      uint32_t r = strtoul(p, &e, 10);
      if (e == p) { p++; continue; }
      p = e;
      if (*p == ',') p++;
      for (int i = 0; i < MAX_RC; i++) if (rcs[i].used && rcs[i].to == me && rcs[i].rid == r) rcs[i].used = false;
    }
  }

  uint32_t now = millis();
  String j;
  j.reserve(1500);
  j = "{\"ok\":true,\"b\":";
  j += String((unsigned long)bootId);
  j += ",\"g\":"; j += cfg.grpOn ? "1" : "0";
  j += ",\"peers\":[";
  bool first = true;
  for (int i = 0; i < MAX_ID; i++) {
    if (!idn[i].used || i == me) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"id\":\""; j += idn[i].id;
    j += "\",\"name\":\""; j += jesc(idn[i].name);
    j += "\",\"on\":"; j += (now - idn[i].seen < ONLINE_MS) ? "1" : "0";
    j += "}";
  }
  j += "],\"msgs\":[";
  first = true;
  int sent = 0;
  for (int i = 0; i < MAX_MSG && sent < 6; i++) {
    if (!msgs[i].used || !(msgs[i].need & bitOf(me)) || (msgs[i].got & bitOf(me))) continue;
    if (!first) j += ",";
    first = false;
    sent++;
    j += "{\"m\":"; j += String((unsigned long)msgs[i].mid);
    j += ",\"f\":\""; j += idn[msgs[i].from].id;
    j += "\",\"g\":"; j += msgs[i].grp ? "1" : "0";
    j += ",\"c\":"; j += String((unsigned long)msgs[i].cts);
    j += ",\"t\":\""; j += jesc(msgs[i].text);
    j += "\"}";
  }
  j += "],\"rc\":[";
  first = true;
  sent = 0;
  for (int i = 0; i < MAX_RC && sent < 32; i++) {
    if (!rcs[i].used || rcs[i].to != me) continue;
    if (!first) j += ",";
    first = false;
    sent++;
    j += "{\"i\":"; j += String((unsigned long)rcs[i].rid);
    j += ",\"m\":"; j += String((unsigned long)rcs[i].mid);
    j += ",\"b\":\""; j += idn[rcs[i].by].id;
    j += "\",\"k\":\""; j += rcs[i].k;
    j += "\"}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void hMSend() {
  String id = server.arg("id"), key = server.arg("key"), to = server.arg("to"), text = server.arg("text");
  uint32_t cid = strtoul(server.arg("cid").c_str(), nullptr, 10);
  uint32_t cts = strtoul(server.arg("ct").c_str(), nullptr, 10);
  expireAll();
  int me = authIdent(id, key, nullptr);
  if (me < 0) return authFail(me);
  text = cleanText(text);
  if (text.length() == 0) return jsonMsg(400, false, "Empty message");
  if (text.length() > MAX_TXT) return jsonMsg(400, false, "Message is too long");

  if (cid != 0 && idn[me].lastCid == cid) {      // retry of a message we already queued
    server.send(200, "application/json", "{\"ok\":true,\"mid\":" + String((unsigned long)idn[me].lastMid) + ",\"n\":" + String((int)idn[me].lastN) + "}");
    return;
  }
  uint16_t need = 0;
  bool grp = false;
  if (to == "g") {
    if (!cfg.grpOn) return jsonMsg(403, false, "Group chat is turned off");
    grp = true;
    for (int i = 0; i < MAX_ID; i++) if (idn[i].used && i != me) need |= bitOf(i);
  } else {
    if (!isHex(to, 8)) return jsonMsg(400, false, "Bad recipient");
    int t = findId(to.c_str());
    if (t < 0 || t == me) return jsonMsg(404, false, "Device not known to this network");
    need = bitOf(t);
  }
  int n = popc(need);
  uint32_t mid = 0;
  if (n > 0) {
    int pend = 0, slot = -1;
    for (int i = 0; i < MAX_MSG; i++) {
      if (msgs[i].used && msgs[i].from == me) pend++;
      if (!msgs[i].used && slot < 0) slot = i;
    }
    if (pend >= MAX_PENDING) return jsonMsg(429, false, "Too many undelivered messages");
    if (slot < 0) return jsonMsg(503, false, "NodeMCU queue is full, try again");
    msgs[slot].used = true;
    msgs[slot].grp = grp;
    msgs[slot].from = me;
    msgs[slot].need = need;
    msgs[slot].got = 0;
    msgs[slot].mid = mid = ++midCtr;
    msgs[slot].ts = millis();
    msgs[slot].cts = cts;
    text.toCharArray(msgs[slot].text, sizeof(msgs[slot].text));
  }
  idn[me].lastCid = cid;
  idn[me].lastMid = mid;
  idn[me].lastN = (uint8_t)n;
  server.send(200, "application/json", "{\"ok\":true,\"mid\":" + String((unsigned long)mid) + ",\"n\":" + String(n) + "}");
}

void hMRead() {
  String id = server.arg("id"), key = server.arg("key"), to = server.arg("to"), ids = server.arg("ids");
  int me = authIdent(id, key, nullptr);
  if (me < 0) return authFail(me);
  if (!isHex(to, 8)) return jsonMsg(400, false, "Bad sender");
  int s = findId(to.c_str());
  if (s >= 0 && s != me) {
    const char* p = ids.c_str();
    int cnt = 0;
    while (*p && cnt < 10) {
      char* e;
      uint32_t m = strtoul(p, &e, 10);
      if (e == p) { p++; continue; }
      p = e;
      if (*p == ',') p++;
      addRc(s, me, 'r', m);
      cnt++;
    }
  }
  jsonMsg(200, true, "ok");
}

void hNotFound() {
  if (!cfg.captive) {
    // Quiet mode: answer the phone's "is there internet" checks so no sign-in popup appears
    String u = server.uri();
    if (u == "/generate_204" || u == "/gen_204") { server.send(204, "text/plain", ""); return; }
    if (u == "/hotspot-detect.html" || u == "/library/test/success.html") {
      server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
      return;
    }
    if (u == "/connecttest.txt") { server.send(200, "text/plain", "Microsoft Connect Test"); return; }
    if (u == "/ncsi.txt") { server.send(200, "text/plain", "Microsoft NCSI"); return; }
    if (u == "/success.txt") { server.send(200, "text/plain", "success\n"); return; }
  }
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  pinMode(RESET_PIN, INPUT_PULLUP);
  loadCfg();
  startAP();
  midCtr = (uint32_t)RANDOM_REG32 & 0x3FFFFFFFUL;
  ridCtr = (uint32_t)RANDOM_REG32 & 0x3FFFFFFFUL;
  bootId = (uint32_t)RANDOM_REG32;
  dns.start(53, "*", apIP);
  server.collectHeaders("Cookie");
  server.on("/", hMsgPage);
  server.on("/admin", hAdminPage);
  server.on("/api/m/poll", HTTP_POST, hMPoll);
  server.on("/api/m/send", HTTP_POST, hMSend);
  server.on("/api/m/read", HTTP_POST, hMRead);
  server.on("/api/login", HTTP_POST, hLogin);
  server.on("/api/logout", HTTP_POST, hLogout);
  server.on("/api/me", HTTP_GET, hMe);
  server.on("/api/config", HTTP_GET, hConfig);
  server.on("/api/save", HTTP_POST, hSave);
  server.on("/api/admin", HTTP_POST, hAdmin);
  server.on("/api/scan", HTTP_GET, hScan);
  server.on("/api/speed", HTTP_GET, hSpeed);
  server.on("/api/devices", HTTP_GET, hDevices);
  server.on("/api/kick", HTTP_POST, hKick);
  server.on("/api/block", HTTP_POST, hBlock);
  server.on("/api/unblock", HTTP_POST, hUnblock);
  server.on("/api/m/cfg", HTTP_POST, hMCfg);
  server.on("/api/m/stats", HTTP_GET, hMStats);
  server.on("/api/m/clear", HTTP_POST, hMClear);
  server.on("/api/m/reset", HTTP_POST, hMReset);
  server.onNotFound(hNotFound);
  server.begin();
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
  if (millis() - lastScan >= 1000) { lastScan = millis(); scanStations(); }
  if (apRestart && (int32_t)(millis() - apRestartAt) >= 0) { apRestart = false; startAP(); }
  if (digitalRead(RESET_PIN) == LOW) {
    if (!pressStart) pressStart = millis();
    if (millis() - pressStart > 5000) { defaults(); saveCfg(); delay(200); ESP.restart(); }
  } else pressStart = 0;
}
