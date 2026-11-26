// The whole-school status page (web console, R1.7 step 1): every unit this unit hears, as a grid of
// rooms, refreshed every 2 s from /api/school. Served by any unit; the office normally keeps the
// office unit's /school open in Chrome, Edge or Firefox. Plain HTML + JS, no external files.
#include "WebUi.h"

namespace ic {

const char* schoolPage() {
    return R"PAGE(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>School intercom</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--ink:#1d2330;--mute:#667085;--line:#dde1e7;--ok:#16803c;--okbg:#e7f6ec;--warn:#b45309;--warnbg:#fff4dc;
--bad:#c62828;--badbg:#fde8e8;--call:#1d4ed8;--callbg:#e0eaff;--talk:#7a5b00;--talkbg:#fff7cc}
@media (prefers-color-scheme:dark){:root{--bg:#13161c;--card:#1c2029;--ink:#e6e9ef;--mute:#98a2b3;--line:#2c3240;--ok:#4ade80;--okbg:#14301f;
--warn:#fbbf24;--warnbg:#33290f;--bad:#f87171;--badbg:#3a1717;--call:#8fb0ff;--callbg:#18234a;--talk:#fde68a;--talkbg:#332c0c}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{padding:14px 16px;border-bottom:1px solid var(--line);background:var(--card);display:flex;flex-wrap:wrap;gap:8px 18px;align-items:baseline}
h1{margin:0;font-size:22px}.mute{color:var(--mute);font-size:13px}a{color:inherit}
#sum{display:flex;flex-wrap:wrap;gap:8px}.n{padding:2px 10px;border-radius:99px;border:1px solid var(--line);font-size:13px}
main{padding:14px;max-width:1300px;margin:0 auto}
.floor{margin:0 0 6px;font-size:14px;color:var(--mute);font-weight:600}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px;margin-bottom:18px}
.room{background:var(--card);border:1px solid var(--line);border-left:6px solid var(--ok);border-radius:8px;padding:8px 10px;min-height:74px}
.room{cursor:pointer}.room.sel{outline:3px solid var(--call);outline-offset:1px}
.panels{display:grid;grid-template-columns:repeat(auto-fit,minmax(380px,1fr));gap:14px;margin-top:6px}
.panel{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px}
.panel h2{margin:0 0 8px;font-size:15px}
#extras{display:flex;flex-direction:column;gap:10px;margin:0 0 16px}#extras .talkbar,#extras .panel{margin:0}
.setupbar{display:flex;flex-wrap:wrap;gap:8px 10px;align-items:center;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 12px}
.setupbar input{font:inherit;padding:6px 8px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--ink);width:150px}
.setupbar button{font:600 14px system-ui,sans-serif;padding:7px 14px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--ink);cursor:pointer}
body:not(.setup) .panels{display:none}body.hasplan:not(.setup) #rooms{display:none}body.setup #setuppw{display:none}
@media(min-width:900px){
body.hasplan main{max-width:none}
body.hasplan #quad{display:grid;gap:10px;grid-template-columns:1fr 1fr;grid-template-rows:minmax(0,1fr) auto minmax(0,1fr);grid-template-areas:"f1 f2" "f3 x1" "f3 x2";height:calc(100vh - 92px);min-height:600px;margin-bottom:16px}
body.hasplan #plan,body.hasplan #plan .floors{display:contents}
body.hasplan #plan .fl{display:flex;flex-direction:column;min-height:0;min-width:0;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:8px 10px}
body.hasplan #plan .fl:nth-child(1){grid-area:f1}body.hasplan #plan .fl:nth-child(2){grid-area:f2}body.hasplan #plan .fl:nth-child(3){grid-area:f3}
body.hasplan #plan .fl figcaption{font-size:15px}body.hasplan #plan .fl svg{flex:1;min-height:0;height:100%;max-height:none}
body.hasplan #plan .pside{grid-area:x1;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 12px}
body.hasplan #extras{grid-area:x2;overflow:auto;min-height:0;margin:0}
}.prow{display:flex;flex-wrap:wrap;gap:6px 8px;align-items:center;margin:6px 0}
.panel input,.panel select{font:inherit;padding:5px 7px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--ink)}
.panel button{font:inherit;padding:5px 11px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--ink);cursor:pointer}
.panel pre{white-space:pre-wrap;background:var(--bg);border:1px solid var(--line);border-radius:6px;padding:8px;margin:6px 0 0;font-size:12.5px;max-height:220px;overflow:auto}
#tkind{font:inherit;padding:6px 8px;border-radius:6px;border:1px solid var(--line);background:var(--card);color:var(--ink)}
.room b{font-size:22px;letter-spacing:.5px}.room .st{font-size:13px;font-weight:600}.room .d{font-size:12px;color:var(--mute)}
.ok{border-left-color:var(--ok)}.ok .st{color:var(--ok)}
.warn{border-left-color:var(--warn);background:var(--warnbg)}.warn .st{color:var(--warn)}
.bad{border-left-color:var(--bad);background:var(--badbg)}.bad .st{color:var(--bad)}
.call{border-left-color:var(--call);background:var(--callbg)}.call .st{color:var(--call)}
.talk{border-left-color:var(--talk);background:var(--talkbg)}.talk .st{color:var(--talk)}
#err{color:var(--bad);font-weight:600}
.talkbar{display:flex;flex-wrap:wrap;align-items:center;gap:10px 14px;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px;margin:0 0 16px}
#talk{font:600 17px system-ui,sans-serif;padding:14px 26px;border-radius:10px;border:2px solid var(--bad);background:var(--card);color:var(--bad);cursor:pointer;user-select:none;touch-action:none}
#talk.on{background:var(--bad);color:#fff}
#tstate{font-weight:600}.meterbox{width:140px;height:8px;background:var(--line);border-radius:4px;overflow:hidden}#tmeter{height:100%;width:0;background:var(--ok)}
</style></head><body>
<header><h1>School intercom</h1><span id="sum"></span><span class="mute" id="via"></span>
<span class="mute" style="margin-left:auto"><a href="/">this unit's page</a></span></header>
<main><div id="err"></div>
<div id="quad"><div id="plan"></div>
<div id="extras"><div class="talkbar"><select id="tkind" title="Who hears you"><option value="all">Everyone</option>
<option value="room">The selected room</option><option value="floor">The selected room's floor</option></select>
<button id="talk" title="Hold the button (or the space bar) and speak">Hold to talk to everyone</button>
<div class="meterbox"><div id="tmeter"></div></div><span id="tstate" class="mute">Uses this PC's microphone. Rooms answer on the office unit.</span></div>
<div id="queue"></div>
<section class="panel"><h2>Emergency</h2>
 <div class="prow"><button id="elock">Lockdown</button><button id="eevac">Evacuate</button><button id="eshel">Shelter</button><button id="eclear">All clear</button></div>
 <div id="estate" class="mute">…</div>
 <div class="mute">Every unit plays the signal (and its recorded message, if it has one) for a minute, evacuate for five. Then it stays
 quiet with its display and alarm light on until All clear. Talking from this page still gets through.</div></section>
