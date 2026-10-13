// Web pages served by the bridge. Self-contained (no internet resources) so
// they work on the setup hotspot. Light slate/teal OpenGlance theme.
#pragma once
#include <pgmspace.h>

#define OG_COMMON_CSS R"CSS(
:root{--bg:#eef2f4;--card:#fff;--ink:#23323d;--muted:#5f707c;--line:#d8e0e5;--teal:#16897f;--teal2:#0f6f67;--tealbg:#e3f3f1;--tealink:#0f6f67;
--warn:#a35c00;--warnbg:#fff4e0;--err:#b3261e;--errbg:#fde9e7;--ok:#1d7a3e;--okbg:#e5f4ea;--r:14px}
@media (prefers-color-scheme:dark){:root{--bg:#1c2328;--card:#252e35;--ink:#e6ecef;--muted:#9fb0ba;--line:#37434c;--tealbg:#1d4441;--tealink:#7fdcd1;--warnbg:#3a2e17;--warn:#f0b45a;--err:#ff8a80;--ok:#7ed69a;--errbg:#3d2321;--okbg:#1d3826}}
*{box-sizing:border-box}a{color:var(--tealink)}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
header{display:flex;align-items:center;gap:12px;padding:14px 18px;background:var(--card);border-bottom:1px solid var(--line);position:sticky;top:0;z-index:5;flex-wrap:wrap}
header h1{font-size:19px;margin:0;font-weight:650;letter-spacing:.2px}header .sp{flex:1}
.logo{width:34px;height:34px}
main{max-width:1180px;margin:0 auto;padding:18px 16px 60px;display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:16px 18px}
.card h2{font-size:15px;margin:0 0 12px;font-weight:650;display:flex;align-items:center;gap:8px}
.wide{grid-column:1/-1}.late{order:2}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:8px 0}
.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;font-size:14px}.kv dt{color:var(--muted)}.kv dd{margin:0;word-break:break-word}
label{font-size:13px;color:var(--muted);display:block;margin:8px 0 3px}
input[type=text],input[type=password],input[type=number],input[type=date],input[type=time],select{width:100%;padding:8px 10px;border:1px solid var(--line);border-radius:9px;background:var(--bg);color:var(--ink);font:inherit}
input[type=range]{width:100%;accent-color:var(--teal)}
button{font:inherit;font-weight:600;border:0;border-radius:9px;padding:8px 14px;background:var(--teal);color:#fff;cursor:pointer}
button:hover{background:var(--teal2)}button.sec{background:var(--tealbg);color:var(--tealink)}button.sec:hover{filter:brightness(.96)}
button.danger{background:var(--errbg);color:var(--err)}button:disabled{opacity:.45;cursor:not-allowed}
.pill{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border-radius:99px;font-size:13px;font-weight:600;background:var(--tealbg);color:var(--tealink)}
.pill.bad{background:var(--errbg);color:var(--err)}.pill.mid{background:var(--warnbg);color:var(--warn)}.pill.good{background:var(--okbg);color:var(--ok)}
.dot{width:8px;height:8px;border-radius:50%;background:currentColor}
.note{font-size:13px;color:var(--muted);margin:6px 0}
.warn{background:var(--warnbg);color:var(--warn);border-radius:10px;padding:9px 12px;font-size:13px;margin:8px 0}
.alert{background:var(--errbg);color:var(--err);border-radius:10px;padding:9px 12px;font-size:13px;margin:8px 0}
.okbox{background:var(--okbg);color:var(--ok);border-radius:10px;padding:9px 12px;font-size:13px;margin:8px 0}
.switch{display:flex;justify-content:space-between;align-items:center;padding:7px 0;border-bottom:1px solid var(--line);font-size:14px}
.switch:last-child{border-bottom:0}.switch input{width:20px;height:20px;accent-color:var(--teal)}
.list{border:1px solid var(--line);border-radius:10px;max-height:220px;overflow:auto}
.list .item{display:flex;align-items:center;justify-content:space-between;padding:8px 10px;border-bottom:1px solid var(--line)}
.list .item:last-child{border-bottom:0}
.log{font:12.5px/1.5 ui-monospace,Menlo,monospace;max-height:260px;overflow:auto;background:var(--bg);border-radius:10px;padding:8px 10px}
.log div.e{color:var(--err)}.log span{color:var(--muted);margin-right:8px}
.big{font-size:26px;font-weight:650;font-variant-numeric:tabular-nums}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:var(--ink);color:var(--card);padding:10px 16px;border-radius:10px;font-size:14px;opacity:0;transition:.25s;pointer-events:none;max-width:92vw;z-index:9}
#toast.show{opacity:1}#toast.err{background:var(--err);color:#fff}
.pinbox{background:var(--tealbg);border-radius:12px;padding:12px;margin:10px 0}
.pinbox input{font-size:22px;letter-spacing:6px;text-align:center}
progress{width:100%;height:10px;accent-color:var(--teal)}
details summary{cursor:pointer;color:var(--tealink);font-weight:600;font-size:14px;margin:6px 0}
ol{padding-left:20px;margin:6px 0}ol li{margin:3px 0;font-size:14px}
@media (max-width:420px){main{grid-template-columns:1fr;padding:12px}}
)CSS"

#define OG_LOGO R"SVG(<svg class="logo" viewBox="0 0 64 64" aria-hidden="true"><circle cx="32" cy="32" r="29" fill="#f6f1e7" stroke="#2f3b44" stroke-width="5"/><circle cx="32" cy="32" r="22" fill="none" stroke="#16897f" stroke-width="4" stroke-dasharray="104 200" transform="rotate(-90 32 32)"/><path d="M32 32V16M32 32l10 7" stroke="#2f3b44" stroke-width="4.5" stroke-linecap="round"/><circle cx="32" cy="32" r="3.5" fill="#16897f"/></svg>)SVG"

