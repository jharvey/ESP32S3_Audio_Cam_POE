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
</style></head><body>
<header><h1>School intercom</h1><span id="sum"></span><span class="mute" id="via"></span>
<span class="mute" style="margin-left:auto"><a href="/">this unit's page</a></span></header>
<main><div id="err"></div><div id="rooms"></div></main>
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
  }catch(e){document.getElementById("err").textContent="Can't reach the unit: "+e.message;}
}
tick();setInterval(tick,2000);
</script></body></html>)PAGE";
}

}  // namespace ic