<div class="setupbar"><b>Setup</b><input type="password" id="setuppw" placeholder="web password" autocomplete="off"><button id="setupbtn">Open setup</button>
 <span class="mute" id="setupnote">Rooms list, features, timed events, commands, installer tests and the log.</span></div>
</div></div>
<div id="rooms"></div>
<div class="panels">
<section class="panel"><h2>Features</h2>
 <div class="prow" id="feats"></div>
 <div class="mute">Each feature is off until ticked. A tick sets it on this unit and sends it to all units.</div>
 <div class="prow">Office unit <input type="number" id="ofc" min="1" max="999" style="width:5em"> stand-in <input type="number" id="ofc2" min="1" max="999" style="width:5em">
 <button id="ofcset">Set on all units</button><span class="mute" id="ofcnow"></span></div>
 <div class="prow">Quiet hours <input type="time" id="q1" value="18:00"> to <input type="time" id="q2" value="07:00"> by
 <input type="number" id="qdb" value="-12" min="-30" max="0" style="width:5em"> dB <button id="qset">Set on all units</button><button id="qoff">Off</button></div>
 <div class="prow">Zone <input type="number" id="zn" min="1" max="8" value="1" style="width:4em"><input id="zname" placeholder="east" size="8">
 <input id="zrooms" placeholder="101-112,201" size="16"><button id="zset">Set on all units</button><button id="zdel">Remove</button></div>
 <div class="prow">The selected room: <button id="dnd45">Do not disturb 45 min</button><button id="dndoff">End it</button>
 <button id="helpclr">Clear its call for help</button></div></section>
