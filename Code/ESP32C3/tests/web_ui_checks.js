// Source-level UI contracts/polling policy. No browser, network or clock access.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../OpenGlanceBridge/web_ui.h'), 'utf8');
const tab = name => source.match(new RegExp('<main class="tab" data-tab="'+name+'">([\\s\\S]*?)</main>'))?.[1] || '';
for (const id of ['tNow','tMode','mDate','mTime','bInternet','bManual','bSync','tz','oDaily'])
    assert(tab('home').includes('id="'+id+'"'), id+' must be on Home');
assert(!tab('home').includes('<details>'), 'Home time choices must not be hidden');
assert(tab('system').includes('<h2>Connection</h2>'));
assert(!tab('clock').includes('<h2>Connection</h2>'));
assert(tab('alerts').includes('id="msgText"') && tab('alerts').includes('id="tmMin"'));
assert(source.includes('if(t==="message")t="alerts"'), 'old bookmarks must remain usable');
assert(!source.includes('setInterval(refresh,1500)'), 'no unconditional 1.5-second poll');
const ids = [...source.slice(source.indexOf('static const char INDEX_HTML')).matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]);
assert.equal(ids.length, new Set(ids).size, 'no duplicate element IDs after moving cards');

const policy = source.match(/function pollDelay\(\)\{([\s\S]*?)\}\nfunction scheduleRefresh/)[1];
function delay(state, failed=false) {
    return vm.runInNewContext('(function(){'+policy+'})()', {S:state, refreshFailed:failed, pendingComet:null});
}
assert.equal(delay({clock:{state:'connected',comet:{on:true}}}),8000);
assert.equal(delay({clock:{state:'connected',comet:{on:false}}}),5000);
for (const state of ['scanning','connecting','securing','awaiting_pin','awaiting_confirm'])
    assert.equal(delay({clock:{state,comet:{on:true}}}),1500);
assert.equal(delay({scan:{running:true}}),1500);
assert.equal(delay(null),5000);
assert.equal(delay({},true),10000);
assert.equal(vm.runInNewContext('(function(){'+policy+'})()', {S:{},refreshFailed:false,pendingComet:{}}),500);
const ready = source.match(/function controlsReady\(c\)\{([^}]+)\}/)[1];
for (const busy of [false,true]) {
    assert.equal(vm.runInNewContext('(function(c){'+ready+'})(c)',{c:{connected:true,busy}}),true);
    assert.equal(vm.runInNewContext('(function(c){'+ready+'})(c)',{c:{connected:false,busy}}),false);
}
assert(!source.includes('const ready=c.connected&&!c.busy'), 'no whole-page busy blackout');
const native = fs.readFileSync(path.join(__dirname,'../OpenGlanceBridge/bridge.cpp'),'utf8');
assert(!native.includes('st.busy = true; }\n            updateComet()'), 'background animation is not a user command');
assert(!native.includes('hand = hand || (st.hand.valid && (st.hand.bits & 256))'), 'time adjustment is not calibration');
assert(source.includes('id="cometDndState"'));
assert(!source.includes('id="cometDnd"') && !source.includes('$("#cometDnd")'), 'removed DND switch has no leftover DOM references');
assert(!source.includes('respectDnd:'), 'new webpage does not submit removed option');
assert(!native.includes('cfg.cometRespectDnd') && !native.includes('prefs.getBool("cometdnd"'), 'old saved override cannot affect seconds');
assert(native.includes('comet::dndPauses(dnd)'));
assert(native.includes('comet::validLegacyDndOption(argInt("respectDnd", -2))'), 'old cached clients are validated, not granted an override');
assert(!native.includes('ignores Do Not Disturb') && !native.includes('DND ignored by bridge'));
const draft = source.match(/function cometDraftWaiting\(c\)\{([\s\S]*?)\}\nconst COMET_COLORS/)[1];
const draftVM={pendingComet:{on:1,color:'18',until:10000},performance:{now:()=>100}};
assert.equal(vm.runInNewContext('(function(c){'+draft+'})(c)',{...draftVM,c:{on:false,color:18,respectDnd:true}}),true);
assert.equal(vm.runInNewContext('(function(c){'+draft+'})(c)',{...draftVM,c:{on:true,color:18,respectDnd:false}}),false);
assert.equal(vm.runInNewContext('(function(c){'+draft+'})(c)',{...draftVM,c:null,performance:{now:()=>10001}}),false);

