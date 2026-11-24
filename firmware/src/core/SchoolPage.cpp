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
.panel h2{margin:0 0 8px;font-size:15px}.prow{display:flex;flex-wrap:wrap;gap:6px 8px;align-items:center;margin:6px 0}
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
<div class="talkbar"><select id="tkind" title="Who hears you"><option value="all">Everyone</option>
<option value="room">The selected room</option><option value="floor">The selected room's floor</option></select>
<button id="talk" title="Hold the button (or the space bar) and speak">Hold to talk to everyone</button>
<div class="meterbox"><div id="tmeter"></div></div><span id="tstate" class="mute">Uses this PC's microphone. Rooms answer on the office unit.</span></div>
<div id="plan"></div>
<div id="rooms"></div>
<div class="panels">
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
 <div class="mute">Coupling test: every unit plays a short sweep in turn (about 1 s each) while the others listen, to find
 rooms that hear each other. Run it once with the classroom doors closed and once with them open; rooms must be quiet.
 Auto-tune: the room's unit plays noise for 2 s to set its echo canceller. Results show under Commands.</div></section>
</div></main>
<script>
const FAULT=["","E1 no Ethernet (on Wi-Fi)","E2 no network","Lo battery low","dUP duplicate number","oL output shorted"];
function card(u){
  let cls="ok",st="OK",d=[];
  d.push(u.link==="ethernet"?"Ethernet":u.link==="wifi"?"Wi-Fi":"no link");
  d.push(u.power==="battery"?"battery "+u.battery+"%":"PoE");
  if(u.group&&u.group!==u.room)d.push("group "+u.group);
  const f=u.flags;
  if(!u.online){cls="bad";st="OFFLINE";d=["last heard "+u.age_s.toFixed(0)+" s ago"];}
  else if(f&32){cls="call";st="CALLING THE OFFICE";}
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
    lastSchool=j;
    if(window.planUpdate)planUpdate(us);  // floor plan map (/floorplan.js)
  }catch(e){document.getElementById("err").textContent="Can't reach the unit: "+e.message;}
}
tick();setInterval(tick,2000);

// ---- Talk from this PC's microphone -------------------------------------------------------------
// The page captures the mic, converts it to 16 kHz 16-bit mono and posts ~100 ms at a time (in order)
// to /api/talk; the unit sends it as an office all-call. Browsers only allow the mic on HTTPS pages or
// localhost: see the office guide for the office PC setting.
let ctx=null,stream=null,src=null,proc=null,talking=false,chunks=[],have=0,busy=false,pos=0;
// Who hears you: everyone, the room picked by clicking its card (all units of its group, e.g. the gym), or its floor.
let selRoom=null,to="all",toText="everyone";
function updTarget(){
  const k=document.getElementById("tkind").value;
  if(k!=="all"&&!selRoom){to="all";toText="everyone (click a room first)";}
  else if(k==="room"){to=selRoom.group&&selRoom.group!==selRoom.room?"group:"+selRoom.group:"room:"+selRoom.room;
    toText=selRoom.group&&selRoom.group!==selRoom.room?"room group "+selRoom.group:"room "+selRoom.room;}
  else if(k==="floor"){const f=Math.floor(selRoom.room/100);to="floor:"+f;toText="floor "+f;}
  else{to="all";toText="everyone";}
  document.getElementById("talk").textContent=(to==="all"?"Hold to talk to ":"Hold to call ")+toText.replace(" (click a room first)","");
  if(!talking&&k!=="all"&&!selRoom)setState("Click a room to pick it.","st");
}
document.getElementById("rooms").addEventListener("click",e=>{
  const c=e.target.closest(".room");if(!c||talking)return;
  selRoom={room:+c.dataset.room,group:+c.dataset.group};
  if(document.getElementById("tkind").value==="all")document.getElementById("tkind").value="room";
  document.querySelectorAll(".room.sel").forEach(x=>x.classList.remove("sel"));c.classList.add("sel");updTarget();});
document.getElementById("tkind").addEventListener("change",updTarget);
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
{const m=location.hash.match(/^#test-talk(?:-(\d+))?$/);  // scripted checks / videos: talk N s (default 4)
 if(m){setTimeout(startTalk,1500);setTimeout(stopTalk,1500+1000*(+m[1]||4));}}
</script><script src="/floorplan.js"></script></body></html>)PAGE";
}

}  // namespace ic
