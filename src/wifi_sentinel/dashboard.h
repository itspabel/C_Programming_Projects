// WiFi Sentinel dashboard - served from flash. Made by Tasfiqul Alam Pabel
#pragma once
#include <pgmspace.h>

const char INDEX_HTML[] PROGMEM = R"=====(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>WiFi Sentinel</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#0c1a1d;--card:#122428;--card2:#17303a;--line:#264850;--txt:#e7f2ef;--mut:#86a6a3;--ok:#5fe0b0;--warn:#ffb347;--bad:#ff5a4f;--acc:#4cc9f0}
body{background:var(--bg);color:var(--txt);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;max-width:560px;margin:0 auto;padding:14px 14px 90px}
.i{width:20px;height:20px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round;flex:none}
.mut{color:var(--mut)}
header{display:flex;align-items:center;gap:12px;margin-bottom:14px}
.logo{width:42px;height:42px;border-radius:12px;background:var(--card2);display:grid;place-items:center;color:var(--acc);border:1px solid var(--line)}
header h1{font-size:19px;font-weight:650;letter-spacing:.2px}
header p{font-size:13px}
.chip{margin-left:auto;display:flex;align-items:center;gap:6px;background:var(--card);border:1px solid var(--line);border-radius:20px;padding:6px 11px;font-variant-numeric:tabular-nums;font-size:13px}
.chip .i{width:15px;height:15px;color:var(--mut)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--bad);display:inline-block;margin-right:6px}
.dot.on{background:var(--ok)}
.seg{display:grid;grid-template-columns:repeat(3,1fr);gap:4px;background:var(--card);border:1px solid var(--line);border-radius:14px;padding:4px;margin-bottom:14px}
.seg button{background:none;border:0;color:var(--mut);padding:10px 4px;border-radius:10px;display:flex;flex-direction:column;align-items:center;gap:3px;font:inherit;font-size:13px;cursor:pointer}
.seg button.on{background:var(--card2);color:var(--txt);box-shadow:inset 0 0 0 1px var(--acc)}
.seg button.on .i{color:var(--acc)}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:16px;margin-bottom:12px}
.card h2{font-size:14px;font-weight:600;color:var(--mut);margin-bottom:10px}
.hero{display:flex;gap:16px;align-items:center}
.hero.bad{border-color:var(--bad);box-shadow:0 0 0 3px rgba(255,90,79,.15)}
.hero.warn{border-color:var(--warn)}
.ring{position:relative;width:120px;height:120px;flex:none}
.ring svg{width:120px;height:120px;transform:rotate(-90deg)}
.rb,.rf{fill:none;stroke-width:10}
.rb{stroke:var(--line)}
.rf{stroke:var(--ok);stroke-linecap:round;stroke-dasharray:326.7;stroke-dashoffset:326.7;transition:stroke-dashoffset .5s,stroke .3s}
.bad .rf{stroke:var(--bad)}
.rv{position:absolute;inset:0;display:grid;place-content:center;text-align:center}
.rv b{font-size:30px;font-variant-numeric:tabular-nums;line-height:1}
.rv small{font-size:12px;color:var(--mut)}
.state{font-size:19px;font-weight:650}
.bad .state{color:var(--bad)}
.ok .state{color:var(--ok)}
.big{font-size:64px;font-weight:700;line-height:1;font-variant-numeric:tabular-nums}
.kv{display:flex;gap:8px;align-items:baseline;margin-top:8px;font-size:13px;color:var(--mut)}
.kv b{color:var(--txt);font-size:16px;font-variant-numeric:tabular-nums}
.spark{height:64px}
.spark svg{width:100%;height:100%;display:block}
.alert{display:flex;gap:10px;align-items:flex-start;padding:12px 14px;border-radius:12px;margin-bottom:12px;font-size:14px;border:1px solid}
.alert.bad{background:rgba(255,90,79,.12);border-color:var(--bad)}
.alert.warn{background:rgba(255,179,71,.12);border-color:var(--warn)}
.alert.ok{background:rgba(95,224,176,.1);border-color:var(--ok)}
.alert .i{margin-top:1px}
.alert.bad .i{color:var(--bad)}.alert.warn .i{color:var(--warn)}.alert.ok .i{color:var(--ok)}
.alert button{margin-left:auto}
.row{display:flex;align-items:center;gap:10px;padding:10px 0;border-top:1px solid var(--line)}
.row:first-child{border-top:0;padding-top:0}
.row .grow{flex:1;min-width:0}
.row .t{font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.row .s{font-size:12px;color:var(--mut);font-variant-numeric:tabular-nums}
.empty{color:var(--mut);font-size:14px;padding:6px 0}
.tag{font-size:11px;padding:2px 7px;border-radius:10px;border:1px solid var(--line);margin-left:6px;font-weight:600;vertical-align:middle}
.tag.new{color:var(--bad);border-color:var(--bad)}
.tag.hid{color:var(--warn);border-color:var(--warn)}
.tag.twin{color:var(--bad);border-color:var(--bad)}
.tag.gone{color:var(--mut)}
.sig{display:inline-flex;gap:2px;align-items:flex-end;height:18px;flex:none}
.sig i{width:4px;background:var(--line);border-radius:1px}
.sig i:nth-child(1){height:5px}.sig i:nth-child(2){height:9px}.sig i:nth-child(3){height:13px}.sig i:nth-child(4){height:18px}
.l1 i:nth-child(-n+1),.l2 i:nth-child(-n+2),.l3 i:nth-child(-n+3),.l4 i{background:var(--acc)}
.tiles{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-bottom:12px}
.tile{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:10px 6px;text-align:center}
.tile b{display:block;font-size:22px;font-variant-numeric:tabular-nums}
.tile span{font-size:12px;color:var(--mut)}
.tile.hot b{color:var(--bad)}
.prox{display:grid;grid-template-columns:62px 1fr 34px;gap:8px;align-items:center;font-size:13px;margin:7px 0}
.bar{height:10px;border-radius:5px;background:var(--line);overflow:hidden}
.bar i{display:block;height:100%;background:var(--acc);border-radius:5px;transition:width .4s}
.prox b{text-align:right;font-variant-numeric:tabular-nums}
.btn{background:var(--card2);color:var(--txt);border:1px solid var(--line);border-radius:10px;padding:9px 13px;font:inherit;font-size:14px;display:inline-flex;gap:7px;align-items:center;cursor:pointer}
.btn:active{background:var(--line)}
.btn .i{width:16px;height:16px}
.btns{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px}
.set{padding:12px 0;border-top:1px solid var(--line)}
.set:first-of-type{border-top:0;padding-top:0}
.set[hidden]{display:none}
.set .h{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;font-size:14px}
.set .h b{color:var(--acc);font-variant-numeric:tabular-nums;font-weight:600}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:28px;background:none;--p:50%}
input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:3px;background:linear-gradient(90deg,var(--acc) var(--p),var(--line) var(--p))}
input[type=range]::-moz-range-track{height:6px;border-radius:3px;background:linear-gradient(90deg,var(--acc) var(--p),var(--line) var(--p))}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:24px;height:24px;border-radius:50%;background:var(--txt);border:3px solid var(--acc);margin-top:-9px}
input[type=range]::-moz-range-thumb{width:18px;height:18px;border-radius:50%;background:var(--txt);border:3px solid var(--acc)}
.tg{width:48px;height:28px;border-radius:14px;border:1px solid var(--line);background:var(--line);position:relative;cursor:pointer;flex:none}
.tg i{position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:var(--txt);transition:transform .2s}
.tg[aria-checked=true]{background:var(--acc)}
.tg[aria-checked=true] i{transform:translateX(20px);background:#06222a}
.swrow{display:flex;justify-content:space-between;align-items:center;font-size:14px}
.dd{position:relative}
.ddb{width:100%;display:flex;justify-content:space-between;align-items:center;background:var(--card2);border:1px solid var(--line);color:var(--txt);border-radius:10px;padding:11px 13px;font:inherit;cursor:pointer}
.ddl{display:none;position:absolute;left:0;right:0;top:calc(100% + 6px);z-index:5;background:var(--card2);border:1px solid var(--line);border-radius:12px;max-height:210px;overflow:auto;box-shadow:0 12px 30px rgba(0,0,0,.45)}
.dd.open .ddl{display:block}
.ddl button{display:flex;justify-content:space-between;width:100%;background:none;border:0;color:var(--txt);padding:11px 13px;font:inherit;text-align:left;cursor:pointer}
.ddl button.sel{color:var(--acc);font-weight:600}
.ddl button:active{background:var(--line)}
.note{font-size:12px;color:var(--mut);margin-top:6px}
.panel{display:none}.panel.on{display:block}
#toast{position:fixed;left:50%;bottom:60px;transform:translate(-50%,30px);opacity:0;background:var(--card2);border:1px solid var(--line);padding:10px 16px;border-radius:12px;font-size:14px;transition:.25s;pointer-events:none;z-index:9;max-width:88vw}
#toast.show{opacity:1;transform:translate(-50%,0)}
#toast.bad{border-color:var(--bad)}
footer{position:fixed;left:0;right:0;bottom:0;text-align:center;padding:12px;font-size:12px;color:var(--mut);background:linear-gradient(transparent,var(--bg) 40%)}
footer b{color:var(--txt);font-weight:600}
@media(prefers-reduced-motion:reduce){*{transition:none!important}}
</style></head><body>
<svg width="0" height="0" style="position:absolute"><defs>
<symbol id="i-shield" viewBox="0 0 24 24"><path d="M12 3l8 3v6c0 5-3.5 8-8 9-4.5-1-8-4-8-9V6z"/></symbol>
<symbol id="i-radar" viewBox="0 0 24 24"><path d="M12 3a9 9 0 1 0 9 9M12 7.5a4.5 4.5 0 1 0 4.5 4.5M12 12L19 5"/></symbol>
<symbol id="i-ghost" viewBox="0 0 24 24"><path d="M5 21V11a7 7 0 0 1 14 0v10l-3-2-2 2-2-2-2 2-2-2zM9.5 11h.01M14.5 11h.01"/></symbol>
<symbol id="i-clock" viewBox="0 0 24 24"><path d="M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM12 7v5l3 2"/></symbol>
<symbol id="i-alert" viewBox="0 0 24 24"><path d="M12 3l10 18H2zM12 10v4M12 17h.01"/></symbol>
<symbol id="i-check" viewBox="0 0 24 24"><path d="M4 12l5 5L20 6"/></symbol>
<symbol id="i-refresh" viewBox="0 0 24 24"><path d="M20 11a8 8 0 1 0-2.3 5.7M20 4v7h-7"/></symbol>
<symbol id="i-trash" viewBox="0 0 24 24"><path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/></symbol>
<symbol id="i-chev" viewBox="0 0 24 24"><path d="M6 9l6 6 6-6"/></symbol>
<symbol id="i-bolt" viewBox="0 0 24 24"><path d="M13 3L5 14h6l-1 7 8-11h-6z"/></symbol>
</defs></svg>

<header>
 <div class="logo"><svg class="i"><use href="#i-shield"/></svg></div>
 <div><h1>WiFi Sentinel</h1><p class="mut"><span class="dot" id="dot"></span><span id="sub">Connecting</span></p></div>
 <div class="chip"><svg class="i"><use href="#i-clock"/></svg><span id="up">00:00:00</span></div>
</header>

<nav class="seg" id="seg">
 <button data-m="0"><svg class="i"><use href="#i-shield"/></svg><span>Deauth guard</span></button>
 <button data-m="1"><svg class="i"><use href="#i-radar"/></svg><span>Crowd radar</span></button>
 <button data-m="2"><svg class="i"><use href="#i-ghost"/></svg><span>Ghost hunter</span></button>
</nav>

<section class="panel" id="p0">
 <div class="alert bad" id="d_alert" hidden><svg class="i"><use href="#i-alert"/></svg><div><b>Deauth flood detected</b><br><span id="d_alert_t"></span></div></div>
 <div class="card hero ok" id="d_card">
  <div class="ring"><svg viewBox="0 0 120 120"><circle cx="60" cy="60" r="52" class="rb"/><circle cx="60" cy="60" r="52" class="rf" id="d_ring"/></svg>
   <div class="rv"><b id="d_pps">0</b><small>pkts/sec</small></div></div>
  <div><div class="state" id="d_state">Network secure</div><p class="mut" id="d_sub" style="font-size:13px">Listening</p>
   <div class="kv"><span>Total frames</span><b id="d_total">0</b></div></div>
 </div>
 <div class="card"><h2>Last 60 seconds</h2><div class="spark" id="d_spark"></div></div>
 <div class="card"><h2>Attack log</h2><div id="d_log"></div></div>
</section>

<section class="panel" id="p1">
 <div class="card"><h2>Estimated people nearby</h2>
  <div style="display:flex;align-items:flex-end;gap:14px"><div class="big" id="r_est">0</div>
  <div class="mut" style="font-size:13px;padding-bottom:6px"><span id="r_st">0</span> stable devices<br><span id="r_rn">0</span> random addresses</div></div>
 </div>
 <div class="card"><h2>Distance (signal strength)</h2>
  <div class="prox"><span>Near</span><div class="bar"><i id="b_near"></i></div><b id="n_near">0</b></div>
  <div class="prox"><span>Medium</span><div class="bar"><i id="b_mid"></i></div><b id="n_mid">0</b></div>
  <div class="prox"><span>Far</span><div class="bar"><i id="b_far"></i></div><b id="n_far">0</b></div>
 </div>
 <div class="card"><h2>Last 5 minutes</h2><div class="spark" id="r_spark"></div>
  <p class="note">Addresses are hashed on the device and never stored or shown.</p></div>
 <div class="btns"><button class="btn" id="r_clear"><svg class="i"><use href="#i-trash"/></svg>Reset count</button></div>
</section>

<section class="panel" id="p2">
 <div class="alert warn" id="g_alert" hidden><svg class="i"><use href="#i-alert"/></svg><div id="g_alert_t"></div></div>
 <div class="tiles">
  <div class="tile"><b id="g_total">0</b><span>Networks</span></div>
  <div class="tile" id="t_new"><b id="g_new">0</b><span>New</span></div>
  <div class="tile"><b id="g_hid">0</b><span>Hidden</span></div>
  <div class="tile" id="t_twin"><b id="g_twin">0</b><span>Twins</span></div>
 </div>
 <div class="btns">
  <button class="btn" id="g_scan"><svg class="i"><use href="#i-refresh"/></svg>Scan now</button>
  <button class="btn" id="g_learn"><svg class="i"><use href="#i-check"/></svg>Trust all current</button>
  <button class="btn" id="g_clear"><svg class="i"><use href="#i-trash"/></svg>Clear list</button>
 </div>
 <div class="card"><h2 id="g_h">Access points</h2><div id="g_list"></div></div>
</section>

<div class="card" id="settings"><h2>Settings</h2>
 <div class="set" data-m="01"><div class="h"><span>Radio channel</span></div>
  <div class="dd" id="dd"><button class="ddb" id="ddb"><span id="ddv">Channel 6</span><svg class="i"><use href="#i-chev"/></svg></button><div class="ddl" id="ddl"></div></div>
  <p class="note">Sniffs one channel only. Match it to the network you protect.</p></div>
 <div class="set" data-m="0"><div class="h"><span>Alert threshold</span><b id="v_thr">5 pkt/s</b></div><input type="range" id="s_thr" min="1" max="50" value="5"></div>
 <div class="set" data-m="1"><div class="h"><span>Ignore weaker than</span><b id="v_rssi">-85 dBm</b></div><input type="range" id="s_rssi" min="-95" max="-30" value="-85"></div>
 <div class="set" data-m="1"><div class="h"><span>Count window</span><b id="v_win">60 s</b></div><input type="range" id="s_win" min="30" max="300" step="10" value="60"></div>
 <div class="set" data-m="1"><div class="h"><span>Random addresses per person</span><b id="v_div">3x</b></div><input type="range" id="s_div" min="1" max="8" value="3"></div>
 <div class="set" data-m="02"><div class="swrow"><span>Buzzer and LED alerts</span><button class="tg" id="buz" role="switch" aria-checked="true" aria-label="Buzzer"><i></i></button></div></div>
</div>

<div id="toast"></div>
<footer>Made by <b>Tasfiqul Alam Pabel</b></footer>

<script>
const $=s=>document.querySelector(s);
const ic=n=>`<svg class="i"><use href="#i-${n}"/></svg>`;
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const fmt=s=>[Math.floor(s/3600),Math.floor(s/60)%60,s%60].map(x=>String(x).padStart(2,'0')).join(':');
let S=null,inited=false,pend={},pt=null,lastAlert=0,lastNew=0,tt=null;

function toast(m,bad){const t=$('#toast');t.textContent=m;t.className='show'+(bad?' bad':'');clearTimeout(tt);tt=setTimeout(()=>t.className='',3200)}
async function api(p){try{const r=await fetch(p,{cache:'no-store'});return await r.json()}catch(e){return null}}
function cfg(k,v){pend[k]=v;clearTimeout(pt);pt=setTimeout(async()=>{const q=new URLSearchParams(pend).toString();pend={};await api('/api/cfg?'+q)},250)}

function spark(el,d,col){const w=300,h=60,m=Math.max(...d,1);
 const p=d.map((v,i)=>(i/(d.length-1)*w).toFixed(1)+','+(h-4-v/m*(h-10)).toFixed(1)).join(' ');
 el.innerHTML=`<svg viewBox="0 0 ${w} ${h}" preserveAspectRatio="none"><polyline points="0,${h} ${p} ${w},${h}" fill="${col}" opacity=".15"/><polyline points="${p}" fill="none" stroke="${col}" stroke-width="2" stroke-linejoin="round" vector-effect="non-scaling-stroke"/></svg>`}
const lvl=r=>r>-55?4:r>-67?3:r>-78?2:1;
const sig=r=>`<span class="sig l${lvl(r)}"><i></i><i></i><i></i><i></i></span>`;

function setSlider(id,v,u){const e=$('#s_'+id);e.value=v;$('#v_'+id).textContent=v+u;e.style.setProperty('--p',(v-e.min)/(e.max-e.min)*100+'%')}
[['thr','thr',' pkt/s'],['rssi','rssi',' dBm'],['win','win',' s'],['div','div','x']].forEach(([id,k,u])=>{
 const e=$('#s_'+id);e.oninput=()=>setSlider(id,e.value,u);e.onchange=()=>cfg(k,e.value)});
function setTg(on){$('#buz').setAttribute('aria-checked',on?'true':'false')}
$('#buz').onclick=()=>{const on=$('#buz').getAttribute('aria-checked')!=='true';setTg(on);cfg('buz',on?1:0)};
function setCh(c){$('#ddv').textContent='Channel '+c;[...$('#ddl').children].forEach(b=>b.classList.toggle('sel',b.dataset.c==c))}
for(let c=1;c<=13;c++){const b=document.createElement('button');b.dataset.c=c;b.innerHTML=`<span>Channel ${c}</span><span class="mut">${(2407+c*5)} MHz</span>`;
 b.onclick=()=>{setCh(c);$('#dd').classList.remove('open');cfg('ch',c);toast('Restarting radio on channel '+c)};$('#ddl').appendChild(b)}
$('#ddb').onclick=e=>{e.stopPropagation();$('#dd').classList.toggle('open')};
document.addEventListener('click',()=>$('#dd').classList.remove('open'));

document.querySelectorAll('#seg button').forEach(b=>b.onclick=async()=>{await api('/api/mode?m='+b.dataset.m);lastAlert=0;lastNew=0;poll(true)});
$('#r_clear').onclick=async()=>{await api('/api/clear');toast('Count reset')};
$('#g_clear').onclick=async()=>{await api('/api/clear');toast('List cleared')};
$('#g_learn').onclick=async()=>{await api('/api/baseline');toast('Current networks are now trusted')};
$('#g_scan').onclick=async()=>{await api('/api/scan');toast('Scanning')};

function renderD(d,j){
 const bad=d.alert==1,max=Math.max(j.thr*3,10),f=Math.min(1,d.pps/max);
 $('#d_ring').style.strokeDashoffset=326.7*(1-f);
 $('#d_card').className='card hero '+(bad?'bad':'ok');
 $('#d_pps').textContent=d.pps;$('#d_total').textContent=d.total;
 $('#d_state').textContent=bad?'Under attack':'Network secure';
 $('#d_sub').textContent='Listening on channel '+j.ch;
 $('#d_alert').hidden=!bad;
 if(bad){const e=d.ev[0];$('#d_alert_t').textContent=e?`Source ${e.s}, ${d.pps} frames in the last second`:''}
 if(bad&&!lastAlert)toast('Deauth attack detected',1);lastAlert=bad?1:0;
 spark($('#d_spark'),d.hist,bad?'#ff5a4f':'#4cc9f0');
 $('#d_log').innerHTML=d.ev.length?d.ev.map(e=>`<div class="row">${ic('bolt')}<div class="grow"><div class="t">${e.s}</div><div class="s">to ${e.d} | ${e.r} dBm</div></div><div style="text-align:right"><b>${e.c}</b><div class="s">${fmt(e.at)}</div></div></div>`).join(''):'<p class="empty">No attacks recorded. The log fills when frames pass the threshold.</p>';
}
function renderR(r){
 $('#r_est').textContent=r.est;$('#r_st').textContent=r.stable;$('#r_rn').textContent=r.rnd;
 const t=Math.max(r.near+r.mid+r.far,1);
 [['near',r.near],['mid',r.mid],['far',r.far]].forEach(([k,v])=>{$('#b_'+k).style.width=v/t*100+'%';$('#n_'+k).textContent=v});
 spark($('#r_spark'),r.hist,'#5fe0b0');
}
function renderG(g){
 const a=g.aps;let nw=0,hd=0,tw=0;a.forEach(x=>{if(x.nw)nw++;if(x.h)hd++;if(x.tw)tw++});
 $('#g_total').textContent=a.length;$('#g_new').textContent=nw;$('#g_hid').textContent=hd;$('#g_twin').textContent=tw;
 $('#t_new').className='tile'+(nw?' hot':'');$('#t_twin').className='tile'+(tw?' hot':'');
 const al=$('#g_alert');al.hidden=!(nw||tw);
 $('#g_alert_t').innerHTML=(nw?`<b>${nw} unknown network${nw>1?'s':''} appeared</b><br>`:'')+(tw?`<b>${tw} possible evil twin${tw>1?'s':''}</b><br>Same name with different security.`:'Check the list below, then trust them if they are yours.');
 if(nw>lastNew)toast('New access point detected',1);lastNew=nw;
 $('#g_h').textContent=g.scanning?'Access points (scanning)':'Access points';
 a.sort((x,y)=>(y.nw-x.nw)||(y.tw-x.tw)||(y.p-x.p)||(y.r-x.r));
 $('#g_list').innerHTML=a.length?a.map(x=>`<div class="row">${sig(x.r)}<div class="grow"><div class="t">${x.h?'(hidden network)':esc(x.n)}${x.nw?'<span class="tag new">NEW</span>':''}${x.h?'<span class="tag hid">HIDDEN</span>':''}${x.tw?'<span class="tag twin">TWIN</span>':''}${x.p?'':'<span class="tag gone">GONE</span>'}</div><div class="s">${x.b} | ch ${x.c} | ${x.e}</div></div><div class="s">${x.r} dBm</div></div>`).join(''):'<p class="empty">Waiting for the first scan. It takes a few seconds.</p>';
}
function render(j){
 $('#up').textContent=fmt(j.up);
 document.querySelectorAll('#seg button').forEach(b=>b.classList.toggle('on',b.dataset.m==j.mode));
 document.querySelectorAll('.panel').forEach((p,i)=>p.classList.toggle('on',i==j.mode));
 document.querySelectorAll('[data-m]').forEach(e=>{if(e.closest('#seg'))return;e.hidden=!e.dataset.m.includes(j.mode)});
 if(!inited){inited=true;setCh(j.ch);setSlider('thr',j.thr,' pkt/s');setSlider('rssi',j.rssi,' dBm');setSlider('win',j.win,' s');setSlider('div',j.div,'x');setTg(j.buz==1)}
 if(j.mode==0&&j.d)renderD(j.d,j);
 if(j.mode==1&&j.r)renderR(j.r);
 if(j.mode==2&&j.g)renderG(j.g);
}
let timer=null;
async function poll(once){
 clearTimeout(timer);
 const j=await api('/api/state');
 $('#dot').className='dot'+(j?' on':'');$('#sub').textContent=j?'Connected to device':'Device offline, retrying';
 if(j){S=j;render(j)}
 timer=setTimeout(poll,S&&S.mode==2?2000:1000);
}
poll();
</script></body></html>)=====";