<section class="panel"><h2>Log: faults and calls</h2>
 <div class="prow"><select id="lkind"><option value="">Faults and calls</option><option value="0">Faults</option><option value="1">Calls</option>
 <option value="all">Everything</option></select><span class="mute">Seen by this unit since it started. Newest first.</span></div>
 <div id="log" style="max-height:320px;overflow:auto"></div></section>
<section class="panel"><h2>Commands</h2>
 <div class="prow"><select id="ctarget"><option value="all">All units</option><option value="sel">The selected room</option></select>
 <input id="cmd" placeholder="stats" size="24"><button id="csend">Send</button></div>
 <div class="mute">The same commands as the USB console ("help" lists them), signed with this unit's network key.</div>
 <pre id="cout"></pre></section>
<section class="panel"><h2>Timed events (kept on every unit)</h2>
 <pre id="sched">…</pre>
 <div class="prow"><input type="time" id="stime" value="11:30"><select id="sdays"><option>mon-fri</option><option>daily</option><option>weekend</option></select>
 <select id="ssound"><option>bell</option><option>lunch</option><option>chime</option><option>tone</option></select>
 <select id="swho"><option value="all">everyone</option><option value="room">the selected room</option><option value="floor">its floor</option></select>
 <button id="sadd">Add</button></div>
 <div class="prow">Delete number <input type="number" id="sdel" min="1" style="width:5em"><button id="sdelb">Delete</button>
 <button id="sskip">Skip today</button><button id="sresume">Resume today</button></div>
 <div class="prow">Day off <input type="date" id="soff"><button id="soffadd">Add</button><button id="soffdel">Remove</button></div>
</section>
<section class="panel"><h2>Installer tests</h2>
 <div class="prow"><button id="tclosed">Coupling test: doors closed</button><button id="topen">Coupling test: doors open</button></div>
 <div class="prow"><button id="tcal">Auto-tune the selected room</button></div>
 <div class="prow">Nightly speaker and microphone check at <input type="time" id="sctime" value="02:30"><button id="scset">Set on all units</button>
 <button id="scoff">Off</button></div>
 <div class="mute">Coupling test: every unit plays a short sweep in turn (about 1 s each) while the others listen, to find
 rooms that hear each other. Run it once with the classroom doors closed and once with them open; rooms must be quiet.
 Auto-tune: the room's unit plays noise for 2 s to set its echo canceller. Results show under Commands.</div></section>