#define OG_TZ_JS R"JS(
const ZONES=[["Sydney / Melbourne / Canberra / Hobart","AEST-10AEDT,M10.1.0,M4.1.0/3"],["Brisbane","AEST-10"],["Adelaide","ACST-9:30ACDT,M10.1.0,M4.1.0/3"],
["Darwin","ACST-9:30"],["Perth","AWST-8"],["Auckland","NZST-12NZDT,M9.5.0,M4.1.0/3"],["Tokyo","JST-9"],["Singapore / Hong Kong","<+08>-8"],
["London","GMT0BST,M3.5.0/1,M10.5.0"],["Paris / Berlin / Rome","CET-1CEST,M3.5.0,M10.5.0/3"],["New York","EST5EDT,M3.2.0,M11.1.0"],
["Chicago","CST6CDT,M3.2.0,M11.1.0"],["Denver","MST7MDT,M3.2.0,M11.1.0"],["Los Angeles","PST8PDT,M3.2.0,M11.1.0"],["UTC","UTC0"]];
function strength(r){return r>-60?"strong":r>-72?"good":r>-82?"weak":"very weak"}
async function scanInto(p){p.btn.disabled=true;p.note.textContent="Scanning (about 3 seconds)…";
try{const j=await(await fetch("/api/networks",{cache:"no-store"})).json();p.sel.innerHTML="";
p.sel.add(new Option(j.networks.length?"Choose a network…":"No networks found","",true,true));
for(const n of j.networks){const o=new Option(n.ssid+" — "+strength(n.rssi)+(n.secure?"":" (open)"),n.ssid);o.dataset.secure=n.secure?"1":"0";p.sel.add(o)}
p.sel.add(new Option("Other / hidden network…","__other"));p.note.textContent=j.networks.length+" network"+(j.networks.length===1?"":"s")+" found";}
catch(e){p.note.textContent="Scan failed — press Scan again"}p.btn.disabled=false;pickChanged(p)}
function pickChanged(p){const v=p.sel.value,o=p.sel.options[p.sel.selectedIndex];p.other.style.display=v==="__other"?"block":"none";
const open=o&&o.dataset.secure==="0";p.passWrap.style.display=open?"none":"block";if(open)p.pass.value=""}
function pickedSSID(p){return p.sel.value==="__other"?p.other.value.trim():p.sel.value}
function fillZones(sel,current){sel.innerHTML="";let found=false;for(const[n,z]of ZONES){const o=new Option(n,z);if(z===current){o.selected=true;found=true}sel.add(o)}
const c=new Option("Custom POSIX rule…","custom");if(current&&!found){c.selected=true}sel.add(c);return found}
)JS"

