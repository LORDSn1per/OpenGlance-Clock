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
.night h3{font-size:14px;margin:12px 0 4px}.night .row{align-items:center}.night select{width:auto}.night .hrs{display:inline-flex;gap:6px;align-items:center;flex-wrap:nowrap}
.leds{display:flex;align-items:center;gap:8px;margin:6px 0 2px}.leds .end{font-size:12px;color:var(--muted)}
.led{width:30px;height:30px;padding:0;border-radius:50%;border:2px solid var(--line);background:var(--bg);color:var(--muted);font-size:11px;font-weight:700}
.led:hover{background:var(--tealbg)}.led[aria-pressed="true"]{background:var(--teal);border-color:var(--teal);color:#fff}
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
.timeChoices{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px;margin:14px 0}
.timeChoice{border:1px solid var(--line);border-radius:11px;background:var(--bg);padding:14px}
.timeChoice h3{font-size:14px;margin:0 0 6px}.timeChoice .row>div{flex:1;min-width:120px}
</style></head><body>
<header>)HTML" OG_LOGO R"HTML(<h1>OpenGlance</h1><span class="sp"></span>
<span class="pill" id="hClock"><span class="dot"></span><span>…</span></span>
<span class="pill" id="hTime"><span class="dot"></span><span>…</span></span>
<span class="pill" id="hWifi"><span class="dot"></span><span>…</span></span></header>
<nav class="tabs" id="tabs">
<button data-t="home">Home</button><button data-t="alerts">Alerts</button><button data-t="alarms">Alarms</button><button data-t="weather">Weather</button>
<button data-t="display">Display</button><button data-t="clock">Clock</button><button data-t="system">System</button></nav>

<main class="tab" data-tab="home">
<section class="card wide" id="pairCta" style="display:none"><div class="cta"><div><h2 style="margin:0 0 4px">Pair your clock</h2>
<p class="note" style="margin:0">No clock is paired with this bridge yet.</p></div><button onclick="show('clock')">Pair a clock</button></div></section>
<section class="card wide" id="timeCard"><div class="cta"><div><h2>Time &amp; date</h2>
<div class="big" id="tNow">—</div></div><span class="pill" id="tMode"><span class="dot"></span><span>…</span></span></div>
<p class="note" id="tSrc"></p>
<div class="timeChoices"><div class="timeChoice"><h3>Internet time</h3>
<p class="note">Keep the bridge on the correct time automatically.</p>
<button id="bInternet" onclick="act('/api/time/internet')">Use internet time</button>
<p class="note" id="internetState"></p></div>
<div class="timeChoice"><h3>Manual time</h3><p class="note">Choose a date and time, then apply it. Internet time pauses until you switch back or restart.</p>
<div class="row"><div><label for="mDate">Date</label><input type="date" id="mDate" required></div>
<div><label for="mTime">Time</label><input type="time" id="mTime" required></div></div>
<button class="sec" id="bManual" onclick="manualTime()">Apply manual time</button></div></div>
<div class="row"><button class="sec" id="bSync" onclick="act('/api/sync')">Update clock now</button>
<span class="note">The time above is bridge time. The clock reads it when it connects.</span></div></section>
<section class="card"><h2>Clock</h2>
<div class="big" id="cState">…</div><p class="note" id="cText"></p>
<dl class="kv"><dt>Clock</dt><dd id="cName">—</dd><dt>Battery</dt><dd id="cBat">—</dd>
<dt>Last time read</dt><dd id="cRead">—</dd><dt>Time supplied</dt><dd id="cSupplied">—</dd></dl>
<button class="linkish" onclick="show('system')">Connection settings →</button></section>
<section class="card"><h2>Time zone</h2>
<label for="tz">Time zone (daylight saving is automatic)</label><select id="tz" onchange="tzChanged()"></select>
<div class="row" id="tzcRow" style="display:none"><input type="text" id="tzc" placeholder="POSIX rule" style="flex:1"></div>
<div class="row"><button class="sec" onclick="saveTZ()">Save time zone</button></div>
<div class="switch"><span>Correct the clock daily at 03:05 and on daylight-saving changes</span><input type="checkbox" id="oDaily" onchange="act('/api/options',{daily:this.checked?1:0})"></div></section>
<section class="card"><h2>Weather</h2><div class="big" id="wHomeNow">—</div><p class="note" id="wHomeText"></p>
<button class="linkish" onclick="show('weather')">Weather settings →</button></section>
<section class="card wide"><h2>Recent activity</h2><div class="log" id="logShort"></div></section>
</main>

<main class="tab" data-tab="alerts">
<section class="card"><h2>Send an alert</h2>
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
<section class="card"><h2>Bridge hourly chime</h2>
<div class="switch"><span>Chime every hour (shows the time with a sound)</span><input type="checkbox" id="chOn" onchange="act('/api/chime',{on:this.checked?1:0})"></div>
<label for="chSound">Sound</label><div class="row" style="margin-top:0"><select id="chSound" class="sounds" style="flex:1" onchange="act('/api/chime',{sound:this.value})"></select>
<button class="sec needs" onclick="testSound('#chSound')">Test</button></div>
<label>Quiet hours (no chime)</label><div class="row" style="margin-top:0"><span class="note">from</span><select id="chFrom" class="hrs" style="flex:1"></select>
<span class="note">to</span><select id="chTo" class="hrs" style="flex:1"></select></div>
<div class="row"><button class="sec" onclick="act('/api/chime',{quietFrom:$('#chFrom').value,quietTo:$('#chTo').value})">Save quiet hours</button></div>
<p class="note">Set both to the same hour to chime around the clock. The clock also mutes sounds in its own night mode.</p></section>
<section class="card"><h2>Clock’s built-in hourly chime</h2>
<p class="note">A separate sound saved in the clock. It can play even when the bridge or desktop app is off. The quiet hours above do not control it.</p>
<label for="clockChime">Saved sound</label><select id="clockChime" class="sounds needs" data-none="1" onchange="act('/api/setting',{field:15,value:this.value})"></select>
<p class="note" id="clockChimeState">Connect the clock to read its saved sound.</p>
<div class="row"><button class="sec needs" onclick="act('/api/refresh',{})">Read clock settings</button></div>
<p class="note">Choose Off to stop only this chime. Alarms, timers and messages are unchanged. The value is read back after saving. Leave this Off when using the bridge chime to avoid two sounds.</p></section>
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
<section class="card night"><h2>Night settings</h2>
<p class="note">Stored on the clock. <b>Do Not Disturb</b> hides the seconds, the weather ring and notifications. <b>Silent</b> only mutes sounds, so the seconds stay visible.</p>
<h3>Do Not Disturb</h3>
<div class="row"><select id="dndMode" class="needs" onchange="nightEdit('dnd')" aria-label="Do Not Disturb mode"><option value="0">Off</option><option value="1">Scheduled</option><option value="2">Always</option></select>
<span id="dndHours" class="hrs">from <select id="dndFrom" class="hours needs" onchange="nightEdit('dnd')" aria-label="Do Not Disturb starts"></select> to <select id="dndTill" class="hours needs" onchange="nightEdit('dnd')" aria-label="Do Not Disturb ends"></select></span></div>
<div class="row"><button class="sec needs" id="dndSave" onclick="saveNight('dnd')">Save Do Not Disturb</button></div>
<p class="note" id="dndRead" role="status">Read from clock: waiting…</p>
<h3>Silent (quiet hours)</h3>
<div class="row"><select id="silentMode" class="needs" onchange="nightEdit('silent')" aria-label="Silent mode"><option value="0">Off</option><option value="1">Scheduled</option><option value="2">Always</option></select>
<span id="silentHours" class="hrs">from <select id="silentFrom" class="hours needs" onchange="nightEdit('silent')" aria-label="Silent starts"></select> to <select id="silentTill" class="hours needs" onchange="nightEdit('silent')" aria-label="Silent ends"></select></span></div>
<div class="row"><button class="sec needs" id="silentSave" onclick="saveNight('silent')">Save Silent</button></div>
<p class="note" id="silentRead" role="status">Read from clock: waiting…</p>
<p class="note">Silent "Always" is the same as <b>Mute sounds</b> above. Timers and alarms may still sound while muted. For quiet nights with the seconds visible, turn Do Not Disturb off and schedule Silent for the same hours.</p></section>
<section class="card"><h2>Comet seconds hand <span class="pill mid">Experimental</span></h2>
<div class="switch"><span>Show a ticking seconds pixel</span><input type="checkbox" id="cometOn" onchange="saveComet()"></div>
<p class="note">Seconds pause while the clock's Do Not Disturb is active.</p>
<p class="note" id="cometDndState" role="status">Clock Do Not Disturb: checking…</p>
<div class="row" style="gap:20px">
<svg id="cometPreview" viewBox="0 0 160 160" width="140" height="140" role="img" aria-label="Illustrative comet preview, not clock telemetry" style="flex:none">
<circle cx="80" cy="80" r="64" fill="none" stroke="var(--line)" stroke-width="2"/>
<path d="M80 80L80 43M80 80L105 94" stroke="var(--muted)" stroke-width="3" stroke-linecap="round"/>
<circle cx="80" cy="80" r="4" fill="var(--ink)"/><g id="cometDots"></g></svg>
<div style="flex:1;min-width:150px"><label for="cometColor">Comet colour</label>
<select id="cometColor" onchange="saveComet()"></select>
<label style="margin-top:10px">LEDs that light</label>
<div class="leds" id="cometRings" role="group" aria-label="Seconds LEDs, from the outer ring to the centre"><span class="end">Outer</span>
<button type="button" class="led" data-bit="1" aria-pressed="true" aria-label="Outer ring LED" onclick="toggleRing(1)">1</button>
<button type="button" class="led" data-bit="2" aria-pressed="false" aria-label="Second LED" onclick="toggleRing(2)">2</button>
<button type="button" class="led" data-bit="4" aria-pressed="false" aria-label="Third LED" onclick="toggleRing(4)">3</button>
<button type="button" class="led" data-bit="8" aria-pressed="false" aria-label="LED nearest the centre" onclick="toggleRing(8)">4</button><span class="end">Centre</span></div>
<p class="note" id="cometRingsNote">1 LED · new clock scene about every 10 s</p>
<p class="note">One lap per minute · one step every 1.25 s<br>Illustrative preview</p></div></div>
<p id="cometStatus" class="note" role="status">Waiting for bridge status…</p>
<div class="row"><button class="sec" id="cometRetry" onclick="saveComet()">Retry / apply</button></div>
<p class="note">Uses the ring instead of weather while enabled. Turn it off to restore weather; your weather settings are kept. Runs on the bridge, even with this page closed.</p>
<p class="note">Pauses for timers, alerts and calibration. Messages get a 90-second pause. Uses the clock's existing brightness and night-mode settings; this first version needs a physical smoothness/night-mode check.</p>
</section></main>

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
<section class="card"><h2>Connection</h2>
<div class="row"><button id="bConnect" onclick="act('/api/connect')">Connect clock</button>
<button class="sec" id="bDisconnect" onclick="act('/api/disconnect')">Disconnect</button>
<button class="sec" id="bRefresh" onclick="act('/api/refresh')">Read clock settings</button></div>
<div class="switch"><span>Keep the clock connected automatically</span><input type="checkbox" id="oAuto" onchange="act('/api/options',{auto:this.checked?1:0})"></div>
<div class="switch"><span>Show “OpenGlance connected” (silent) after start-up, pairing or Connect</span><input type="checkbox" id="oAnnounce" onchange="act('/api/options',{announce:this.checked?1:0})"></div>
<p class="note" id="pollStatus">Status updates use cached bridge data, not Bluetooth reads.</p></section>
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
let pollTimer=0,refreshBusy=false,refreshAgain=false,refreshFailed=false,timeAnchor=null;
let pendingComet=null;
// Busy describes one serial BLE operation, not an unusable connection. Commands
// are queued safely by the bridge; cached busy must never grey all controls.
function controlsReady(c){return !!c.connected}
const W={sel:$("#wSel"),btn:$("#wScan"),note:$("#wNote"),other:$("#wOther"),pass:$("#wPass"),passWrap:$("#wPassWrap")};
$("#wifiDetails").addEventListener("toggle",e=>{if(e.target.open&&W.sel.options.length<2)scanInto(W)});
function show(t){if(t==="message")t="alerts";document.querySelectorAll(".tab").forEach(m=>m.classList.toggle("on",m.dataset.tab===t));
document.querySelectorAll("#tabs button").forEach(b=>b.classList.toggle("on",b.dataset.t===t));if(location.hash!=="#"+t)history.replaceState(null,"","#"+t);window.scrollTo(0,0)}
document.querySelectorAll("#tabs button").forEach(b=>b.onclick=()=>show(b.dataset.t));
function fromHash(){show((location.hash||"#home").slice(1).replace(/[^a-z]/g,"")||"home");if(!document.querySelector(".tab.on"))show("home")}
fromHash();window.addEventListener("hashchange",fromHash);
function toast(t,e){const x=$("#toast");x.textContent=t;x.className="show"+(e?" err":"");clearTimeout(x._t);x._t=setTimeout(()=>x.className="",4500)}
async function act(url,data){try{const r=await fetch(url,{method:"POST",body:new URLSearchParams(data||{})});const j=await r.json();
if(!j.ok)toast(j.error||"Request failed",true);else if(j.message)toast(j.message);setTimeout(refresh,300);return j}catch(e){toast("The bridge did not respond",true)}}
function manualTime(){if(!$("#mDate").reportValidity()||!$("#mTime").reportValidity())return;
return act('/api/time/manual',{date:$("#mDate").value,time:$("#mTime").value})}
function wxSave(d){return act("/api/weather",d)}
const nightDraft={dnd:0,silent:0};
document.querySelectorAll("select.hours").forEach(sel=>{for(let h=0;h<24;h++)sel.add(new Option(String(h).padStart(2,"0")+":00",String(h)))});
function nightShowHours(w){$("#"+w+"Hours").style.display=$("#"+w+"Mode").value==="1"?"":"none"}
function nightEdit(w){nightDraft[w]=performance.now()+60000;nightShowHours(w)}
function nightText(x){return !x?"not read yet":!x.known?"unreadable — not editable":x.mode===2?"always on":x.mode===1?"scheduled "+String(x.from).padStart(2,"0")+":00–"+String(x.till).padStart(2,"0")+":00":"off"}
async function saveNight(w){const mode=Number($("#"+w+"Mode").value),from=Number($("#"+w+"From").value),till=Number($("#"+w+"Till").value);
if(mode===1&&from===till)return toast("Start and end hours must differ",true);
nightDraft[w]=performance.now()+10000;const r=await act("/api/night",{which:w,mode,from,till});if(!r?.ok)nightDraft[w]=0;return r}
function nightRefresh(g,ready){for(const w of ["dnd","silent"]){const x=g?.[w];$("#"+w+"Read").textContent="Read from clock: "+nightText(x);
const ok=ready&&x&&x.known;["Mode","From","Till","Save"].forEach(k=>$("#"+w+k).disabled=!ok);
if(x&&x.known&&performance.now()>=nightDraft[w]){nightDraft[w]=0;$("#"+w+"Mode").value=String(x.mode);$("#"+w+"From").value=String(x.from);$("#"+w+"Till").value=String(x.till)}
nightShowHours(w)}}
let cometRings=1,cometSceneSteps=[8,8,8,8,6,8,8,8,6,6,6,8,6,8,8];
function ringGap(m){return /10+1/.test(m.toString(2))}
function renderRings(){document.querySelectorAll("#cometRings .led").forEach(b=>b.setAttribute("aria-pressed",cometRings&Number(b.dataset.bit)?"true":"false"));
const n=[1,2,4,8].filter(b=>cometRings&b).length,secs=(cometSceneSteps[cometRings-1]||8)*1.25;
$("#cometRingsNote").textContent=n+(n===1?" LED":" LEDs")+" · new clock scene about every "+(Number.isInteger(secs)?secs:secs.toFixed(1))+" s"+(ringGap(cometRings)?" (a gap needs more data per tick)":"")}
function toggleRing(bit){const next=cometRings^bit;if(!next)return toast("Keep at least one LED lit",true);cometRings=next;renderRings();saveComet()}
async function saveComet(){const data={on:$("#cometOn").checked?1:0,color:$("#cometColor").value,rings:cometRings};
const draft={...data,until:performance.now()+10000};pendingComet=draft;
const result=await act("/api/comet",data);if(!result?.ok&&pendingComet===draft)pendingComet=null;return result}
function cometDraftWaiting(c){if(!pendingComet)return false;
const matched=c&&Number(c.on)===pendingComet.on&&Number(c.color)===Number(pendingComet.color)&&Number(c.rings||1)===Number(pendingComet.rings||1);
if(matched||performance.now()>=pendingComet.until){pendingComet=null;return false}return true}
const COMET_COLORS=[[1,"Gold","#b8860b"],[2,"Orange","#ff8c00"],[3,"Olive","#808000"],[4,"Orange red","#ff4500"],[5,"Red","#ff3030"],
[6,"Maroon","#800000"],[7,"Magenta","#8b008b"],[8,"Pink","#c71585"],[9,"Brown","#a52a2a"],[10,"Indigo","#4b0082"],[11,"Violet","#8a2be2"],
[12,"White","#f1f7fa"],[13,"Slate blue","#8470ff"],[14,"Royal blue","#4169e1"],[15,"Blue","#2463ff"],[16,"Cornflower blue","#6495ed"],
[17,"Sky blue","#87ceeb"],[18,"Turquoise","#40e0d0"],[19,"Aqua","#00ffff"],[20,"Spring green","#00fa9a"],[21,"Lime green","#32cd32"],
[22,"Dark green","#006400"],[23,"Lime","#00ff00"],[24,"Lawn green","#7cfc00"]];
COMET_COLORS.forEach(([id,n])=>$("#cometColor").add(new Option(n,id)));$("#cometColor").value="18";
const cometDots=[];[64,56,48,40].forEach((r,ring)=>{for(let i=0;i<48;i++){const d=document.createElementNS("http://www.w3.org/2000/svg","circle"),a=i*Math.PI/24-Math.PI/2;
d.setAttribute("cx",80+r*Math.cos(a));d.setAttribute("cy",80+r*Math.sin(a));d.setAttribute("r","2.6");$("#cometDots").append(d);cometDots.push({d,i,ring})}});
function animateComet(){if(document.hidden||!document.querySelector('[data-tab="display"].on')){requestAnimationFrame(animateComet);return}
const phase=(Date.now()%60000)/1250,col=COMET_COLORS.find(x=>String(x[0])===$("#cometColor").value)?.[2]||"#40e0d0";
const head=Math.floor(phase);cometDots.forEach(({d,i,ring})=>{d.setAttribute("fill",col);d.setAttribute("opacity",i===head&&(cometRings>>ring&1)?"1":"0.08")});
requestAnimationFrame(animateComet)}requestAnimationFrame(animateComet);
const SOUNDS=["None","Waves","Rise","Charging","Steps","Radar","Bells","Bye","Hello","Flowers","Circles","Complete","Popcorn","Break","Opening","High","Shine","Extension"];
document.querySelectorAll("select.sounds").forEach(sel=>{SOUNDS.forEach((n,i)=>{if(i||sel.dataset.none)sel.add(new Option(n,i))})});
$("#clockChime").options[0].text="Off";
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
const ready=controlsReady(c);document.querySelectorAll(".needs").forEach(e=>e.disabled=!ready);
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
timeAnchor=s.time.valid?{wall:Date.parse(s.time.local.replace(" ","T")+"Z"),at:performance.now()}:null;
pill($("#tMode"),s.time.source==="manual"?"mid":s.time.valid?"good":"mid",s.time.source==="manual"?"Active: manual time":s.time.valid?"Active: internet time":"Internet time · waiting");
$("#internetState").textContent=s.time.source==="manual"?"Internet time is paused. Switch back here at any time.":s.time.valid?"Internet time is active.":"Waiting for internet time…";
$("#tSrc").textContent=(s.time.source==="manual"?"Manual time":"Internet time")+(s.time.lastNtp?" · last internet sync "+s.time.lastNtp:"")+" · "+s.time.tzName;
if(!zonesFilled){zonesFilled=true;const found=fillZones($("#tz"),s.time.tz);if(!found)$("#tzc").value=s.time.tz;tzChanged();
const now=new Date(),date=s.time.valid?s.time.local.slice(0,10):[now.getFullYear(),String(now.getMonth()+1).padStart(2,"0"),String(now.getDate()).padStart(2,"0")].join("-");
$("#mDate").value=date;$("#mTime").value=s.time.valid?s.time.local.slice(11,16):now.toTimeString().slice(0,5)}
$("#tmLeft").textContent=fmtDur(c.timerRemaining);
const g=c.settings;if(g){document.querySelectorAll("input[data-f]").forEach(cb=>{if(document.activeElement!==cb)cb.checked={11:g.digitalTime,12:g.twelveHour,9:g.hourPoints,2:g.nightMode,4:g.muted}[cb.dataset.f]});
if(document.activeElement!==$("#dateFmt"))$("#dateFmt").value=g.dateFormat;const auto=g.brightness===0;$("#bAuto").checked=auto;
$("#manualBox").style.display=auto?"none":"block";$("#autoBox").style.display=auto?"block":"none";
if(!dragging){$("#bMan").value=g.brightness||128;$("#bLow").value=g.autoLow;$("#bHigh").value=g.autoHigh;labels()}}
nightRefresh(g,ready);
const cs=$("#clockChime"),cv=g?.builtInChimeSound;cs.disabled=!ready||cv===undefined;
if(document.activeElement!==cs){cs.querySelectorAll("option[data-unknown]").forEach(o=>o.remove());
if(cv!==undefined&&!(cv>=0&&cv<SOUNDS.length)){const o=new Option("Unknown sound ("+cv+")",String(cv));o.dataset.unknown="1";o.disabled=true;cs.add(o)}
cs.value=cv===undefined?"":String(cv)}
$("#clockChimeState").textContent=cv===undefined?"Saved sound not read yet.":"Read from clock: "+(cv===0?"Off":SOUNDS[cv]||"Unknown sound ("+cv+")");
const comet=c.comet;$("#cometOn").disabled=$("#cometColor").disabled=$("#cometRetry").disabled=!comet;
document.querySelectorAll("#cometRings .led").forEach(b=>b.disabled=!comet);
if(comet&&Array.isArray(comet.sceneSteps)&&comet.sceneSteps.length===15)cometSceneSteps=comet.sceneSteps;
if(comet){const saving=cometDraftWaiting(comet);
if(!saving){if(document.activeElement!==$("#cometOn"))$("#cometOn").checked=comet.on;
if(document.activeElement!==$("#cometColor"))$("#cometColor").value=String(comet.color);
if(comet.rings>=1&&comet.rings<=15)cometRings=comet.rings}
renderRings();
$("#cometStatus").textContent=saving?"Saving your seconds options…":comet.status||"Saved — waiting for clock";
$("#cometDndState").textContent=!c.connected?"Clock Do Not Disturb: not connected":comet.stateFresh===false?"Clock Do Not Disturb: waiting for fresh status":
"Clock Do Not Disturb: "+(comet.dndActive?"active":"inactive")+". Automatic night mode is a separate setting."}
else $("#cometStatus").textContent="Install Bridge 1.12 or newer to use the comet.";
const h=c.hand;$("#hState").textContent=!h.valid?"not reported":h.motorFailure?"motor failure":h.homingFailure?"homing failure":h.homing?"calibrating hands…":h.awaiting?"waiting for confirmation":h.timeAdjusting?"setting hands to the time · not calibration":"idle";
$("#bCalStart").disabled=!ready||h.homing||h.awaiting||h.timeAdjusting||c.calibrationRequested;$("#bCalConfirm").disabled=!ready||h.homing||!(h.awaiting||c.calibrationRequested);
$("#fwVer").textContent=s.fw;$("#fwUp").textContent=fmtDur(s.uptime);$("#fwHeap").textContent=Math.round(s.heap/1024)+" KB";
$("#nSsid").textContent=s.wifi.ssid||"—";$("#nIp").textContent=s.wifi.connected?s.wifi.ip+" · openglance.local":"—";$("#nRssi").textContent=s.wifi.connected?s.wifi.rssi+" dBm":"—";
if(s.weather)renderWeather(s);
if(comet?.on){$("#wHomeText").textContent="Weather ring paused · comet selected";$("#wStatus").textContent="Weather ring paused while the comet is enabled. Forecast and saved settings are kept."}
if(c.alarms)renderAlarms(c);logInto($("#log"),s.log);logInto($("#logShort"),s.log.slice(0,6))}
function pollDelay(){if(refreshFailed)return 10000;
if(pendingComet)return 500; // brief acknowledgement checks only after a user edit
const c=S?.clock;if(S?.scan?.running||["scanning","connecting","securing","awaiting_pin","awaiting_confirm"].includes(c?.state))return 1500;
return c?.comet?.on?8000:5000}
function scheduleRefresh(){clearTimeout(pollTimer);if(document.hidden)return;
const delay=refreshAgain?250:pollDelay();refreshAgain=false;
$("#pollStatus").textContent="Cached status every "+Math.round(delay/1000)+" seconds · no Bluetooth reads. Automatic updates pause when this page is hidden.";
pollTimer=setTimeout(refresh,delay)}
async function refresh(){if(refreshBusy){refreshAgain=true;return}clearTimeout(pollTimer);refreshBusy=true;
try{const r=await fetch("/api/status",{cache:"no-store"});if(!r.ok)throw new Error("Status unavailable");render(await r.json());refreshFailed=false}
catch(e){refreshFailed=true;pill($("#hWifi"),"bad","Bridge offline")}
finally{refreshBusy=false;scheduleRefresh()}}
document.addEventListener("visibilitychange",()=>{if(document.hidden)clearTimeout(pollTimer);else refresh()});
setInterval(()=>{if(!document.hidden&&timeAnchor&&Number.isFinite(timeAnchor.wall))$("#tNow").textContent=new Date(timeAnchor.wall+performance.now()-timeAnchor.at).toISOString().slice(0,19).replace("T"," ")},1000);
function upload(){const f=$("#fwFile").files[0];if(!f)return toast("Choose the OTA .bin file first",true);
if(!confirm("Install "+f.name+"? The bridge restarts afterwards."))return;const fd=new FormData();fd.append("firmware",f,f.name);
const x=new XMLHttpRequest();const p=$("#fwProg");p.style.display="block";p.value=0;$("#bUpload").disabled=true;
x.upload.onprogress=e=>{if(e.lengthComputable)p.value=e.loaded/e.total*100};
x.onload=()=>{$("#bUpload").disabled=false;let j={};try{j=JSON.parse(x.responseText)}catch(e){}
if(j.ok){toast("Installed "+(j.version||"")+". Restarting…");setTimeout(()=>location.reload(),9000)}else{toast(j.error||"Update failed",true);p.style.display="none"}};
x.onerror=()=>{$("#bUpload").disabled=false;toast("Upload failed",true)};x.open("POST","/update");x.send(fd)}
refresh();
</script></body></html>)HTML";