</div></main>
<script>
const EMERG=["","LOCKDOWN","EVACUATE","SHELTER"];let selfFw="";
const FAULT=["","E1 no Ethernet (on Wi-Fi)","E2 no network","Lo battery low","dUP duplicate number","oL output shorted"];
function card(u){
  let cls="ok",st="OK",d=[];
  d.push(u.link==="ethernet"?"Ethernet":u.link==="wifi"?"Wi-Fi":"no link");
  d.push(u.power==="battery"?"battery "+u.battery+"%":"PoE");
  if(u.group&&u.group!==u.room)d.push("group "+u.group);
  if(u.fw&&selfFw&&u.fw!==selfFw)d.push("firmware "+u.fw);
  if(u.flags2&1)d.push("do not disturb");
  const f=u.flags;
  if(!u.online){cls="bad";st="OFFLINE";d=["last heard "+u.age_s.toFixed(0)+" s ago"];}
  else if(u.emergency){cls="bad";st="EMERGENCY: "+EMERG[u.emergency];}
  else if(u.flags2&2){cls="bad";st="SILENT HELP CALL";}
  else if((f&32)&&(f&128)){cls="bad";st="HELP NEEDED";}
  else if(f&32){cls="call";st="CALLING THE OFFICE";}
  else if(f&64){cls="bad";st="SELF-CHECK FAILED";}
  else if(u.fault===4||u.fault===5||u.fault===3||(f&16)){cls="bad";st=(f&16)?"TALK HELD >2 min":FAULT[u.fault];}
  else if(u.fault||(f&8)){cls="warn";st=u.fault?FAULT[u.fault]:"TALK muted";}
  else if(f&1){cls="talk";st="talking";}
  const sel=selRoom&&selRoom.room===u.room?" sel":"";
  return `<div class="room ${cls}${sel}" data-room="${u.room}" data-group="${u.group}"><b>${u.unit}</b><div class="st">${st}</div><div class="d">${d.join(" · ")}</div></div>`;
}
async function tick(){
  try{
    const r=await fetch(location.origin+"/api/school",{cache:"no-store"});
    if(!r.ok)throw new Error(r.status+" "+r.statusText);
    const j=await r.json();
    document.getElementById("err").textContent="";
    document.getElementById("via").textContent="via unit "+j.self+" · "+new Date().toLocaleTimeString();
    const us=j.units.sort((a,b)=>a.room-b.room);
    const c={on:0,off:0,call:0,bad:0,talk:0};
    for(const u of us){if(!u.online){c.off++;continue}c.on++;if(u.flags&32)c.call++;if(u.flags&1)c.talk++;
      if(u.fault||(u.flags&(8|16)))c.bad++;}
    document.getElementById("sum").innerHTML=`<span class="n">${c.on} online</span><span class="n">${c.off} offline</span>`+
      `<span class="n">${c.call} calling</span><span class="n">${c.talk} talking</span><span class="n">${c.bad} need attention</span>`;
    const floors={};for(const u of us){const fl=Math.floor(u.room/100);(floors[fl]=floors[fl]||[]).push(u);}
    document.getElementById("rooms").innerHTML=Object.keys(floors).sort((a,b)=>a-b).map(fl=>
      `<div class="floor">${fl==="0"?"Office / other":"Floor "+fl}</div><div class="grid">${floors[fl].map(card).join("")}</div>`).join("");
    lastSchool=j;selfFw=j.fw||"";drawFeats(j.features||0);drawQueue(j.queue||[]);drawZones((j.features&4)?j.zones||[]:[]);
    document.getElementById("ofcnow").textContent=j.office?"Now: office "+j.office+(j.standin?", stand-in "+j.standin:"")+", acting "+j.acting:"No office unit set.";
    {const em=us.filter(u=>u.online&&u.emergency);
     document.getElementById("estate").textContent=em.length?EMERG[em[0].emergency]+" on "+em.length+" of "+c.on+" units online":"No emergency.";}
    if(window.planUpdate)planUpdate(us);  // floor plan map (/floorplan.js)
  }catch(e){document.getElementById("err").textContent="Can't reach the unit: "+e.message;}
}
tick();setInterval(tick,2000);

// ---- Features (R3.2): switches, office and stand-in, quiet hours, zones, the waiting queue ----------
const FEATS=[["reason","Help needed: OFFICE held 1.5 s (a short press is a question)"],["queue","Call queue: rooms wait in order, help first"],
  ["zones","Zones: page a set of rooms"],["dnd","Do not disturb for a room"],["quiet","Quiet hours"],
  ["silent","Silent help call: OFFICE held 5 s, the office hears the room"],["failover","Stand-in office unit"]];
let featBits=-1,zoneSig="";
function drawFeats(bits){if(bits===featBits)return;featBits=bits;
  document.getElementById("feats").innerHTML=FEATS.map((f,i)=>`<label style="display:block"><input type="checkbox" data-f="${f[0]}" ${bits&(1<<i)?"checked":""}> ${f[1]}</label>`).join("");}
async function both(c){const r=await cmd(c);out("> "+c+"   ("+r+")");return sendTo("all",c);}
document.getElementById("feats").addEventListener("change",async e=>{const f=e.target.dataset.f;if(!f)return;
  if(f==="silent"&&e.target.checked&&!confirm("A silent help call opens the room's microphone to the office for 2 minutes with no sound in the room. Switch it on for all units?")){e.target.checked=false;return;}
  await both("feature "+f+(e.target.checked?" on":" off"));featBits=-1;});
function drawQueue(q){
  document.getElementById("queue").innerHTML=q.length?'<div class="floor">Waiting for the office</div><div class="grid">'+q.map((w,i)=>
    `<div class="room ${w.silent||w.urgent?"bad":"call"}"><b>${i+1}. Room ${w.room}</b><div class="st">${w.silent?"SILENT HELP CALL":w.urgent?"HELP NEEDED":"question"}</div><div class="d">waiting ${w.wait_s} s</div></div>`).join("")+"</div>":"";}
function drawZones(zs){const sig=JSON.stringify(zs);if(sig===zoneSig)return;zoneSig=sig;const s=document.getElementById("tkind");
  [...s.querySelectorAll("option[data-z]")].forEach(o=>o.remove());
  for(const z of zs){const o=document.createElement("option");o.value="zone:"+z.n;o.dataset.z="1";o.textContent="Zone "+z.n+": "+z.name+" ("+z.rooms+")";s.appendChild(o);}}