static const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenGlance Setup</title><style>)HTML" OG_COMMON_CSS R"HTML(main{display:block;max-width:520px}</style></head><body>
<header>)HTML" OG_LOGO R"HTML(<h1>OpenGlance Bridge setup</h1></header>
<main><div class="card">
<h2>1 · Your Wi-Fi</h2>
<p class="note">The bridge joins your home Wi-Fi to get the internet time and to show its control page. 2.4 GHz networks only.</p>
<label for="ssidSel">Network</label>
<div class="row" style="margin-top:0"><select id="ssidSel" style="flex:1" onchange="pickChanged(P)"><option value="">Scanning…</option></select><button class="sec" id="scanBtn" onclick="scanInto(P)">Scan again</button></div>
<p class="note" id="scanNote"></p>
<input type="text" id="ssidOther" placeholder="Network name" style="display:none">
<div id="passWrap"><label for="pass">Wi-Fi password</label><input type="password" id="pass" autocomplete="current-password"></div>
<h2 style="margin-top:18px">2 · Time zone</h2>
<select id="tz" onchange="tzChanged()"></select><input type="text" id="tzc" placeholder="POSIX rule, e.g. AEST-10AEDT,M10.1.0,M4.1.0/3" style="display:none;margin-top:8px">
<div class="row" style="margin-top:16px"><button onclick="save()">Save and connect</button></div>
<div id="msg"></div></div></main>
<div id="toast"></div>
<script>)HTML" OG_TZ_JS R"HTML(
const $=s=>document.querySelector(s);fillZones($("#tz"),"AEST-10AEDT,M10.1.0,M4.1.0/3");
const P={sel:$("#ssidSel"),btn:$("#scanBtn"),note:$("#scanNote"),other:$("#ssidOther"),pass:$("#pass"),passWrap:$("#passWrap")};
function tzChanged(){$("#tzc").style.display=$("#tz").value==="custom"?"block":"none"}
function toast(t,e){const x=$("#toast");x.textContent=t;x.className="show"+(e?" err":"");clearTimeout(x._t);x._t=setTimeout(()=>x.className="",4000)}
async function save(){const ssid=pickedSSID(P);if(!ssid)return toast("Choose your Wi-Fi network",true);
const tzSel=$("#tz");const tz=tzSel.value==="custom"?$("#tzc").value.trim():tzSel.value;const tzName=tzSel.value==="custom"?tz:tzSel.options[tzSel.selectedIndex].text;
const r=await fetch("/api/setup",{method:"POST",body:new URLSearchParams({ssid,pass:P.pass.value,tz,tzName})});
const j=await r.json();if(!j.ok)return toast(j.error,true);$("#msg").innerHTML='<div class="okbox">'+j.message+'</div>'}
scanInto(P);
</script></body></html>)HTML";

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenGlance</title><style>)HTML" OG_COMMON_CSS R"HTML(
header{position:static}nav.tabs{display:flex;gap:4px;padding:8px 14px;background:var(--card);border-bottom:1px solid var(--line);position:sticky;top:0;z-index:4;overflow-x:auto;scrollbar-width:none}
nav.tabs::-webkit-scrollbar{display:none}
nav.tabs button{background:transparent;color:var(--muted);border-radius:99px;padding:7px 14px;white-space:nowrap;font-weight:600}
nav.tabs button:hover{background:var(--tealbg);color:var(--tealink)}nav.tabs button.on{background:var(--teal);color:#fff}
.tab{display:none}.tab.on{display:grid}
.bars{display:grid;grid-template-columns:repeat(12,1fr);gap:4px;align-items:end;height:90px;margin:6px 0 2px}
.bars div{border-radius:5px 5px 2px 2px;min-height:3px;position:relative}
.bars div span{position:absolute;top:-17px;left:50%;transform:translateX(-50%);font-size:11px;color:var(--muted);white-space:nowrap}
.hours{display:grid;grid-template-columns:repeat(12,1fr);gap:4px;font-size:11px;color:var(--muted);text-align:center}
.cta{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}
.linkish{background:none;color:var(--tealink);padding:0;font-weight:600}.linkish:hover{background:none;text-decoration:underline}
.scale{height:12px;border-radius:6px;background:linear-gradient(90deg,#2060ff,#00c070,#ffc000,#ff3000);margin:4px 0 2px}
</style></head><body>
<header>)HTML" OG_LOGO R"HTML(<h1>OpenGlance</h1><span class="sp"></span>
<span class="pill" id="hClock"><span class="dot"></span><span>…</span></span>
<span class="pill" id="hTime"><span class="dot"></span><span>…</span></span>
<span class="pill" id="hWifi"><span class="dot"></span><span>…</span></span></header>
<nav class="tabs" id="tabs">
<button data-t="home">Home</button><button data-t="message">Messages</button><button data-t="alarms">Alarms</button><button data-t="weather">Weather</button>
<button data-t="display">Display</button><button data-t="clock">Clock</button><button data-t="system">System</button></nav>

<main class="tab" data-tab="home">
<section class="card wide" id="pairCta" style="display:none"><div class="cta"><div><h2 style="margin:0 0 4px">Pair your clock</h2>
<p class="note" style="margin:0">No clock is paired with this bridge yet.</p></div><button onclick="show('clock')">Pair a clock</button></div></section>
<section class="card"><h2>Clock</h2>
<div class="big" id="cState">…</div><p class="note" id="cText"></p>
<dl class="kv"><dt>Clock</dt><dd id="cName">—</dd><dt>Battery</dt><dd id="cBat">—</dd>
<dt>Last time read</dt><dd id="cRead">—</dd><dt>Time supplied</dt><dd id="cSupplied">—</dd></dl>
<div class="row"><button id="bSync" onclick="act('/api/sync')">Sync time now</button>
<button class="sec" id="bConnect" onclick="act('/api/connect')">Connect</button>
<button class="sec" id="bDisconnect" onclick="act('/api/disconnect')">Disconnect</button>
<button class="sec" id="bRefresh" onclick="act('/api/refresh')">Refresh</button></div></section>
<section class="card"><h2>Time &amp; date</h2>
<div class="big" id="tNow">—</div><p class="note" id="tSrc"></p>
<button class="linkish" onclick="show('system')">Time zone and manual time →</button></section>
<section class="card"><h2>Weather</h2><div class="big" id="wHomeNow">—</div><p class="note" id="wHomeText"></p>
<button class="linkish" onclick="show('weather')">Weather settings →</button></section>
<section class="card wide"><h2>Recent activity</h2><div class="log" id="logShort"></div></section>
</main>

<main class="tab" data-tab="message">
<section class="card"><h2>Message</h2>
<label for="msgText">Text (up to 64 plain characters)</label><input type="text" id="msgText" maxlength="64" value="Hello world">
<div class="row"><div style="flex:1"><label>Animation</label><select id="msgAnim">
<option value="1">Pulse</option><option value="2">Wave</option><option value="10">Fire</option><option value="11">Wheel</option><option value="12">Flower</option>
<option value="15">Sun</option><option value="17">Cloud</option><option value="0">None</option></select></div>
<div style="flex:1"><label>Colour</label><select id="msgColor"><option value="18">Turquoise</option><option value="23">Lime</option><option value="15">Blue</option>
<option value="5">Red</option><option value="12">White</option></select></div>
</div>
<label for="msgSound">Sound</label><div class="row" style="margin-top:0"><select id="msgSound" class="sounds" data-none="1" style="flex:1"></select>
<button class="sec needs" onclick="testSound('#msgSound')">Test</button></div>
<div class="row"><button class="needs" onclick="act('/api/message',{text:$('#msgText').value,animation:$('#msgAnim').value,color:$('#msgColor').value,sound:$('#msgSound').value})">Send to clock</button></div>
<p class="note">If no sound plays, check that Mute is off on the Display tab.</p></section>
<section class="card"><h2>Hourly chime</h2>
<div class="switch"><span>Chime every hour (shows the time with a sound)</span><input type="checkbox" id="chOn" onchange="act('/api/chime',{on:this.checked?1:0})"></div>
<label for="chSound">Sound</label><div class="row" style="margin-top:0"><select id="chSound" class="sounds" style="flex:1" onchange="act('/api/chime',{sound:this.value})"></select>
<button class="sec needs" onclick="testSound('#chSound')">Test</button></div>
<label>Quiet hours (no chime)</label><div class="row" style="margin-top:0"><span class="note">from</span><select id="chFrom" class="hrs" style="flex:1"></select>
<span class="note">to</span><select id="chTo" class="hrs" style="flex:1"></select></div>
<div class="row"><button class="sec" onclick="act('/api/chime',{quietFrom:$('#chFrom').value,quietTo:$('#chTo').value})">Save quiet hours</button></div>
<p class="note">Set both to the same hour to chime around the clock. The clock also mutes sounds in its own night mode.</p></section>
<section class="card"><h2>Timer</h2>
<div class="big" id="tmLeft">—</div><p class="note">The countdown here is an estimate from when the timer was sent.</p>
<div class="row"><input type="number" id="tmMin" min="1" max="720" value="10" style="width:110px"><span class="note">minutes</span>
<button class="needs" onclick="act('/api/timer',{minutes:$('#tmMin').value})">Start</button>
<button class="sec needs" onclick="act('/api/timer/stop')">Stop</button></div>
<div class="row"><button class="sec needs" onclick="quick(5)">5 min</button><button class="sec needs" onclick="quick(15)">15 min</button>
<button class="sec needs" onclick="quick(30)">30 min</button><button class="sec needs" onclick="quick(60)">1 hour</button></div></section>
</main>

<main class="tab" data-tab="alarms">
<section class="card"><h2>Alarms</h2>
<div id="alList"></div>
<div class="row"><button id="alAdd" onclick="alEdit(-1)">Add alarm</button></div>
<p class="note" id="alNote">The clock holds up to 4 alarms.</p></section>
<section class="card" id="alEditor" style="display:none"><h2 id="alEdTitle">New alarm</h2>
<label for="alTime">Time</label><input type="time" id="alTime" value="07:00">
<label>Days</label><div class="row" id="alDays" style="margin-top:0"></div>
<div class="row"><button class="sec" onclick="alSetDays(31)">Weekdays</button><button class="sec" onclick="alSetDays(96)">Weekend</button>
<button class="sec" onclick="alSetDays(127)">Every day</button><button class="sec" onclick="alSetDays(0)">Once</button></div>
<label for="alSound">Sound</label><div class="row" style="margin-top:0"><select id="alSound" class="sounds" style="flex:1"></select><button class="sec needs" onclick="testSound('#alSound')">Test</button></div>
<label for="alText">Label on the display (up to 16 characters)</label><input type="text" id="alText" maxlength="16" placeholder="Wake up">
<div class="row"><button onclick="alSave()">Save alarm</button><button class="sec" onclick="$('#alEditor').style.display='none'">Cancel</button></div>
<p class="note">“Once” sends the alarm with no repeat days, as the original app did for one-off alarms.</p></section>
<section class="card"><h2>Alarm ringing?</h2>
<div class="row"><button class="needs" onclick="act('/api/alarms/stop')">Stop the alarm now</button></div>
<p class="note">The clock can't report which alarms it holds. Saving an alarm here replaces <b>every</b> alarm on the clock, including old ones set from the phone app.</p>
<div class="row"><button class="danger" onclick="if(confirm('Delete every alarm on the clock and on this bridge?'))act('/api/alarms/clear')">Delete all alarms on the clock</button></div></section>
</main>

<main class="tab" data-tab="weather">
<section class="card"><h2>Weather on the clock</h2>
<div class="switch"><span><b>Show weather on the ring</b></span><input type="checkbox" id="wOn" onchange="wxSave({on:this.checked?1:0})"></div>
<div class="switch"><span>Temperature ring (stays up all the time)</span><input type="checkbox" id="wTemp" onchange="wxSave({temp:this.checked?1:0})"></div>
<div class="switch"><span>Also show the temperature on the display<br><span class="note">Off keeps the time and date on the display</span></span><input type="checkbox" id="wText" onchange="wxSave({text:this.checked?1:0})"></div>
<label for="wRainMode">When rain is coming</label><select id="wRainMode" onchange="wxSave({rainMode:this.value})">
<option value="1">Alternate rain and temperature</option><option value="2">Show rain instead of temperature</option><option value="0">Don't show rain</option></select>
<div id="wCycleRow"><label for="wCycle">Switch rings every</label><select id="wCycle" onchange="wxSave({cycle:this.value})">
<option value="10">10 seconds</option><option value="20">20 seconds</option><option value="30">30 seconds</option><option value="60">1 minute</option>
<option value="120">2 minutes</option><option value="300">5 minutes</option><option value="600">10 minutes</option></select></div>
<div id="wRainRules"><label>Rain is coming when an hour has at least <b id="wMmVal"></b> mm</label><input type="range" id="wMm" min="0.1" max="5" step="0.1">
<label>… and at least <b id="wPctVal"></b>% chance</label><input type="range" id="wPct" min="10" max="90" step="5">
<label for="wHours">… within the next</label><select id="wHours" onchange="wxSave({rainHours:this.value})">
<option value="2">2 hours</option><option value="3">3 hours</option><option value="6">6 hours</option><option value="12">12 hours</option></select></div>
<div class="row"><button class="sec" id="wRefresh" onclick="act('/api/weather/refresh')">Update forecast now</button></div>
<p class="note" id="wStatus"></p>
<p class="note">The clock hides the rings by itself in night mode.</p></section>

<section class="card"><h2>Next 12 hours</h2>
<p class="note" id="wPlaceLine">—</p>
<div class="bars" id="wTBars"></div><div class="hours" id="wTHours"></div>
<p class="note" style="margin-top:12px" id="wRainLine">Rain</p>
<div class="bars" id="wRBars" style="height:60px"></div><div class="hours" id="wRHours"></div>
<p class="note" style="margin-top:10px">Weather data by <a href="https://open-meteo.com/" target="_blank" rel="noopener">Open-Meteo.com</a> (CC BY 4.0).</p></section>

<section class="card"><h2>Location</h2>
<p class="note">Current: <b id="wPlace">not set</b></p>
<div class="row"><input type="text" id="wQuery" placeholder="Town or suburb" style="flex:1" onkeydown="if(event.key==='Enter')wxSearch()"><button class="sec" onclick="wxSearch()">Search</button></div>
<div class="list" id="wResults" style="display:none"></div>
<p class="note">Place search uses Open-Meteo directly from this browser.</p></section>

<section class="card"><h2>Temperature colours</h2>
<div class="row"><div style="flex:1"><label>Units</label><select id="wUnit" onchange="wxSave({unit:this.value})"><option value="C">Celsius</option><option value="F">Fahrenheit</option></select></div></div>
<div class="scale" id="wScale"></div>
<div class="row"><div style="flex:1"><label>Blue at (cold)</label><input type="number" id="wColdAt"></div><div style="flex:1"><label>Red at (hot)</label><input type="number" id="wHotAt"></div></div>
<div class="row"><button class="sec" onclick="wxSave({coldAt:$('#wColdAt').value,hotAt:$('#wHotAt').value})">Save range</button></div>
<p class="note">Temperatures are coloured blue → green → amber → red across this range. The clock blends between the colours of the day's lowest and highest temperature.</p></section>
</main>

<main class="tab" data-tab="display">
<section class="card"><h2>Display</h2>
<div class="switch"><span>Digital time on the display</span><input type="checkbox" class="needs" data-f="11"></div>
<div class="switch"><span>12-hour time</span><input type="checkbox" class="needs" data-f="12"></div>
<div class="switch"><span>Hour points always on</span><input type="checkbox" class="needs" data-f="9"></div>
<div class="switch"><span>Automatic night mode</span><input type="checkbox" class="needs" data-f="2"></div>
<div class="switch"><span>Mute sounds</span><input type="checkbox" class="needs" data-f="4"></div>
<label for="dateFmt">Date on the display</label><select id="dateFmt" class="needs" onchange="setting(5,this.value)">
<option value="0">Off</option><option value="1">24 Jan</option><option value="2">24 Tue</option><option value="3">Jan 24</option><option value="4">Tue 24</option></select></section>
<section class="card"><h2>Brightness</h2>
<div class="switch"><span>Automatic (light sensor)</span><input type="checkbox" class="needs" id="bAuto"></div>
<div id="manualBox"><label>Fixed brightness <b id="bManVal"></b></label><input type="range" class="needs" id="bMan" min="1" max="255"></div>
<div id="autoBox"><label>Minimum (darkest) <b id="bLowVal"></b></label><input type="range" class="needs" id="bLow" min="0" max="255">
<label>Maximum (brightest) <b id="bHighVal"></b></label><input type="range" class="needs" id="bHigh" min="0" max="255">
<div class="row"><button class="sec needs" onclick="act('/api/brightness/range',{low:30,high:220})">Restore 30–220 defaults</button></div>
<p class="note">Limits for the sensor-driven range, as in the original app. Changes are sent when you release a slider.</p></div></section>
</main>

<main class="tab" data-tab="clock">
<section class="card" id="pairCard"><h2>Pair a clock</h2>
<ol><li>Make sure no phone or Mac app is connected to the clock.</li>
<li><b>Short-press</b> the clock's button to activate pairing mode (do not hold it).</li>
<li>Press <b>Find clocks</b>, then <b>Pair</b> beside your clock.</li>
<li>Type the PIN the clock shows into the box that appears here.</li></ol>
<div class="row"><button id="bScan" onclick="act('/api/scan')">Find clocks</button><span class="note" id="scanNote"></span></div>
<div class="list" id="scanList" style="display:none"></div>
<div class="pinbox" id="pinBox" style="display:none"><b>Enter the PIN shown on the clock</b>
<div class="row"><input type="text" id="pin" inputmode="numeric" maxlength="6" autocomplete="one-time-code" style="flex:1"><button onclick="sendPin()">Send PIN</button></div></div>
<div class="pinbox" id="cfmBox" style="display:none"><b>Does the clock show <span id="cfmNum"></span>?</b>
<div class="row"><button onclick="act('/api/confirm',{accept:1})">Yes, it matches</button><button class="danger" onclick="act('/api/confirm',{accept:0})">No</button></div></div>
<details><summary>Forget the saved clock</summary><p class="note">Removes the pairing stored on this bridge only. It never resets the clock or clears the clock's own pairings.</p>
<button class="danger" onclick="if(confirm('Forget the saved clock on this bridge?'))act('/api/forget')">Forget clock</button></details></section>
<section class="card"><h2>Connection</h2>
<div class="switch"><span>Keep the clock connected automatically</span><input type="checkbox" id="oAuto" onchange="act('/api/options',{auto:this.checked?1:0})"></div>
<div class="switch"><span>Show “OpenGlance connected” (silent) after start-up, pairing or Connect</span><input type="checkbox" id="oAnnounce" onchange="act('/api/options',{announce:this.checked?1:0})"></div></section>
<section class="card"><h2>Hand calibration</h2>
<p class="note">State reported by the clock: <b id="hState">—</b></p>
<div class="warn">Calibration physically moves the hands. Confirm only when the clock has stopped, shows Confirm with a green segment, and <b>both hands point exactly to 12</b>.</div>
<div class="row"><button class="sec needs" id="bCalStart" onclick="if(confirm('Start calibration? The hands will move.'))act('/api/calibration/start')">Start calibration</button>
<button class="needs" id="bCalConfirm" onclick="if(confirm('Both hands point exactly to 12?'))act('/api/calibration/confirm')">Confirm hands at 12</button></div>
<details><summary>Hands stop short of 12 (e.g. 12:14)? Reseating guide</summary><ol>
<li>Only while the clock is stopped, showing Confirm and the green segment.</li>
<li>Pull the long minute hand straight off, then the short hour hand. Do not twist hands on the shaft.</li>
<li>Refit the hour hand pointing exactly at 12, then the minute hand at 12, keeping clearance from the fabric and between hands.</li>
<li>Never force anything. Then press Confirm hands at 12.</li></ol></details></section>
</main>

<main class="tab" data-tab="system">
<section class="card"><h2>Time &amp; date</h2>
<label for="tz">Time zone (daylight saving is automatic)</label><select id="tz" onchange="tzChanged()"></select>
<div class="row" id="tzcRow" style="display:none"><input type="text" id="tzc" placeholder="POSIX rule" style="flex:1"></div>
<div class="row"><button class="sec" onclick="saveTZ()">Save time zone</button></div>
<div class="switch"><span>Correct the clock daily at 03:05 and on daylight-saving changes</span><input type="checkbox" id="oDaily" onchange="act('/api/options',{daily:this.checked?1:0})"></div>
<details><summary>Set a manual date &amp; time</summary>
<p class="note">Pauses internet time until restart or until you return to internet time.</p>
<div class="row"><input type="date" id="mDate" style="flex:1"><input type="time" id="mTime" style="flex:1"></div>
<div class="row"><button class="sec" onclick="act('/api/time/manual',{date:$('#mDate').value,time:$('#mTime').value})">Use manual time</button>
<button class="sec" onclick="act('/api/time/internet')">Back to internet time</button></div></details></section>
<section class="card"><h2>Bridge firmware</h2>
<dl class="kv"><dt>Installed</dt><dd id="fwVer">—</dd><dt>Running for</dt><dd id="fwUp">—</dd><dt>Free memory</dt><dd id="fwHeap">—</dd></dl>
<label for="fwFile">Firmware file (OpenGlance Bridge … OTA.bin)</label><input type="file" id="fwFile" accept=".bin">
<progress id="fwProg" value="0" max="100" style="display:none;margin-top:8px"></progress>
<div class="row"><button id="bUpload" onclick="upload()">Install update</button><button class="sec" onclick="if(confirm('Restart the bridge?'))act('/api/reboot')">Restart bridge</button></div>
<p class="note">Only OpenGlance bridge images built for the ESP32-C3 are accepted. The clock reconnects after the restart.</p></section>
<section class="card"><h2>Network</h2>
<dl class="kv"><dt>Wi-Fi</dt><dd id="nSsid">—</dd><dt>Address</dt><dd id="nIp">—</dd><dt>Signal</dt><dd id="nRssi">—</dd></dl>
<details id="wifiDetails"><summary>Change Wi-Fi network</summary>
<div class="row"><select id="wSel" style="flex:1" onchange="pickChanged(W)"><option value="">Press Scan</option></select><button class="sec" id="wScan" onclick="scanInto(W)">Scan</button></div>
<p class="note" id="wNote"></p><input type="text" id="wOther" placeholder="Network name" style="display:none">
<div id="wPassWrap"><label>Wi-Fi password</label><input type="password" id="wPass"></div>
<div class="row"><button class="sec" onclick="const n=pickedSSID(W);if(!n)return toast('Choose a network',true);if(confirm('Restart and join '+n+'?'))act('/api/wifi',{ssid:n,pass:W.pass.value})">Save &amp; restart</button></div></details></section>
<section class="card wide"><h2>Activity</h2><div class="log" id="log"></div></section>
</main>
<div id="toast"></div>
<script>)HTML" OG_TZ_JS R"HTML(
const $=s=>document.querySelector(s);let S=null,zonesFilled=false,dragging=null,wxFilled=false;
const W={sel:$("#wSel"),btn:$("#wScan"),note:$("#wNote"),other:$("#wOther"),pass:$("#wPass"),passWrap:$("#wPassWrap")};
$("#wifiDetails").addEventListener("toggle",e=>{if(e.target.open&&W.sel.options.length<2)scanInto(W)});
function show(t){document.querySelectorAll(".tab").forEach(m=>m.classList.toggle("on",m.dataset.tab===t));
document.querySelectorAll("#tabs button").forEach(b=>b.classList.toggle("on",b.dataset.t===t));if(location.hash!=="#"+t)history.replaceState(null,"","#"+t);window.scrollTo(0,0)}
document.querySelectorAll("#tabs button").forEach(b=>b.onclick=()=>show(b.dataset.t));
function fromHash(){show((location.hash||"#home").slice(1).replace(/[^a-z]/g,"")||"home");if(!document.querySelector(".tab.on"))show("home")}
fromHash();window.addEventListener("hashchange",fromHash);
function toast(t,e){const x=$("#toast");x.textContent=t;x.className="show"+(e?" err":"");clearTimeout(x._t);x._t=setTimeout(()=>x.className="",4500)}
async function act(url,data){try{const r=await fetch(url,{method:"POST",body:new URLSearchParams(data||{})});const j=await r.json();
if(!j.ok)toast(j.error||"Request failed",true);else if(j.message)toast(j.message);setTimeout(refresh,300);return j}catch(e){toast("The bridge did not respond",true)}}
function wxSave(d){return act("/api/weather",d)}
const SOUNDS=["None","Waves","Rise","Charging","Steps","Radar","Bells","Bye","Hello","Flowers","Circles","Complete","Popcorn","Break","Opening","High","Shine","Extension"];
document.querySelectorAll("select.sounds").forEach(sel=>{SOUNDS.forEach((n,i)=>{if(i||sel.dataset.none)sel.add(new Option(n,i))})});
$("#msgSound").value="0";
document.querySelectorAll("select.hrs").forEach(sel=>{for(let h=0;h<24;h++)sel.add(new Option(String(h).padStart(2,"0")+":00",h))});
const DAYS=["Mon","Tue","Wed","Thu","Fri","Sat","Sun"];let alIndex=-1;
DAYS.forEach((d,i)=>{const l=document.createElement("label");l.style.cssText="display:flex;align-items:center;gap:4px;margin:0;color:var(--ink)";
const c=document.createElement("input");c.type="checkbox";c.dataset.bit=1<<i;l.append(c,document.createTextNode(d));$("#alDays").append(l)});
function alDaysText(d){if(!d)return"Once";if(d===127)return"Every day";if(d===31)return"Weekdays";if(d===96)return"Weekend";return DAYS.filter((x,i)=>d&(1<<i)).join(" ")}
function alSetDays(d){document.querySelectorAll("#alDays input").forEach(c=>c.checked=!!(d&c.dataset.bit))}
function alEdit(i){alIndex=i;const a=i>=0?S.clock.alarms[i]:{hour:7,minute:0,days:31,sound:6,text:""};
$("#alEdTitle").textContent=i>=0?"Edit alarm":"New alarm";$("#alTime").value=String(a.hour).padStart(2,"0")+":"+String(a.minute).padStart(2,"0");
alSetDays(a.days);$("#alSound").value=String(a.sound);$("#alText").value=a.text;$("#alEditor").style.display="block";$("#alEditor").scrollIntoView({behavior:"smooth"})}
async function alSave(){const t=$("#alTime").value.split(":");if(t.length<2)return toast("Choose a time",true);let d=0;document.querySelectorAll("#alDays input").forEach(c=>{if(c.checked)d|=+c.dataset.bit});
const en=alIndex>=0?(S.clock.alarms[alIndex].enabled?1:0):1;
const r=await act("/api/alarms/save",{index:alIndex,hour:+t[0],minute:+t[1],days:d,sound:$("#alSound").value,text:$("#alText").value,enabled:en});if(r&&r.ok)$("#alEditor").style.display="none"}
function renderAlarms(c){const L=$("#alList"),k=JSON.stringify(c.alarms);if(L._k!==k){L._k=k;L.innerHTML="";
if(!c.alarms.length)L.innerHTML="<p class='note'>No alarms saved on this bridge.</p>";
c.alarms.forEach((a,i)=>{const d=document.createElement("div");d.className="switch";
const left=document.createElement("span");left.innerHTML="<b class='big' style='font-size:20px'></b> <span class='note'></span>";
left.querySelector("b").textContent=String(a.hour).padStart(2,"0")+":"+String(a.minute).padStart(2,"0");
left.querySelector(".note").textContent=" "+alDaysText(a.days)+" · "+SOUNDS[a.sound]+(a.text?" · “"+a.text+"”":"");
const right=document.createElement("span");right.style.cssText="display:flex;gap:6px;align-items:center";
const en=document.createElement("input");en.type="checkbox";en.checked=a.enabled;en.title="On/off";en.onchange=()=>act("/api/alarms/enable",{index:i,enabled:en.checked?1:0});
const e=document.createElement("button");e.className="sec";e.textContent="Edit";e.onclick=()=>alEdit(i);
const x=document.createElement("button");x.className="danger";x.textContent="Delete";x.onclick=()=>{if(confirm("Delete this alarm?"))act("/api/alarms/delete",{index:i})};
right.append(en,e,x);d.append(left,right);L.append(d)})}
$("#alAdd").disabled=c.alarms.length>=4;
$("#alNote").textContent=c.alarmsPending?"Changes are waiting to be sent to the clock (it sends when the clock connects).":"The clock holds up to 4 alarms."+(c.alarms.length>=4?" Delete one to add another.":"")}
function testSound(id){const v=$(id).value;if(v==="0")return toast("Choose a sound to test",true);act("/api/sound/test",{sound:v})}
function setting(f,v){act("/api/setting",{field:f,value:v})}
function quick(m){$("#tmMin").value=m;act("/api/timer",{minutes:m})}
function sendPin(){act("/api/pin",{pin:$("#pin").value.trim()});$("#pin").value=""}
function tzChanged(){$("#tzcRow").style.display=$("#tz").value==="custom"?"flex":"none"}
function saveTZ(){const s=$("#tz");const tz=s.value==="custom"?$("#tzc").value.trim():s.value;act("/api/timezone",{tz,tzName:s.value==="custom"?tz:s.options[s.selectedIndex].text})}
function pill(el,cls,text){el.className="pill "+cls;el.lastElementChild.textContent=text}
function fmtDur(s){if(s<=0)return"—";const h=Math.floor(s/3600),m=Math.floor(s%3600/60),x=s%60;return(h?h+":"+String(m).padStart(2,"0"):m)+":"+String(x).padStart(2,"0")}
function palette(t){const s=[0x2060ff,0x00c070,0xffc000,0xff3000];if(t<=0)return mix(s[0],s[0],0);if(t>=1)return mix(s[3],s[3],0);const x=t*3,i=Math.floor(x);return mix(s[i],s[i+1],x-i)}
function mix(a,b,t){t=Math.max(0,Math.min(1,t));let o="#";for(const sh of[16,8,0]){const x=(a>>sh)&255,y=(b>>sh)&255;o+=Math.round(x+(y-x)*t).toString(16).padStart(2,"0")}return o}
document.querySelectorAll("input[data-f]").forEach(cb=>cb.onchange=()=>setting(cb.dataset.f,cb.checked?1:0));
$("#bAuto").onchange=e=>{if(e.target.checked)act("/api/brightness/range",{low:+$("#bLow").value||30,high:+$("#bHigh").value||220});else setting(10,+$("#bMan").value||128)};
for(const id of["bMan","bLow","bHigh","wMm","wPct"]){const el=$("#"+id);el.onpointerdown=()=>dragging=id;
el.oninput=()=>{if(id==="bLow"&&+el.value>+$("#bHigh").value)$("#bHigh").value=el.value;if(id==="bHigh"&&+el.value<+$("#bLow").value)$("#bLow").value=el.value;labels()};
el.onchange=()=>{dragging=null;if(id==="bMan")setting(10,el.value);else if(id==="wMm")wxSave({rainMm:el.value});else if(id==="wPct")wxSave({rainChance:el.value});else act("/api/brightness/range",{low:$("#bLow").value,high:$("#bHigh").value})}}
function labels(){$("#bManVal").textContent=$("#bMan").value;$("#bLowVal").textContent=$("#bLow").value;$("#bHighVal").textContent=$("#bHigh").value;$("#wMmVal").textContent=(+$("#wMm").value).toFixed(1);$("#wPctVal").textContent=$("#wPct").value}
async function wxSearch(){const q=$("#wQuery").value.trim();if(q.length<2)return toast("Type a town or suburb",true);const L=$("#wResults");L.style.display="block";L.innerHTML="<div class='item'>Searching…</div>";
try{const j=await(await fetch("https://geocoding-api.open-meteo.com/v1/search?count=8&language=en&name="+encodeURIComponent(q))).json();L.innerHTML="";
if(!j.results||!j.results.length){L.innerHTML="<div class='item'>No places found</div>";return}
for(const p of j.results){const name=[p.name,p.admin1,p.country].filter(Boolean).join(", ");const d=document.createElement("div");d.className="item";
const s=document.createElement("span");s.textContent=name;const b=document.createElement("button");b.className="sec";b.textContent="Use";
b.onclick=async()=>{const r=await wxSave({lat:p.latitude,lon:p.longitude,place:name,on:1});if(r&&r.ok){L.style.display="none";$("#wQuery").value=""}};d.append(s,b);L.append(d)}}
catch(e){L.innerHTML="<div class='item'>Search needs internet access from this browser</div>"}}
function bars(el,hoursEl,items,valueOf,colourOf,labelOf,maxV,minV){el.innerHTML="";hoursEl.innerHTML="";
for(const it of items){const v=valueOf(it);const d=document.createElement("div");const f=maxV>minV?(v-minV)/(maxV-minV):0;
d.style.height=Math.max(3,Math.round(8+f*72))+"%";d.style.background=colourOf(it);const sp=document.createElement("span");sp.textContent=labelOf(it);d.append(sp);el.append(d);
const h=document.createElement("div");h.textContent=it.h;hoursEl.append(h)}}
const STATE={not_paired:["No clock paired","mid"],waiting:["Not connected","mid"],scanning:["Searching…","mid"],connecting:["Connecting…","mid"],securing:["Pairing…","mid"],
awaiting_pin:["Enter the PIN","mid"],awaiting_confirm:["Confirm number","mid"],connected:["Connected","good"],error:["Needs attention","bad"]};
function logInto(el,items){const k=JSON.stringify(items);if(el._k===k)return;el._k=k;el.innerHTML="";for(const e of items){const d=document.createElement("div");if(e.e)d.className="e";
const t=document.createElement("span");t.textContent=e.t;d.append(t,document.createTextNode(e.m));el.append(d)}}
function renderWeather(s){const w=s.weather,unit="°"+w.unit;
if(!dragging){$("#wMm").value=w.rainMm;$("#wPct").value=w.rainChance;labels()}
for(const[id,v]of[["#wOn",w.on],["#wTemp",w.temp],["#wText",w.text]])if(document.activeElement!==$(id))$(id).checked=v;
for(const[id,v]of[["#wRainMode",w.rainMode],["#wCycle",w.cycle],["#wHours",w.rainHours]])if(document.activeElement!==$(id))$(id).value=String(v);
$("#wCycleRow").style.display=w.rainMode==1&&w.temp?"block":"none";$("#wRainRules").style.display=w.rainMode!=0?"block":"none";
$("#wTemp").disabled=$("#wText").disabled=!w.on;$("#wPlace").textContent=w.place||"not set";
if(!wxFilled){wxFilled=true;$("#wUnit").value=w.unit;$("#wColdAt").value=w.coldAt;$("#wHotAt").value=w.hotAt}
$("#wStatus").textContent=!w.on?"Weather rings are off.":w.error?w.error:(w.fetched?"Forecast updated "+w.fetched+(w.sent?" · sent to clock "+w.sent:""):"Waiting for the first forecast…");
$("#wPlaceLine").textContent=(w.place||"No location")+(w.current!==null?" · now "+Math.round(w.current)+unit:"");
const span=Math.max(1,w.hotAt-w.coldAt);
if(w.hours.length){const ts=w.hours.map(h=>h.t),lo=Math.min(...ts),hi=Math.max(...ts);
bars($("#wTBars"),$("#wTHours"),w.hours,h=>h.t,h=>palette((h.t-w.coldAt)/span),h=>Math.round(h.t)+"°",hi,lo-1);
const shown=w.hours.map(h=>h.r>=w.rainMm-1e-6&&h.c>=w.rainChance?Math.max(10,h.c):0);const any=shown.slice(0,w.rainHours).some(v=>v>0);
bars($("#wRBars"),$("#wRHours"),w.hours.map((h,i)=>({...h,v:shown[i]})),h=>h.v,h=>h.v?mix(0x7fd4ff,0x0030ff,h.v/100):"var(--line)",h=>h.v?h.v+"%":"",100,0);
const rule=(+w.rainMm).toFixed(1)+" mm and "+w.rainChance+"% within "+w.rainHours+" h";
$("#wRainLine").textContent=w.rainMode==0?"Rain ring is off":any?"Rain coming ("+rule+") — rain ring in use":"No rain coming ("+rule+") — temperature only"}
else{$("#wTBars").innerHTML=$("#wTHours").innerHTML=$("#wRBars").innerHTML=$("#wRHours").innerHTML="";$("#wRainLine").textContent="Rain"}
$("#wHomeNow").textContent=w.on&&w.current!==null?Math.round(w.current)+unit:"—";
$("#wHomeText").textContent=!w.on?"Weather ring is off":(w.place||"")+" · ring: "+(w.showing==0?"nothing":w.rainDue?(w.rainMode==1&&w.temp?"temperature/rain alternating":"rain"):"temperature")}
function render(s){S=s;const c=s.clock,st=STATE[c.state]||["…","mid"];
pill($("#hClock"),st[1],"Clock: "+st[0]);pill($("#hTime"),s.time.valid?(s.time.source==="manual"?"mid":"good"):"bad",s.time.valid?(s.time.source==="manual"?"Manual time":"Internet time"):"No time yet");
pill($("#hWifi"),s.wifi.connected?"good":"bad",s.wifi.connected?"Wi-Fi "+s.wifi.ip:(s.wifi.ap?"Setup hotspot":"Wi-Fi offline"));
$("#cState").textContent=st[0];$("#cText").textContent=c.text;$("#cName").textContent=c.name||"—";
$("#cBat").textContent=c.battery>=0?c.battery+"%":"—";$("#cRead").textContent=c.lastRead||"Not yet on this connection";$("#cSupplied").textContent=c.supplied||"—";
const ready=c.connected&&!c.busy;document.querySelectorAll(".needs").forEach(e=>e.disabled=!ready);
$("#bSync").disabled=!ready||!s.time.valid;$("#bConnect").disabled=c.connected||!c.paired||c.busy;$("#bDisconnect").disabled=!c.connected;$("#bRefresh").disabled=!ready;
for(const[id,v]of[["#oAuto",c.auto],["#oAnnounce",c.announce],["#oDaily",s.time.daily],["#chOn",c.chime.on]])if(document.activeElement!==$(id))$(id).checked=v;
if(document.activeElement!==$("#chSound"))$("#chSound").value=String(c.chime.sound);
if(!render._q){render._q=1;$("#chFrom").value=String(c.chime.quietFrom);$("#chTo").value=String(c.chime.quietTo)}
$("#pairCta").style.display=c.paired?"none":"block";
const pairing=c.state==="awaiting_pin"||c.state==="awaiting_confirm";
if(pairing&&!render._p){show("clock");$("#pairCard").scrollIntoView({behavior:"smooth"});if(c.state==="awaiting_pin")$("#pin").focus()}render._p=pairing;
$("#pinBox").style.display=c.state==="awaiting_pin"?"block":"none";$("#cfmBox").style.display=c.state==="awaiting_confirm"?"block":"none";$("#cfmNum").textContent=String(c.confirm).padStart(6,"0");
$("#bScan").disabled=s.scan.running||c.busy;$("#scanNote").textContent=s.scan.running?"Searching for 8 seconds…":"";
const L=$("#scanList");L.style.display=s.scan.items.length?"block":"none";const key=JSON.stringify(s.scan.items)+c.busy;
if(L._k!==key){L._k=key;L.innerHTML="";for(const it of s.scan.items){const d=document.createElement("div");d.className="item";
d.innerHTML="<span><b></b><br><span class='note'></span></span>";d.querySelector("b").textContent=it.name;d.querySelector(".note").textContent=it.addr+" · "+it.rssi+" dBm";
const b=document.createElement("button");b.textContent="Pair";b.disabled=c.busy;b.onclick=()=>act("/api/pair",{i:it.i});d.append(b);L.append(d)}}
$("#tNow").textContent=s.time.valid?s.time.local:"Waiting for internet time…";
$("#tSrc").textContent=(s.time.source==="manual"?"Manual time":"Internet time")+(s.time.lastNtp?" · last internet sync "+s.time.lastNtp:"")+" · "+s.time.tzName;
if(!zonesFilled){zonesFilled=true;const found=fillZones($("#tz"),s.time.tz);if(!found)$("#tzc").value=s.time.tz;tzChanged();
const now=new Date();$("#mDate").value=now.toISOString().slice(0,10);$("#mTime").value=now.toTimeString().slice(0,5)}
$("#tmLeft").textContent=fmtDur(c.timerRemaining);
const g=c.settings;if(g){document.querySelectorAll("input[data-f]").forEach(cb=>{if(document.activeElement!==cb)cb.checked={11:g.digitalTime,12:g.twelveHour,9:g.hourPoints,2:g.nightMode,4:g.muted}[cb.dataset.f]});
if(document.activeElement!==$("#dateFmt"))$("#dateFmt").value=g.dateFormat;const auto=g.brightness===0;$("#bAuto").checked=auto;
$("#manualBox").style.display=auto?"none":"block";$("#autoBox").style.display=auto?"block":"none";
if(!dragging){$("#bMan").value=g.brightness||128;$("#bLow").value=g.autoLow;$("#bHigh").value=g.autoHigh;labels()}}
const h=c.hand;$("#hState").textContent=!h.valid?"not reported":h.motorFailure?"motor failure":h.homingFailure?"homing failure":h.homing?"hands moving…":h.awaiting?"waiting for confirmation":"idle";
$("#bCalStart").disabled=!ready||h.homing;$("#bCalConfirm").disabled=!ready||h.homing||!(h.awaiting||c.calibrationRequested);
$("#fwVer").textContent=s.fw;$("#fwUp").textContent=fmtDur(s.uptime);$("#fwHeap").textContent=Math.round(s.heap/1024)+" KB";
$("#nSsid").textContent=s.wifi.ssid||"—";$("#nIp").textContent=s.wifi.connected?s.wifi.ip+" · openglance.local":"—";$("#nRssi").textContent=s.wifi.connected?s.wifi.rssi+" dBm":"—";
if(s.weather)renderWeather(s);if(c.alarms)renderAlarms(c);logInto($("#log"),s.log);logInto($("#logShort"),s.log.slice(0,6))}
async function refresh(){try{const r=await fetch("/api/status",{cache:"no-store"});if(r.ok)render(await r.json())}catch(e){pill($("#hWifi"),"bad","Bridge offline")}}
function upload(){const f=$("#fwFile").files[0];if(!f)return toast("Choose the OTA .bin file first",true);
if(!confirm("Install "+f.name+"? The bridge restarts afterwards."))return;const fd=new FormData();fd.append("firmware",f,f.name);
const x=new XMLHttpRequest();const p=$("#fwProg");p.style.display="block";p.value=0;$("#bUpload").disabled=true;
x.upload.onprogress=e=>{if(e.lengthComputable)p.value=e.loaded/e.total*100};
x.onload=()=>{$("#bUpload").disabled=false;let j={};try{j=JSON.parse(x.responseText)}catch(e){}
if(j.ok){toast("Installed "+(j.version||"")+". Restarting…");setTimeout(()=>location.reload(),9000)}else{toast(j.error||"Update failed",true);p.style.display="none"}};
x.onerror=()=>{$("#bUpload").disabled=false;toast("Upload failed",true)};x.open("POST","/update");x.send(fd)}
refresh();setInterval(refresh,1500);
</script></body></html>)HTML";
