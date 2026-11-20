// ESP Link - ESP8266 high-throughput Access Point with secured web console
// Portal: http://192.168.4.1   Default AP: ESP-Link / 12345678
// Default admin login: admin / admin  (change it in the portal)
// Factory reset (Wi-Fi + admin + blocklist): hold FLASH (GPIO0) for 5 seconds
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

#define CFG_MAGIC 0xA51C0DE3UL
#define RESET_PIN 0
#define MAX_BLOCK 8
#define MAX_STA 8
#define SESS_N 3
#define SESS_TIMEOUT 1800000UL  // 30 min idle

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
};

struct Sta { uint8_t mac[6]; uint32_t ip; uint32_t since; bool used; };
struct Sess { char tok[17]; uint32_t last; bool active; };

Cfg cfg;
Sta stas[MAX_STA];
Sess sess[SESS_N];
ESP8266WebServer server(80);
DNSServer dns;
IPAddress apIP(192, 168, 4, 1);
bool apRestart = false;
uint32_t apRestartAt = 0;
uint32_t pressStart = 0;
uint32_t lastScan = 0;
uint8_t fails = 0;
uint32_t lockUntil = 0;

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP Link</title>
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
</style></head><body><div class="w">
<div id="alerts"></div>

<div id="login" style="display:none"><div class="card lg">
<div style="display:flex;gap:12px;align-items:center"><div class="logo" data-ic="lock"></div><div><h1>Admin sign in</h1><small>ESP Link console</small></div></div>
<label>Username</label><div class="in"><span class="ib" data-ic="user"></span><input id="lu" autocomplete="username" autocapitalize="off"></div>
<label>Password</label><div class="in"><span class="ib" data-ic="lock"></span><input id="lp" type="password" autocomplete="current-password"><button class="ib" id="eyeL" type="button"></button></div>
<button class="btn" id="lgb" type="button">Sign in</button></div></div>

<div id="app" style="display:none">
<header><div class="logo" data-ic="wifi"></div><div><h1>ESP Link</h1><small>Access point console</small></div>
<div class="pill"><span class="chip"><span data-ic="users"></span><b id="cl">0</b></span><span class="chip"><span data-ic="clock"></span><b id="up">0:00</b></span><button class="sb" id="lo" type="button" title="Sign out"></button></div></header>

<div class="card"><h2>Connected devices</h2>
<div id="dl"></div>
<div id="bw" style="display:none"><p class="sub">Blocked devices</p><div id="bl"></div></div></div>

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
$('#ssid').value=c.ssid;$('#pass').value=c.pass;setHid(c.hidden);setPhy(c.phy);setCh(c.channel);setMx(c.maxConn);setTx(c.txp);
poll();clearInterval(timer);timer=setInterval(poll,3000)}catch(e){}}

function dur(s){if(s<60)return s+'s';const m=Math.floor(s/60);if(m<60)return m+'m';return Math.floor(m/60)+'h '+(m%60)+'m'}
function mk(txt,ico,cls,fn){const b=document.createElement('button');b.type='button';b.className='sb '+(cls||'');b.innerHTML=ic(ico,14)+'<span>'+txt+'</span>';b.onclick=fn;return b}
async function act(u,mac,msg){try{const r=await post(u,{mac:mac});const j=await r.json();note(j.ok?'ok':'err',j.ok?msg:j.msg);poll()}catch(e){}}
async function poll(){try{const d=await(await api('/api/devices')).json();
$('#cl').textContent=d.devices.length;const m=Math.floor(d.up/60);$('#up').textContent=Math.floor(m/60)+':'+String(m%60).padStart(2,'0');
const L=$('#dl');L.innerHTML='';
if(!d.devices.length)L.innerHTML='<div class="empty">No devices connected</div>';
d.devices.forEach(x=>{const r=document.createElement('div');r.className='dev';r.innerHTML='<span class="di">'+ic('phone')+'</span><div class="dm"><b></b><small></small></div><div class="da"></div>';
r.querySelector('b').textContent=x.ip+(x.me?' (this device)':'');r.querySelector('small').textContent=x.mac+'  \u00b7  '+dur(x.t);
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
init();
</script></body></html>)rawliteral";

void saveCfg() { EEPROM.put(0, cfg); EEPROM.commit(); }

void defaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CFG_MAGIC;
  strcpy(cfg.ssid, "ESP-Link");
  strcpy(cfg.pass, "12345678");
  cfg.channel = 6; cfg.hidden = 0; cfg.maxConn = 4; cfg.phy = 2; cfg.txp = 20;
  strcpy(cfg.aUser, "admin");
  strcpy(cfg.aPass, "admin");
  cfg.nBlock = 0;
}

void loadCfg() {
  EEPROM.begin(512);
  EEPROM.get(0, cfg);
  if (cfg.magic != CFG_MAGIC) { defaults(); saveCfg(); }
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

// ---------- sessions ----------
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
  WiFi.setSleepMode(WIFI_NONE_SLEEP);  // no power saving = lowest latency
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

// ---------- handlers ----------
void hIndex() { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html", INDEX_HTML); }

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
             ",\"hidden\":" + cfg.hidden + ",\"maxConn\":" + cfg.maxConn + ",\"phy\":" + cfg.phy + ",\"txp\":" + cfg.txp + "}";
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
  for (int k = 0; k < SESS_N; k++) sess[k].active = false;   // sign everyone out
  String t = newSession();                                   // keep this browser signed in
  server.sendHeader("Set-Cookie", "sid=" + t + "; Path=/; HttpOnly; SameSite=Strict");
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

void hNotFound() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  pinMode(RESET_PIN, INPUT_PULLUP);
  loadCfg();
  startAP();
  dns.start(53, "*", apIP);   // captive portal: phones open the console automatically
  server.collectHeaders("Cookie");
  server.on("/", hIndex);
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