const val=id=>document.getElementById(id).value.trim();
document.getElementById("ofcset").onclick=()=>{if(val("ofc"))both("office "+val("ofc")+(val("ofc2")?" "+val("ofc2"):""));};
document.getElementById("qset").onclick=()=>{if(val("q1")&&val("q2"))both("quiet "+val("q1")+" "+val("q2")+" "+(val("qdb")||"-12"));};
document.getElementById("qoff").onclick=()=>both("quiet off");
document.getElementById("zset").onclick=()=>{if(val("zn")&&val("zname")&&val("zrooms"))both("zone "+val("zn")+" "+val("zname")+" "+val("zrooms"));};
document.getElementById("zdel").onclick=()=>{if(val("zn"))both("zone "+val("zn")+" off");};
function selCmd(c){if(!selRoom){out("Click a room first.");return;}sendTo(String(selRoom.room),c);}
document.getElementById("dnd45").onclick=()=>selCmd("dnd 45");
document.getElementById("dndoff").onclick=()=>selCmd("dnd off");
document.getElementById("helpclr").onclick=()=>selCmd("callofc off");

// ---- Emergency, event log, nightly self-check (R3.1) --------------------------------------------
async function emerg(kind,ask){if(!confirm(ask))return;const r=await cmd("emergency "+kind+" all");out("> emergency "+kind+"   ("+r+")");tick();}
document.getElementById("elock").onclick=()=>emerg("lockdown","Start a LOCKDOWN on every unit?");
document.getElementById("eevac").onclick=()=>emerg("evacuate","Start an EVACUATION on every unit?");
document.getElementById("eshel").onclick=()=>emerg("shelter","Start a SHELTER signal on every unit?");
document.getElementById("eclear").onclick=()=>emerg("clear","End the emergency on every unit?");
let logN=0,logRows=[];
function esc(s){return s.replace(/[&<>]/g,ch=>({"&":"&amp;","<":"&lt;",">":"&gt;"}[ch]));}
function drawLog(){const k=document.getElementById("lkind").value;
  const rows=logRows.filter(e=>k==="all"||(k===""?e.kind<2:e.kind===+k)).slice().reverse();
  document.getElementById("log").innerHTML=rows.length?"<table>"+rows.map(e=>`<tr><td class="mute" style="white-space:nowrap;padding-right:10px">${
    e.utc?new Date(e.utc*1000).toLocaleString():"+"+e.up_s+" s"}</td><td>${e.kind===0?"<b>"+esc(e.text)+"</b>":esc(e.text)}</td></tr>`).join("")+"</table>"
    :'<span class="mute">Nothing yet.</span>';}
async function pollLog(){
  try{const r=await fetch(location.origin+"/api/log?since="+logN,{cache:"no-store"});if(!r.ok)return;const j=await r.json();
    if(j.next<logN)logRows=[];
    for(const e of j.events)logRows.push(e);logN=j.next;if(logRows.length>200)logRows=logRows.slice(-200);drawLog();}catch(e){}
}
document.getElementById("lkind").onchange=drawLog;pollLog();setInterval(pollLog,3000);
document.getElementById("scset").onclick=async()=>{const t=document.getElementById("sctime").value;if(!t)return;await cmd("selfcheck "+t);sendTo("all","selfcheck "+t);};
document.getElementById("scoff").onclick=async()=>{await cmd("selfcheck off");sendTo("all","selfcheck off");};