const scheduler = source.match(/function scheduleRefresh\(\)\{([\s\S]*?)\}\nasync function refresh/)[1];
let scheduled = [];
const sandbox = {pollTimer:99, document:{hidden:true}, refreshAgain:false,
    clearTimeout:()=>{}, setTimeout:(_fn,ms)=>scheduled.push(ms), refresh:()=>{},
    pollDelay:()=>8000, $:()=>({textContent:''})};
vm.runInNewContext('(function(){'+scheduler+'})()',sandbox);
assert.deepEqual(scheduled,[], 'hidden page must not schedule requests');
sandbox.document.hidden=false;
vm.runInNewContext('(function(){'+scheduler+'})()',sandbox);
assert.deepEqual(scheduled,[8000]);
assert(source.includes('if(refreshBusy){refreshAgain=true;return}'), 'serialize in-flight status fetches');
assert(source.includes('timeAnchor.wall+performance.now()-timeAnchor.at'), 'clock ticks locally without fetching status');
// 1.20: seconds are a single ticking pixel, new clock scene about every 10 s.
assert(source.includes('Show a ticking seconds pixel') && !source.includes('short fading comet') && !source.includes('Head + 2'), 'seconds label matches the single pixel');
assert(source.includes('i===head&&(cometRings>>ring&1)?"1":"0.08"') && source.includes('new clock scene about every 10 s'), 'preview lights the chosen LEDs at one ticking angle');
// 1.21: clickable LED picker, saved with the comet, never empty, interval from the bridge's table.
for (const bit of [1, 2, 4, 8]) assert(source.includes(`data-bit="${bit}"`) && source.includes(`onclick="toggleRing(${bit})"`), `LED ${bit} is clickable`);
assert(source.includes('rings:cometRings') && source.includes('Number(c.rings||1)===Number(pendingComet.rings||1)'), 'LED choice is saved and confirmed');
assert(source.includes('if(!next)return toast("Keep at least one LED lit",true)'), 'at least one LED stays lit');
assert(source.includes('comet.sceneSteps') && source.includes('cometSceneSteps[cometRings-1]'), 'blip interval uses the bridge table');
const ringGap = new Function('m', source.match(/function ringGap\(m\)\{([^}]*)\}/)[1]);
assert.deepEqual([1,2,3,4,5,6,7,8,9,10,11,12,13,14,15].filter(ringGap), [5,9,10,11,13], 'gap detection matches the firmware table');
const native2 = fs.readFileSync(path.join(__dirname, '../OpenGlanceBridge/bridge.cpp'), 'utf8');
assert(native2.includes('server.hasArg("rings") ? argInt("rings", -1) : cfg.cometRings'), 'old cached pages keep the saved LEDs');
// 1.22: Night settings card (DND field 1 + 3, Silent field 14 + 4), read from and verified by the clock.
assert(tab('display').includes('<h2>Night settings</h2>'), 'night settings live on the Display tab');
for (const w of ['dnd','silent']) {
    for (const k of ['Mode','From','Till','Save','Read','Hours']) assert(source.includes(`id="${w}${k}"`), `${w}${k} exists`);
    assert(source.includes(`onclick="saveNight('${w}')"`), `${w} has a save button`);
}
assert(source.includes('act("/api/night",{which:w,mode,from,till})') && source.includes('nightRefresh(g,ready);'), 'night card posts and refreshes');
assert(source.includes('if(mode===1&&from===till)return toast('), 'scheduled start and end must differ');
const nightText = new Function('x', source.match(/function nightText\(x\)\{([^\n]*)\}\n/)[1]);
assert.equal(nightText(undefined), 'not read yet');
assert.equal(nightText({known:false}), 'unreadable — not editable');
assert.equal(nightText({known:true,mode:1,from:22,till:7}), 'scheduled 22:00–07:00');
assert.equal(nightText({known:true,mode:0,from:22,till:8}), 'off');
assert.equal(nightText({known:true,mode:2,from:22,till:8}), 'always on');
assert(native2.includes('server.on("/api/night"') && native2.includes('glance::validSchedule(kind, mode, from, till)'), 'bridge validates night settings');
assert(native2.includes('glance::scheduleMatches(updated, kind, mode, from, till)'), 'bridge verifies by reading back');
console.log('PASS: webpage layout, Alerts, polling, hidden-page pause, local clock, busy-control and delayed comet-save regressions, single-pixel seconds preview, LED picker, night settings');
