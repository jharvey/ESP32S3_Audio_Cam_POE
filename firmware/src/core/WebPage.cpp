// The web interface page, built into the firmware. Plain HTML + JS, no external files
// (the unit may be on a network with no internet access).
#include "WebUi.h"

namespace ic {

const char* webPage() {
    return R"PAGE(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Intercom</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--ink:#1d2330;--mute:#667085;--line:#dde1e7;--accent:#2563eb;--ok:#16803c;--warn:#b45309;--bad:#c62828;--bar:#e6e9ee}
@media (prefers-color-scheme:dark){:root{--bg:#13161c;--card:#1c2029;--ink:#e6e9ef;--mute:#98a2b3;--line:#2c3240;--accent:#6c9cff;--ok:#4ade80;--warn:#fbbf24;--bad:#f87171;--bar:#2c3240}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{padding:16px;border-bottom:1px solid var(--line);background:var(--card);display:flex;flex-wrap:wrap;gap:8px 18px;align-items:baseline}
h1{margin:0;font-size:26px}h2{margin:0 0 10px;font-size:16px}
.chip{display:inline-block;padding:2px 9px;border-radius:99px;border:1px solid var(--line);font-size:13px;color:var(--mute)}
.chip.ok{color:var(--ok);border-color:currentColor}.chip.warn{color:var(--warn);border-color:currentColor}.chip.bad{color:var(--bad);border-color:currentColor}
main{display:grid;grid-template-columns:repeat(auto-fit,minmax(330px,1fr));gap:14px;padding:14px;max-width:1200px;margin:0 auto}
section{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px}
.big{font-size:30px;font-weight:600}.mute{color:var(--mute);font-size:13px}
.row{display:flex;justify-content:space-between;gap:10px;padding:3px 0}
.meter{height:8px;background:var(--bar);border-radius:4px;overflow:hidden;flex:1;margin-top:7px}.meter i{display:block;height:100%;background:var(--accent);width:0}
.lvl{display:grid;grid-template-columns:70px 1fr 34px;gap:8px;align-items:start}
table{width:100%;border-collapse:collapse;font-size:14px}td,th{padding:5px 4px;border-bottom:1px solid var(--line);text-align:left;vertical-align:middle}
th{color:var(--mute);font-weight:500;font-size:13px}
input[type=number],input[type=text],input[type=password]{width:100%;padding:6px 8px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--ink);font:inherit}
input.changed{border-color:var(--accent);outline:1px solid var(--accent)}
button{padding:7px 13px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--ink);font:inherit;cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}button:disabled{opacity:.5;cursor:default}
.btns{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
pre{white-space:pre-wrap;word-break:break-word;background:var(--bg);border:1px solid var(--line);border-radius:6px;padding:8px;margin:8px 0 0;font-size:12.5px;max-height:260px;overflow:auto}
.wide{grid-column:1/-1}#msg{min-height:20px}
</style></head><body>
<header><h1 id="unit">Room …</h1><span class="mute" id="ip"></span><span id="chips"></span><span class="mute" id="ver" style="margin-left:auto"></span></header>
<main>
<section><h2>Echo canceller</h2>
 <div class="row"><div><div class="big" id="erle">–</div><div class="mute">echo reduction (dB)</div></div><div id="aecchips"></div></div>
 <div class="lvl"><span class="mute">Mic</span><div class="meter"><i id="mmic"></i></div><span id="vmic"></span></div>
 <div class="lvl"><span class="mute">Sent</span><div class="meter"><i id="msent"></i></div><span id="vsent"></span></div>
 <div class="lvl"><span class="mute">Speaker</span><div class="meter"><i id="mspk"></i></div><span id="vspk"></span></div>
</section>
<section><h2>Audio</h2>
 <div class="row"><span>Talkers playing now</span><b id="talkers"></b></div>
 <div class="row"><span>Frames played</span><b id="played"></b></div>
 <div class="row"><span>Dropouts filled in</span><b id="concealed"></b></div>
 <div class="row"><span>Arrived too late</span><b id="late"></b></div>
 <div class="row"><span>Up for</span><b id="uptime"></b></div>
</section>
<section><h2>Tools</h2>
 <div class="mute">Auto-tune plays a 2 s noise burst. Keep the room quiet; it takes about 6 s.</div>
 <div class="meter" style="margin:10px 0"><i id="calbar"></i></div>
 <div class="btns"><button class="primary" id="bcal">Auto-tune</button><button id="btone">Test tone (2 s)</button></div>
 <div style="margin-top:14px" class="mute">Room number (sets the unit's IP address)</div>
 <div class="btns"><input type="text" id="room" placeholder="214 or 214a" style="max-width:140px"><button id="broom">Set room</button></div>
 <div id="msg" class="mute" style="margin-top:8px"></div>
</section>
<section class="wide"><h2>Settings</h2>
 <div class="mute">Changes apply at once but are lost on restart until saved. Undo restores the settings from before the last save or auto-tune.</div>
 <table><thead><tr><th>Setting</th><th style="width:130px">Value</th><th>Range</th><th>What it does</th></tr></thead><tbody id="params"></tbody></table>
 <div class="btns"><button class="primary" id="bapply" disabled>Apply changes</button><button id="bsave">Save</button><button id="bundo">Undo</button><button id="bload">Reload saved</button><button id="bdef">Factory defaults</button></div>
</section>
<section><h2>Last auto-tune</h2><pre id="cal">No auto-tune run since start-up.</pre></section>
<section><h2>Other units heard</h2><table><thead><tr><th>Unit</th><th>Link</th><th>Power</th><th>Up</th></tr></thead><tbody id="peers"></tbody></table></section>
<section class="wide"><h2>Console</h2>
 <div class="mute">Same commands as the USB console (type <b>help</b>). The web password can only be changed over USB.</div>
 <div class="btns"><input type="text" id="cmd" placeholder="stats" style="flex:1;min-width:200px"><button id="bcmd">Send</button></div>
 <pre id="out"></pre>
</section>
</main>
<script>
const $=id=>document.getElementById(id);
let built=false,last=null;
async function cmd(c){
  const r=await fetch('/api/cmd',{method:'POST',headers:{'X-Intercom':'1'},body:c});
  const t=await r.text();$('out').textContent='> '+c+'\n'+t+'\n'+$('out').textContent;return t;
}
function chip(t,cls){return '<span class="chip '+(cls||'')+'">'+t+'</span> ';}
function bar(id,v){$('m'+id).style.width=Math.min(100,v/255*100)+'%';$('v'+id).textContent=v;}
function dur(s){const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h?h+' h '+m+' min':m?m+' min '+(s%60)+' s':s+' s';}
function buildParams(ps){
  const tb=$('params');tb.innerHTML='';
  for(const p of ps){
    const tr=document.createElement('tr');
    const step=p.kind==='float'?'any':'1';
    const input=p.kind==='bool'?'<input type="checkbox" data-k="'+p.key+'">':'<input type="number" step="'+step+'" min="'+p.min+'" max="'+p.max+'" data-k="'+p.key+'">';
    tr.innerHTML='<td><code>'+p.key+'</code></td><td>'+input+'</td><td class="mute">'+(p.kind==='bool'?'on/off':p.min+' to '+p.max)+'</td><td class="mute">'+p.desc+'</td>';
    tb.appendChild(tr);
  }
  tb.querySelectorAll('input').forEach(i=>i.addEventListener('input',()=>{i.classList.add('changed');$('bapply').disabled=false;}));
  built=true;
}
function fillParams(ps){
  for(const p of ps){
    const i=document.querySelector('input[data-k="'+p.key+'"]');
    if(!i||i.classList.contains('changed'))continue;
    if(p.kind==='bool')i.checked=!!p.value;else if(document.activeElement!==i)i.value=p.value;
  }
}
async function refresh(){
  let s;
  try{const r=await fetch('/api/status');if(!r.ok){$('msg').textContent=await r.text();return;}s=await r.json();}
  catch(e){$('chips').innerHTML=chip('not responding','bad');return;}
  last=s;
  $('unit').textContent='Room '+s.unit;$('ip').textContent=s.ip;$('ver').textContent='firmware '+s.version;
  document.title='Intercom '+s.unit;
  let c=chip(s.link==='ethernet'?'Ethernet':s.link==='wifi'?'Wi-Fi':'no network',s.link==='ethernet'?'ok':s.link==='wifi'?'warn':'bad');
  c+=chip(s.power==='poe'?'PoE power':'battery '+s.battery+'%',s.power==='poe'?'ok':s.battery<25?'bad':'warn');
  if(s.talking)c+=chip('talking','warn');if(s.tone)c+=chip('test tone','warn');if(s.calibrating)c+=chip('auto-tune '+s.cal_progress+'%','warn');
  if(s.in_setup)c+=chip('room setup on panel','warn');
  $('chips').innerHTML=c;
  $('erle').textContent=s.aec.on?s.aec.erle_db.toFixed(1):'off';
  $('aecchips').innerHTML=(s.aec.far?chip('far end playing'):'')+(s.aec.double_talk?chip('double talk','warn'):'')+(s.aec.converged?chip('converged','ok'):'');
  bar('mic',s.levels.mic);bar('sent',s.levels.sent);bar('spk',s.levels.spk);
  $('talkers').textContent=s.talkers;$('played').textContent=s.audio.played;$('concealed').textContent=s.audio.concealed;$('late').textContent=s.audio.late;
  $('uptime').textContent=dur(s.uptime_s);
  $('calbar').style.width=(s.calibrating?s.cal_progress:0)+'%';$('bcal').disabled=s.calibrating;
  if(s.cal)$('cal').textContent=s.cal;
  if(!built)buildParams(s.params);fillParams(s.params);
  $('peers').innerHTML=s.peers.length?s.peers.map(p=>'<tr><td><b>'+p.unit+'</b>'+(p.talking?' '+chip('talking','warn'):'')+'</td><td>'+(p.link==='wifi'?'Wi-Fi':p.link==='ethernet'?'Ethernet':'none')+'</td><td>'+(p.power==='poe'?'PoE':'battery '+p.battery+'%')+'</td><td class="mute">'+dur(p.uptime_s)+'</td></tr>').join(''):'<tr><td colspan="4" class="mute">None heard yet.</td></tr>';
}
$('bapply').onclick=async()=>{
  const parts=[];
  document.querySelectorAll('#params input.changed').forEach(i=>{parts.push(i.dataset.k,i.type==='checkbox'?(i.checked?1:0):i.value);});
  if(!parts.length)return;
  const t=await cmd('set '+parts.join(' '));$('msg').textContent=t;
  document.querySelectorAll('#params input.changed').forEach(i=>i.classList.remove('changed'));$('bapply').disabled=true;refresh();
};
const simple=(id,c,ask)=>$(id).onclick=async()=>{if(ask&&!confirm(ask))return;$('msg').textContent=await cmd(c);document.querySelectorAll('#params input.changed').forEach(i=>i.classList.remove('changed'));$('bapply').disabled=true;refresh();};
simple('bsave','save');simple('bundo','undo','Restore the settings from before the last save or auto-tune?');simple('bload','load');
simple('bdef','defaults','Load factory settings? (Not saved until you press Save.)');
simple('bcal','cal','Start auto-tune? The unit plays a loud noise burst for 2 s.');simple('btone','tone 2');
$('broom').onclick=async()=>{const r=$('room').value.trim();if(!r)return;
  if(!confirm('Change this unit to room '+r+'? Its IP address changes too, so this page will stop working at the current address.'))return;
  $('msg').textContent=await cmd('unit '+r);};
$('bcmd').onclick=async()=>{const c=$('cmd').value.trim();if(c){await cmd(c);$('cmd').value='';refresh();}};
$('cmd').addEventListener('keydown',e=>{if(e.key==='Enter')$('bcmd').click();});
refresh();setInterval(refresh,1000);
</script></body></html>)PAGE";
}

}  // namespace ic