// ---- Talk from this PC's microphone -------------------------------------------------------------
// The page captures the mic, converts it to 16 kHz 16-bit mono and posts ~100 ms at a time (in order)
// to /api/talk; the unit sends it as an office all-call. Browsers only allow the mic on HTTPS pages or
// localhost: see the office guide for the office PC setting.
let ctx=null,stream=null,src=null,proc=null,talking=false,chunks=[],have=0,busy=false,pos=0;
// Who hears you: everyone, the room picked by clicking its card (all units of its group, e.g. the gym), or its floor.
let selRoom=null,to="all",toText="everyone";
function updTarget(){
  const k=document.getElementById("tkind").value;
  if(k.startsWith("zone:")){to=k;toText="zone "+k.slice(5);}
  else if(k!=="all"&&!selRoom){to="all";toText="everyone (click a room first)";}
  else if(k==="room"){to=selRoom.group&&selRoom.group!==selRoom.room?"group:"+selRoom.group:"room:"+selRoom.room;
    toText=selRoom.group&&selRoom.group!==selRoom.room?"room group "+selRoom.group:"room "+selRoom.room;}
  else if(k==="floor"){const f=Math.floor(selRoom.room/100);to="floor:"+f;toText="floor "+f;}
  else{to="all";toText="everyone";}
  document.getElementById("talk").textContent=(to==="all"?"Hold to talk to ":"Hold to call ")+toText.replace(" (click a room first)","");
  if(!talking&&k!=="all"&&!k.startsWith("zone:")&&!selRoom)setState("Click a room to pick it.","st");
}
document.getElementById("rooms").addEventListener("click",e=>{
  const c=e.target.closest(".room");if(!c||talking)return;
  selRoom={room:+c.dataset.room,group:+c.dataset.group};
  if(document.getElementById("tkind").value==="all")document.getElementById("tkind").value="room";
  document.querySelectorAll(".room.sel").forEach(x=>x.classList.remove("sel"));c.classList.add("sel");updTarget();});
document.getElementById("tkind").addEventListener("change",updTarget);
// a dot clicked on the floor plan picks its room, as clicking its card does
// Floor plan (/floorplan.js): click a room = pick it (as clicking its card); hold = talk to it while held;
// double-click = ring it (it chimes and blinks for 10 s; the teacher answers with TALK, heard on this unit).
function planSel(info){const u=info.units[0];
  if(!u){setState("No unit in room "+info.room+" yet.","st");return false;}
  const c=document.querySelector('.room[data-room="'+u.room+'"]');if(c&&!c.classList.contains("sel"))c.click();return true;}
let planHolding=false;
window.onPlanPick=info=>{planSel(info);};
window.onPlanHold=async info=>{if(talking||!planSel(info))return;planHolding=true;
  document.getElementById("tkind").value="room";updTarget();await startTalk();if(!planHolding)stopTalk();};
window.onPlanHoldEnd=()=>{planHolding=false;stopTalk();};
// the plan's "whole building" button: talk to everyone while it is held
window.onPlanAll=async on=>{if(!on){planHolding=false;stopTalk();return;}if(talking)return;planHolding=true;
  document.getElementById("tkind").value="all";updTarget();await startTalk();if(!planHolding)stopTalk();};
window.onPlanRing=async info=>{if(!planSel(info))return;
  for(const u of info.units){const r=await cmd("send "+u.room+" ring "+lastSchool.self);out("> "+u.room+" ring   ("+r+")");}
  setState("Ringing room "+info.room+" for 10 s: the teacher answers with TALK, heard on this office unit.","st");};
// Setup (rooms list and the panels below) opens with the unit's web password, checked by the unit, and is closed
// again by the same button or by loading the page anew.
function setupShown(){const on=document.body.classList.contains("setup");document.getElementById("setupbtn").textContent=on?"Lock setup":"Open setup";
  document.getElementById("setupnote").textContent=on?"Setup is open below.":"Rooms list, features, timed events, commands, installer tests and the log.";}
document.getElementById("setupbtn").onclick=async()=>{
  if(document.body.classList.contains("setup")){document.body.classList.remove("setup");setupShown();return;}
  const box=document.getElementById("setuppw"),pw=box.value;box.value="";
  if(!pw){document.getElementById("setupnote").textContent="Type the web password first.";box.focus();return;}
  try{const r=await fetch(location.origin+"/api/status",{cache:"no-store",headers:{Authorization:"Basic "+btoa("admin:"+pw)}});
    if(r.ok){document.body.classList.add("setup");setupShown();document.querySelector(".panels").scrollIntoView({behavior:"smooth"});}
    else document.getElementById("setupnote").textContent=r.status===429?"Too many wrong passwords: wait 30 seconds.":"Wrong password.";
  }catch(e){document.getElementById("setupnote").textContent="Could not check the password.";}};
