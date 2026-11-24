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
<div class="talkbar"><button id="talk" title="Hold the button (or the space bar) and speak">Hold to talk to everyone</button>
<div class="meterbox"><div id="tmeter"></div></div><span id="tstate" class="mute">Uses this PC's microphone. Rooms answer on the office unit.</span></div>
<div id="plan"></div>
<div id="rooms"></div></main>
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
  return `<div class="room ${cls}"><b>${u.unit}</b><div class="st">${st}</div><div class="d">${d.join(" · ")}</div></div>`;
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
    if(window.planUpdate)planUpdate(us);  // floor plan map (/floorplan.js)
  }catch(e){document.getElementById("err").textContent="Can't reach the unit: "+e.message;}
}
tick();setInterval(tick,2000);

// ---- Talk from this PC's microphone -------------------------------------------------------------
// The page captures the mic, converts it to 16 kHz 16-bit mono and posts ~100 ms at a time (in order)
// to /api/talk; the unit sends it as an office all-call. Browsers only allow the mic on HTTPS pages or
// localhost: see the office guide for the office PC setting.
let ctx=null,stream=null,src=null,proc=null,talking=false,chunks=[],have=0,busy=false,pos=0;
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
    talking=true;document.getElementById("talk").classList.add("on");setState("TALKING to everyone","st bad");
  }catch(e){setState("No microphone: "+e.message,"st bad");}
}
function send(){
  if(busy||!have)return;
  const all=new Int16Array(have);let o=0;for(const c of chunks){all.set(c,o);o+=c.length;}chunks=[];have=0;
  const b=new Uint8Array(all.buffer);let s="";for(let i=0;i<b.length;i+=8192)s+=String.fromCharCode.apply(null,b.subarray(i,i+8192));
  busy=true;
  fetch(location.origin+"/api/talk",{method:"POST",headers:{"X-Intercom":"1","Content-Type":"text/plain"},body:btoa(s)})
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
if(location.hash==="#test-talk"){setTimeout(startTalk,1500);setTimeout(stopTalk,5500);}  // scripted check: talk 4 s
</script><script src="/floorplan.js"></script></body></html>)PAGE";
}

}  // namespace ic