document.getElementById("setuppw").addEventListener("keydown",e=>{if(e.key==="Enter")document.getElementById("setupbtn").click();});
function setState(t,cls){const e=document.getElementById("tstate");e.textContent=t;e.className=cls||"mute";}
async function startTalk(){
  if(talking)return;
  if(!window.isSecureContext){setState("The browser blocks the microphone on this address (needs HTTPS): see the office guide.","st bad");return;}
  try{
    stream=stream||await navigator.mediaDevices.getUserMedia({audio:{channelCount:1,echoCancellation:true,noiseSuppression:true,autoGainControl:true}});
    ctx=ctx||new AudioContext();await ctx.resume();
    src=ctx.createMediaStreamSource(stream);proc=ctx.createScriptProcessor(2048,1,1);
    const step=ctx.sampleRate/16000;pos=0;
    proc.onaudioprocess=e=>{
      if(!talking)return;
      const f=e.inputBuffer.getChannelData(0);const out=[];let peak=0;
      for(;pos<f.length;pos+=step){  // 48 kHz (or 44.1) -> 16 kHz: average the samples in each step
        const a=Math.floor(pos),b=Math.min(f.length,Math.floor(pos+step));let s=0,n=0;
        for(let i=a;i<b;i++){s+=f[i];n++;}const v=n?s/n:f[a];peak=Math.max(peak,Math.abs(v));
        out.push(Math.max(-32767,Math.min(32767,Math.round(v*32767))));}
      pos-=f.length;
      chunks.push(Int16Array.from(out));have+=out.length;
      document.getElementById("tmeter").style.width=Math.min(100,peak*140)+"%";
      if(have>=1600)send();};
    src.connect(proc);proc.connect(ctx.destination);
    talking=true;document.getElementById("talk").classList.add("on");setState((to==="all"?"TALKING to ":"CALLING ")+toText+(to==="all"?"":" (it hears a chime; it answers on the office unit)"),"st bad");
  }catch(e){setState("No microphone: "+e.message,"st bad");}
}
function send(){
  if(busy||!have)return;
  const all=new Int16Array(have);let o=0;for(const c of chunks){all.set(c,o);o+=c.length;}chunks=[];have=0;
  const b=new Uint8Array(all.buffer);let s="";for(let i=0;i<b.length;i+=8192)s+=String.fromCharCode.apply(null,b.subarray(i,i+8192));
  busy=true;
  fetch(location.origin+"/api/talk?to="+encodeURIComponent(to),{method:"POST",headers:{"X-Intercom":"1","Content-Type":"text/plain"},body:btoa(s)})
    .then(r=>{if(!r.ok)setState("The unit refused the audio: "+r.status,"st bad");})
    .catch(()=>setState("Can't reach the unit","st bad"))
    .finally(()=>{busy=false;if(have>=1600||(!talking&&have))send();});
}
function stopTalk(){
  if(!talking)return;talking=false;send();
  if(proc){proc.disconnect();src.disconnect();proc=null;src=null;}
  document.getElementById("talk").classList.remove("on");document.getElementById("tmeter").style.width="0";
  setState("Uses this PC's microphone. Rooms answer on the office unit.","mute");
}
const tb=document.getElementById("talk");
tb.addEventListener("pointerdown",e=>{e.preventDefault();tb.setPointerCapture(e.pointerId);startTalk();});
for(const ev of ["pointerup","pointercancel","lostpointercapture"])tb.addEventListener(ev,stopTalk);
document.addEventListener("keydown",e=>{if(e.code==="Space"&&!e.repeat&&e.target===document.body){e.preventDefault();startTalk();}});
document.addEventListener("keyup",e=>{if(e.code==="Space")stopTalk();});
window.addEventListener("blur",stopTalk);
// ---- Commands to other units, and the timed events every unit keeps ------------------------------
async function cmd(text){  // runs a console command on this unit (e.g. "send all stats")
  const r=await fetch(location.origin+"/api/cmd",{method:"POST",headers:{"X-Intercom":"1","Content-Type":"text/plain"},body:text});
  return r.ok?await r.text():"error "+r.status;}
let replyN=0;
function out(line){const o=document.getElementById("cout");o.textContent+=line+"\n";o.scrollTop=o.scrollHeight;}
async function pollReplies(){
  try{const r=await fetch(location.origin+"/api/replies?since="+replyN,{cache:"no-store"});const j=await r.json();
    for(const l of j.lines)out(l.from+": "+l.text);replyN=j.next;}catch(e){}}
setInterval(pollReplies,1000);
async function sendTo(target,text){const r=await cmd("send "+target+" "+text);out("> "+target+" "+text+"   ("+r+")");return r;}
document.getElementById("csend").addEventListener("click",async()=>{
  const c=document.getElementById("cmd").value.trim();if(!c)return;
  let t="all";if(document.getElementById("ctarget").value==="sel"){if(!selRoom){out("click a room first");return;}t=String(selRoom.room);}
  await sendTo(t,c);document.getElementById("cmd").value="";});
document.getElementById("cmd").addEventListener("keydown",e=>{if(e.key==="Enter")document.getElementById("csend").click();});
async function loadSched(){document.getElementById("sched").textContent=await cmd("sched");}
// ---- Installer tests: need the tests password if one is set on the unit (USB: testpass) -----------
let lastSchool={units:[]},testPw=null;
async function testCmd(text){
  if(lastSchool.tests_locked&&testPw===null){const p=prompt("Tests password");if(p===null)return null;testPw=p;}
  const r=await cmd("tests "+(testPw||"-")+" "+text);
  if(r.indexOf("password")>=0)testPw=null;
  out("> "+text+"   ("+r+")");return r;}
async function coupleTest(open){
  const rooms=lastSchool.units.filter(u=>u.online).map(u=>u.room);
  if(rooms.length<2){out("coupling test: needs at least two units online");return;}
  if(!confirm(`Coupling test with the doors ${open?"OPEN":"CLOSED"}: every unit plays a short sweep in turn, about ${rooms.length+2} s. The rooms must be quiet. Start?`))return;
  const c="couple run 1 auto "+rooms.join(",")+(open?" open":" closed");
  const r=await testCmd("send all "+c);if(r===null||r.indexOf("sent")<0)return;
  await testCmd(c);
  setTimeout(async()=>{out("this unit: "+await cmd("couple"));sendTo("all","couple");},(rooms.length+3)*1000);}
document.getElementById("tclosed").addEventListener("click",()=>coupleTest(false));
document.getElementById("topen").addEventListener("click",()=>coupleTest(true));
document.getElementById("tcal").addEventListener("click",async()=>{
  if(!selRoom){out("auto-tune: click a room first");return;}
  if(!confirm(`Auto-tune room ${selRoom.room}: its unit plays a 2-second noise burst. The room should be quiet. Start?`))return;
  testCmd(String(selRoom.room)===lastSchool.self?"cal":"send "+selRoom.room+" cal");});
async function schedAll(text){await sendTo("all",text);setTimeout(loadSched,1500);}
document.getElementById("sadd").addEventListener("click",()=>{
  const w=document.getElementById("swho").value;let who="";
  if(w!=="all"){if(!selRoom){out("click a room first");return;}who=w==="room"?" room "+selRoom.room:" floor "+Math.floor(selRoom.room/100);}
  schedAll("sched add "+document.getElementById("stime").value+" "+document.getElementById("sdays").value+" "+document.getElementById("ssound").value+who);});
document.getElementById("sdelb").addEventListener("click",()=>{const n=document.getElementById("sdel").value;if(n)schedAll("sched del "+n);});
document.getElementById("sskip").addEventListener("click",()=>schedAll("skip today"));
document.getElementById("sresume").addEventListener("click",()=>schedAll("skip off"));
document.getElementById("soffadd").addEventListener("click",()=>{const d=document.getElementById("soff").value;if(d)schedAll("dayoff add "+d);});
document.getElementById("soffdel").addEventListener("click",()=>{const d=document.getElementById("soff").value;if(d)schedAll("dayoff del "+d);});
loadSched();
if(location.hash==="#test-plan"){  // video: a demo plan; hold room 214 for 4 s (talk), then double-click it (ring)
  const go=setInterval(()=>{if(!window.planTest||lastSchool.units.filter(u=>u.online).length<4)return;clearInterval(go);
    planUpdate(lastSchool.units);planTest.demo();document.getElementById("plan").scrollIntoView();
    setTimeout(()=>planTest.press(214,4000),2500);setTimeout(()=>planTest.dbl(214),9500);},300);}
{const m=location.hash.match(/^#test-talk(?:-(\d+))?$/);  // scripted checks / videos: talk N s (default 4)
 if(m){setTimeout(startTalk,1500);setTimeout(stopTalk,1500+1000*(+m[1]||4));}}
</script><script src="/floorplan.js"></script></body></html>)PAGE";
}

}  // namespace ic
