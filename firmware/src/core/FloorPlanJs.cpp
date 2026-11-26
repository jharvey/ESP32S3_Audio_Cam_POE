// Floor plan map for the school page (/floorplan.js). The office loads the school's DXF floor plan in the
// browser; the script draws each floor (layers that carry room-number labels) and puts every unit on its
// room, coloured by its status, refreshed with the room grid. The DXF and the unit positions stay in that
// browser's local storage only: they are never sent to the unit (and so never end up in the firmware or
// its repository). Units whose number isn't a room on the plan (gym units 299, 298 ...) are placed by
// clicking: pick the unit, then click the map ("Move dots" rearranges them). Click a room to pick it; with
// the school page's hooks, hold a room to talk to it and double-click it to ring it.
#include "WebUi.h"

namespace ic {

const char* floorPlanJs() {
    return R"JS((function(){
"use strict";
const KP="icPlan1",KU="icPlace1",KF="icPlanFloor";
const st=document.createElement("style");
st.textContent=`#plan{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 12px;margin:0 0 16px}
#plan .bar{display:flex;flex-wrap:wrap;gap:6px 10px;align-items:center}#plan .bar b{font-size:15px}
#plan button{font:13px system-ui,sans-serif;padding:4px 10px;border-radius:6px;border:1px solid var(--line);background:var(--card);color:var(--ink);cursor:pointer}
#plan button.sel{border-color:var(--call);background:var(--callbg);color:var(--call);font-weight:600}
#plan .tabs{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 6px}#plan .hint{font-size:13px;color:var(--mute);margin:4px 0}
#plan svg{width:100%;height:auto;max-height:72vh;display:block;background:var(--bg);border-radius:6px;cursor:crosshair}
#plan .wall{fill:none;stroke:var(--mute);stroke-linecap:round}#plan .lbl{fill:var(--mute)}
#plan .dot{stroke:var(--card);cursor:pointer}#plan .num{fill:#fff;font-weight:700;pointer-events:none}
#plan .ok{fill:var(--ok)}#plan .warn{fill:var(--warn)}#plan .bad{fill:var(--bad)}#plan .call{fill:var(--call)}#plan .talk{fill:#d4a800}
#plan .chip{font-size:13px;padding:2px 8px}
#plan .floors{display:grid;grid-template-columns:repeat(auto-fit,minmax(330px,1fr));gap:10px;margin-top:6px}
#plan .fl{margin:0}#plan .fl figcaption{font-size:13px;font-weight:600;margin:0 0 3px}#plan .fl svg{max-height:60vh}
#plan button.big{font:600 15px system-ui,sans-serif;padding:9px 18px;border:2px solid var(--bad);color:var(--bad);border-radius:8px;user-select:none;touch-action:none}
#plan button.big.sel{background:var(--bad);color:#fff}`;
document.head.appendChild(st);
const box=document.getElementById("plan");if(!box)return;
function load(k,d){try{const v=localStorage.getItem(k);return v?JSON.parse(v):d}catch(e){return d}}
function save(k,v){try{localStorage.setItem(k,JSON.stringify(v));return true}catch(e){return false}}
let plan=load(KP,null),place=load(KU,{}),floor=load(KF,0),units=[],sel=null,msg="",moving=false;

// ---- DXF -> floors ---------------------------------------------------------------------------------
function parseDxf(txt){
  const L=txt.split(/\r?\n/),P=[];for(let i=0;i+1<L.length;i+=2)P.push([parseInt(L[i],10),L[i+1].trim()]);
  const blocks={},ents=[];let sec=null,blk=null,hdr=false,cur=null;
  const flush=()=>{if(cur)(blk?blk.ents:ents).push(cur);cur=null};
  for(let i=0;i<P.length;i++){const c=P[i][0],v=P[i][1];
    if(c===0){flush();
      if(v==="SECTION"){sec=(P[i+1]&&P[i+1][0]===2)?P[i+1][1]:null;i++;continue}
      if(v==="ENDSEC"){sec=null;continue}
      if(sec==="BLOCKS"&&v==="BLOCK"){blk={name:"",x:0,y:0,ents:[]};hdr=true;continue}
      if(v==="ENDBLK"){if(blk)blocks[blk.name]=blk;blk=null;hdr=false;continue}
      hdr=false;if(sec==="ENTITIES"||(sec==="BLOCKS"&&blk))cur={t:v,g:[]};continue}
    if(hdr&&blk){if(c===2)blk.name=v;else if(c===10)blk.x=+v;else if(c===20)blk.y=+v;continue}
    if(cur)cur.g.push([c,v]);}
  flush();return{blocks,ents};
}
function g1(e,c,d){for(const p of e.g)if(p[0]===c)return p[1];return d}
function num(e,c,d){const v=g1(e,c,null);return v===null?d:+v}
function cleanText(s){return s.replace(/\\P/g," ").replace(/\\[A-Za-z][^;\\]*;/g,"").replace(/[{}]/g,"").replace(/\s+/g," ").trim()}
function build(dxf){
  const lines={},texts=[];  // lines[layer] = [[x,y],...] polylines
  const add=(lay,pts)=>{if(pts.length>1)(lines[lay]=lines[lay]||[]).push(pts)};
  function walk(list,xf,over,depth){
    let poly=null;
    for(const e of list){
      let lay=g1(e,8,"0");if(lay==="0"&&over)lay=over;
      const P=(x,y)=>xf(x,y);
      switch(e.t){
      case "LINE":add(lay,[P(num(e,10,0),num(e,20,0)),P(num(e,11,0),num(e,21,0))]);break;
      case "LWPOLYLINE":{const xs=[],ys=[];for(const p of e.g){if(p[0]===10)xs.push(+p[1]);else if(p[0]===20)ys.push(+p[1]);}
        const pts=xs.map((x,k)=>P(x,ys[k]||0));if(num(e,70,0)&1&&pts.length)pts.push(pts[0]);add(lay,pts);break}
      case "POLYLINE":poly={lay,pts:[],closed:num(e,70,0)&1};break;
      case "VERTEX":if(poly)poly.pts.push(P(num(e,10,0),num(e,20,0)));break;
      case "SEQEND":if(poly){if(poly.closed&&poly.pts.length)poly.pts.push(poly.pts[0]);add(poly.lay,poly.pts);poly=null}break;
      case "ARC":case "CIRCLE":{const cx=num(e,10,0),cy=num(e,20,0),r=num(e,40,0);let a0=e.t==="ARC"?num(e,50,0):0,a1=e.t==="ARC"?num(e,51,360):360;
        if(a1<=a0)a1+=360;const n=Math.max(3,Math.ceil((a1-a0)/12)),pts=[];
        for(let k=0;k<=n;k++){const a=(a0+(a1-a0)*k/n)*Math.PI/180;pts.push(P(cx+r*Math.cos(a),cy+r*Math.sin(a)))}add(lay,pts);break}
      case "INSERT":{const b=dxf.blocks[g1(e,2,"")];if(!b||depth>6)break;
        if(!/door/i.test(b.name))break;  // keep it basic: walls and doors only (no cars, sinks, furniture)
        const ix=num(e,10,0),iy=num(e,20,0),sx=num(e,41,1),sy=num(e,42,1),ro=num(e,50,0)*Math.PI/180,co=Math.cos(ro),si=Math.sin(ro);
        walk(b.ents,(x,y)=>{const u=(x-b.x)*sx,v=(y-b.y)*sy;return xf(ix+u*co-v*si,iy+u*si+v*co)},lay,depth+1);break}
      case "TEXT":case "MTEXT":if(depth===0){let s="";for(const p of e.g)if(p[0]===3)s+=p[1];s+=g1(e,1,"");
        const q=P(num(e,10,0),num(e,20,0));texts.push({lay,s:cleanText(s),x:q[0],y:q[1]})}break;
      }
    }
  }
  walk(dxf.ents,(x,y)=>[x,y],null,0);
  // A floor is a layer with room-number labels (3 digits).
  const byLay={};
  for(const t of texts){const m=t.s.match(/(^|\D)(\d{3})(?!\d)/g);if(!m)continue;
    for(const tok of m){const n=+tok.replace(/\D/g,"");(byLay[t.lay]=byLay[t.lay]||[]).push({n,t})}}
  // The floor number is the most common hundreds digit of its rooms (a floor may also label a room below it).
  const hundOf=lay=>{const c={};for(const r of byLay[lay]){const h=Math.floor(r.n/100);c[h]=(c[h]||0)+1}
    return +Object.keys(c).sort((a,b)=>c[b]-c[a]||a-b)[0]};
  const lays=Object.keys(byLay).filter(l=>lines[l]).sort((a,b)=>hundOf(a)-hundOf(b));
  if(!lays.length)throw new Error("no layer has room-number labels (like \"ART 101\")");
  const floors=[],rooms={};
  lays.forEach((lay,fi)=>{
    const R=v=>Math.round(v*2)/2;let d="";const xs=[],ys=[];
    for(const pl of lines[lay]){d+="M"+pl.map(p=>{xs.push(p[0]);ys.push(-p[1]);return R(p[0])+" "+R(-p[1])}).join("L")}
    const lbl=byLay[lay].map(r=>[R(r.t.x),R(-r.t.y)]);  // room labels: only used to frame the view
    // view: the drawing, but no further than 30% beyond the rooms, so a stray far-off bit doesn't shrink the floor
    const mn=a=>a.reduce((m,v)=>Math.min(m,v),1e18),mx=a=>a.reduce((m,v)=>Math.max(m,v),-1e18);
    const lx=lbl.map(t=>t[0]),ly=lbl.map(t=>t[1]),lw=mx(lx)-mn(lx),lh=mx(ly)-mn(ly);
    const dw=mx(xs)-mn(xs),dh=mx(ys)-mn(ys),few=Math.max(lw,lh)<0.2*Math.max(dw,dh);  // one room or a few close together
    const e=few?1e18:Math.max(0.3*Math.max(lw,lh),0.15*Math.max(dw,dh));  // few: frame the whole drawing
    let x0=Math.max(mn(xs),mn(lx)-e),x1=Math.min(mx(xs),mx(lx)+e),y0=Math.max(mn(ys),mn(ly)-e),y1=Math.min(mx(ys),mx(ly)+e);
    if(x1-x0<1){x0-=1;x1+=1}if(y1-y0<1){y0-=1;y1+=1}
    const hund=hundOf(lay);
    floors.push({name:"Floor "+hund,x0,y0,x1,y1,d});
    for(const r of byLay[lay]){const old=rooms[r.n];
      if(!old||Math.floor(r.n/100)===hund)rooms[r.n]={f:fi,x:R(r.t.x),y:R(-r.t.y)}}
  });
  return{floors,rooms,when:new Date().toISOString().slice(0,10)};
}

// ---- drawing ----------------------------------------------------------------------------------------
function cls(u){const f=u.flags;if(!u.online)return"bad";if(f&32)return"call";if(u.fault===3||u.fault===4||u.fault===5||(f&16))return"bad";
  if(u.fault||(f&8))return"warn";if(f&1)return"talk";return"ok"}
const rank={bad:4,call:3,warn:2,talk:1,ok:0};
function where(u){return place[u.unit]||(plan&&plan.rooms[u.room])||null}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]))}

// ---- room areas: the walls drawn thick enough to close the door gaps, flood-filled from each room's
// label, then grown back to the walls plus a small margin, so a click on or near a room finds it.
const regCache={};
function regionsFor(fi){
  if(regCache[fi])return regCache[fi];
  const F=plan.floors[fi],W=F.x1-F.x0,H=F.y1-F.y0,M=Math.max(W,H),s=M/700;
  const cw=Math.ceil(W/s)+3,ch=Math.ceil(H/s)+3,N=cw*ch,shut=0.011*M,grow=Math.ceil((shut+0.009*M)/s);
  const cv=document.createElement("canvas");cv.width=cw;cv.height=ch;const c=cv.getContext("2d");
  c.setTransform(1/s,0,0,1/s,-F.x0/s+1,-F.y0/s+1);c.lineWidth=2*shut;c.lineCap="round";c.lineJoin="round";
  c.stroke(new Path2D(F.d));
  const px=c.getImageData(0,0,cw,ch).data,wall=new Uint8Array(N);for(let i=0;i<N;i++)wall[i]=px[i*4+3]>40?1:0;
  const own=new Int16Array(N).fill(-1),list=[];
  for(const n in plan.rooms){const p=plan.rooms[n];if(p.f===fi)list.push({n:+n,x:Math.round((p.x-F.x0)/s+1),y:Math.round((p.y-F.y0)/s+1),k:list.length})}
  const shareOf={};  // region id -> labels in it
  const q=new Int32Array(N);
  for(const L of list){
    let st=-1;  // nearest free pixel to the label (labels can sit on a thick wall)
    for(let r=0;r<8&&st<0;r++)for(let dy=-r;dy<=r&&st<0;dy++)for(let dx=-r;dx<=r;dx++){
      const x=L.x+dx,y=L.y+dy;if(x<0||y<0||x>=cw||y>=ch)continue;const i=y*cw+x;if(!wall[i]){st=i;break}}
    if(st<0)continue;
    if(own[st]>=0){(shareOf[own[st]]=shareOf[own[st]]||[own[st]]).push(L.k);continue}
    let h=0,t=0;q[t++]=st;own[st]=L.k;
    while(h<t){const i=q[h++],x=i%cw;
      for(const j of [x>0?i-1:-1,x<cw-1?i+1:-1,i-cw,i+cw])if(j>=0&&j<N&&!wall[j]&&own[j]<0){own[j]=L.k;q[t++]=j}}
    if(t>0.35*N){for(let k=0;k<t;k++)own[q[k]]=-1;L.leak=true}  // ran outside through a gap: use a circle
  }
  for(const id in shareOf){  // several labels in one area: each pixel goes to the nearest label
    const ls=shareOf[id].map(k=>list[k]);
    for(let i=0;i<N;i++)if(own[i]==id){const x=i%cw,y=(i/cw)|0;let b=ls[0],bd=1e18;
      for(const L of ls){const d=(L.x-x)**2+(L.y-y)**2;if(d<bd){bd=d;b=L}}own[i]=b.k}}
  // Unlabelled pockets inside the building (court markings in the gym, closets, stair treads) join the
  // nearest labelled room they touch across a wall or line; the outside (touching the edge) stays out.
  {const seen=new Uint8Array(N),pk=new Int32Array(N),D=Math.ceil(2*shut/s)+3,dd=new Uint16Array(N);
   for(let i0=0;i0<N;i0++){if(wall[i0]||own[i0]>=0||seen[i0])continue;
     let n=0,edge=false,sx=0,sy=0;pk[n++]=i0;seen[i0]=1;
     for(let h=0;h<n;h++){const i=pk[h],x=i%cw,y=(i/cw)|0;sx+=x;sy+=y;if(x===0||y===0||x===cw-1||y===ch-1)edge=true;
       for(const j of [x>0?i-1:-1,x<cw-1?i+1:-1,i-cw,i+cw])if(j>=0&&j<N&&!wall[j]&&own[j]<0&&!seen[j]){seen[j]=1;pk[n++]=j}}
     if(edge||n>0.35*N)continue;
     const touch=new Set(),fr=[],mark=new Set();  // walk out through the walls / lines, up to D pixels
     for(let k=0;k<n;k++){fr.push(pk[k]);dd[pk[k]]=0;mark.add(pk[k])}
     for(let h=0;h<fr.length;h++){const i=fr[h],x=i%cw;if(dd[i]>=D)continue;
       for(const j of [x>0?i-1:-1,x<cw-1?i+1:-1,i-cw,i+cw]){if(j<0||j>=N||mark.has(j))continue;mark.add(j);
         if(own[j]>=0){touch.add(own[j]);continue}if(wall[j]){dd[j]=dd[i]+1;fr.push(j)}}}
     if(!touch.size)continue;const cx=sx/n,cy=sy/n;let best=-1,bd=1e18;
     for(const k of touch){const L=list[k],d=(L.x-cx)**2+(L.y-cy)**2;if(d<bd){bd=d;best=k}}
     for(let k=0;k<n;k++)own[pk[k]]=best}}
  // grow every area into the walls and a little beyond (the walls' own thickness plus a click margin)
  let h=0,t=0;for(let i=0;i<N;i++)if(own[i]>=0)q[t++]=i;
  const dist=new Uint16Array(N);
  while(h<t){const i=q[h++],x=i%cw;if(dist[i]>=grow)continue;
    for(const j of [x>0?i-1:-1,x<cw-1?i+1:-1,i-cw,i+cw])if(j>=0&&j<N&&own[j]<0){own[j]=own[i];dist[j]=dist[i]+1;q[t++]=j}}
  for(const L of list)if(L.leak){const R=Math.round(0.03*M/s);
    for(let dy=-R;dy<=R;dy++)for(let dx=-R;dx<=R;dx++){const x=L.x+dx,y=L.y+dy;
      if(x>=0&&y>=0&&x<cw&&y<ch&&dx*dx+dy*dy<=R*R&&own[y*cw+x]<0)own[y*cw+x]=L.k}}
  return regCache[fi]={s,cw,ch,own,list,F,img:{}};
}
function roomAt(fi,x,y){const g=regionsFor(fi),i=Math.round((y-g.F.y0)/g.s+1)*g.cw+Math.round((x-g.F.x0)/g.s+1);
  const k=g.own[i];return k>=0&&k!==undefined?g.list[k].n:0}
function highlight(fi,n){  // the room's area as a translucent image over the plan
  const g=regionsFor(fi);if(g.img[n])return g.img[n];const L=g.list.find(l=>l.n===n);if(!L)return"";
  const cv=document.createElement("canvas");cv.width=g.cw;cv.height=g.ch;const c=cv.getContext("2d"),im=c.createImageData(g.cw,g.ch);
  for(let i=0;i<g.own.length;i++)if(g.own[i]===L.k){im.data[i*4]=29;im.data[i*4+1]=78;im.data[i*4+2]=216;im.data[i*4+3]=70}
  c.putImageData(im,0,0);return g.img[n]=cv.toDataURL();
}
function unitsIn(fi,n){return units.filter(u=>{const p=where(u);return p&&p.f===fi&&(u.room===n||roomAt(fi,p.x,p.y)===n)})}

let hot=0,hotF=0,hotUs=[],actHtml=null;  // room shown highlighted (picked, talking or ringing), its floor; the action row shown
const fsz=F=>Math.max(F.x1-F.x0,F.y1-F.y0);
function full(){
  actHtml=null;
  if(!plan){box.innerHTML=`<div class="bar"><b>Floor plan</b><button id="pload">Load a DXF floor plan…</button><button id="pdemo">Show a demo plan</button>
    <span class="hint">Shows every unit on its room. The file stays in this browser only: it is not sent to the unit.</span>${msg?`<span class="hint">${esc(msg)}</span>`:""}</div>`;
    hook();return}
  box.innerHTML=`<div class="bar"><b>Floor plan</b>${window.onPlanAll?`<button id="pall" class="big">Hold to talk to the whole building</button>`:""}<span id="pact"></span>
    <span style="margin-left:auto"></span><button id="pmove">Move dots</button><button id="pload">Load another DXF…</button><button id="pdel">Remove</button></div>
    <div id="phint"></div><div class="floors">`+plan.floors.map((F,i)=>{const w=F.x1-F.x0,h=F.y1-F.y0,m=fsz(F)*0.03,g=regionsFor(i);
      return `<figure class="fl"><figcaption id="pcap${i}"></figcaption>
      <svg id="psvg${i}" data-f="${i}" viewBox="${F.x0-m} ${F.y0-m} ${w+2*m} ${h+2*m}" preserveAspectRatio="xMidYMid meet" style="touch-action:none">
      <image id="phl${i}" x="${F.x0-g.s}" y="${F.y0-g.s}" width="${g.cw*g.s}" height="${g.ch*g.s}" preserveAspectRatio="none"/>
      <path class="wall" d="${F.d}" stroke-width="${fsz(F)*0.0015}"/><g id="pdots${i}"></g></svg></figure>`}).join("")+`</div>`;
  hook();part();
}
function holdButton(el,on,off){  // a button that acts while it is held
  if(!el)return;let held=false;
  el.addEventListener("pointerdown",ev=>{ev.preventDefault();try{el.setPointerCapture(ev.pointerId)}catch(e){}held=true;el.classList.add("sel");on()});
  for(const e of ["pointerup","pointercancel","lostpointercapture"])el.addEventListener(e,()=>{if(!held)return;held=false;el.classList.remove("sel");off()});
}
function part(){  // what changes every refresh: captions, hint, dots, highlight (the drawings themselves stay)
  if(!plan||!document.getElementById("psvg0"))return full();
  const fc=plan.floors.map(()=>({n:0,worst:"ok"}));const un=[];
  for(const u of units){const p=where(u);if(!p){un.push(u);continue}const c=fc[p.f];if(!c)continue;c.n++;if(rank[cls(u)]>rank[c.worst])c.worst=cls(u)}
  plan.floors.forEach((F,fi)=>{const r=fsz(F)*0.011;
    document.getElementById("pcap"+fi).innerHTML=`${esc(F.name)} · ${fc[fi].n}${fc[fi].worst!=="ok"?` <span style="color:var(--${fc[fi].worst==="talk"?"talk":fc[fi].worst})">●</span>`:""}`;
    const spots={};for(const u of units){const p=where(u);if(!p||p.f!==fi)continue;const k=p.x+","+p.y;(spots[k]=spots[k]||[]).push(u)}
    let dots="";
    for(const k in spots){const us=spots[k],p=where(us[0]);us.forEach((u,i)=>{const x=p.x+(i-(us.length-1)/2)*r*2.3,y=p.y+r*1.6;
      dots+=`<g data-u="${u.unit}"><circle class="dot ${cls(u)}" cx="${x}" cy="${y}" r="${r}" stroke-width="${r*0.18}"${sel===String(u.unit)?` style="stroke:var(--call);stroke-width:${r*0.45}"`:""}><title>${esc(u.unit)}</title></circle>`+
        `<text class="num" x="${x}" y="${y+r*0.36}" font-size="${r*0.95}" text-anchor="middle">${esc(u.unit)}</text></g>`})}
    document.getElementById("pdots"+fi).innerHTML=dots;
    const hl=document.getElementById("phl"+fi),href=hot&&hotF===fi?highlight(fi,hot):"";if(hl.getAttribute("href")!==href)hl.setAttribute("href",href)});
  // the picked room's own buttons (drawn only when they change, so a held button is never replaced)
  const who=hotUs.length?"intercom "+hotUs.map(u=>esc(u.unit)).join(", "):"room "+hot;
  const a=hot&&window.onPlanHold&&!sel&&!moving?`<button id="ptalk" class="big">Hold to talk to ${who}</button><button id="pring">Ring ${who}</button>`:"";
  if(a!==actHtml){actHtml=a;const e=document.getElementById("pact");e.innerHTML=a;const n=hot,f=hotF,us=hotUs,inf=()=>({room:n,units:us.length?us:unitsIn(f,n)});
    holdButton(document.getElementById("ptalk"),()=>window.onPlanHold(inf()),()=>{if(window.onPlanHoldEnd)window.onPlanHoldEnd(inf())});
    const pr=document.getElementById("pring");if(pr)pr.onclick=()=>{if(window.onPlanRing)window.onPlanRing(inf())}}
  const chips=un.map(u=>`<button class="chip ${sel===String(u.unit)?"sel":""}" data-u="${u.unit}">${esc(u.unit)}</button>`).join("");
  const act=window.onPlanHold?"Click an intercom's dot or its room to pick it, hold it to talk to it, double-click it to ring it.":"Click a room to pick it.";
  document.getElementById("phint").innerHTML=
    sel?`<div class="hint"><b>Unit ${esc(sel)}:</b> click the map where it is. <button id="pcancel">Cancel</button>${place[sel]?` <button id="preset">Back to its room</button>`:""}</div>`:
    moving?`<div class="hint">Click a dot, then click where it goes.</div>`:
    un.length?`<div class="hint">${act} Not on the plan yet: ${chips} pick one, then click the map.</div>`:`<div class="hint">${act}</div>`;
  const mv=document.getElementById("pmove");mv.textContent=moving?"Done moving":"Move dots";mv.className=moving?"sel":"";
  if(msg)document.getElementById("phint").innerHTML+=`<div class="hint">${esc(msg)}</div>`;
  const b=id=>document.getElementById(id);
  if(b("pcancel"))b("pcancel").onclick=()=>{sel=null;part()};
  if(b("preset"))b("preset").onclick=()=>{delete place[sel];save(KU,place);sel=null;part()};
  box.querySelectorAll("button.chip").forEach(t=>t.onclick=()=>{sel=t.dataset.u;part()});
}
function hook(){
  const fi=document.getElementById("pfile")||Object.assign(document.createElement("input"),{type:"file",accept:".dxf",id:"pfile",hidden:true});
  if(!fi.parentNode){document.body.appendChild(fi);fi.addEventListener("change",()=>{const f=fi.files[0];if(!f)return;
    const rd=new FileReader();rd.onload=()=>loadText(rd.result);rd.readAsText(f);fi.value=""})}
  const b=id=>document.getElementById(id);
  if(b("pload"))b("pload").onclick=()=>fi.click();
  if(b("pdemo"))b("pdemo").onclick=showDemo;
  if(b("pmove"))b("pmove").onclick=()=>{moving=!moving;sel=null;part()};
  if(b("pdel"))b("pdel").onclick=()=>{if(confirm("Remove the floor plan from this browser? Unit positions you placed are kept.")){plan=null;try{localStorage.removeItem(KP)}catch(e){}full()}};
  if(window.onPlanAll)holdButton(b("pall"),()=>window.onPlanAll(true),()=>window.onPlanAll(false));
  box.querySelectorAll("svg[data-f]").forEach(svg=>{const fi=+svg.dataset.f;
  const at=ev=>{const pt=svg.createSVGPoint();pt.x=ev.clientX;pt.y=ev.clientY;return pt.matrixTransform(svg.getScreenCTM().inverse())};
  // Gestures (not while placing or moving dots): click = pick the room; hold = talk to it while held;
  // two quick clicks = ring it. The school page supplies onPlanPick / onPlanHold / onPlanHoldEnd / onPlanRing.
  let down=null,holdT=0,holding=null,last=null;
  let pick=[];  // the intercom(s) the last press was on
  const info=n=>({room:n,units:pick.length?pick:unitsIn(fi,n)});
  const roomFrom=ev=>{pick=[];const g=ev.target.closest&&ev.target.closest("g[data-u]");
    if(g){const u=units.find(x=>String(x.unit)===g.dataset.u);if(u){const p=where(u);pick=[u];return roomAt(fi,p.x,p.y)||u.room}}  // a dot: that intercom
    const q=at(ev),n=roomAt(fi,q.x,q.y);if(n&&unitsIn(fi,n).length)return n;
    let best=null,bd=fsz(plan.floors[fi])*0.06;  // no intercom of its own there (a closet, a corridor): the nearest one, if close
    for(const u of units){const p=where(u);if(!p||p.f!==fi)continue;const d=Math.hypot(p.x-q.x,p.y-q.y);if(d<bd){bd=d;best=u}}
    if(best){const p=where(best);pick=[best];return roomAt(fi,p.x,p.y)||best.room}
    return n};
  const endHold=()=>{clearTimeout(holdT);if(holding){const h=holding;holding=null;if(window.onPlanHoldEnd)window.onPlanHoldEnd(h)}};
  svg.addEventListener("pointerdown",ev=>{
    if(sel||moving)return;const n=roomFrom(ev);if(!n)return;
    ev.preventDefault();try{svg.setPointerCapture(ev.pointerId)}catch(e){}down={n,t:Date.now()};hot=n;hotF=fi;hotUs=info(n).units;part();
    if(window.onPlanHold)holdT=setTimeout(()=>{if(down&&down.n===n){holding=info(n);down=null;window.onPlanHold(holding)}},350)});
  svg.addEventListener("pointerup",ev=>{
    if(holding){endHold();return}clearTimeout(holdT);if(!down)return;const n=down.n;down=null;
    if(last&&last.n===n&&Date.now()-last.t<450&&window.onPlanRing){last=null;window.onPlanRing(info(n));return}
    last={n,t:Date.now()};if(window.onPlanPick)window.onPlanPick(info(n))});
  for(const e of ["pointercancel","lostpointercapture"])svg.addEventListener(e,()=>{down=null;endHold()});
  svg.addEventListener("click",ev=>{  // placing or moving a dot
    if(!sel&&!moving)return;
    const g=ev.target.closest&&ev.target.closest("g[data-u]");
    if(g&&!sel){sel=g.dataset.u;part();return}
    if(!sel)return;
    const q=at(ev),F=plan.floors[fi];  // dots sit a little below their spot: store the spot
    place[sel]={f:fi,x:Math.round(q.x),y:Math.round(q.y-Math.max(F.x1-F.x0,F.y1-F.y0)*0.011*1.6)};
    save(KU,place);sel=null;part()});
  });
}
function render(){full()}
// A made-up plan from the units online (a corridor of rooms per floor): to try the map without a DXF,
// and for the videos (the school's own plan must not be published).
function demoDxf(){
  const rooms=[...new Set(units.map(u=>u.room).filter(r=>r>=100))].sort((a,b)=>a-b);
  if(!rooms.length)throw new Error("no units online yet to lay out");
  const fl={};for(const r of rooms)(fl[Math.floor(r/100)]=fl[Math.floor(r/100)]||[]).push(r);
  const W=360,D=300,C=96;let o="0\nSECTION\n2\nENTITIES\n";
  const L=(lay,x1,y1,x2,y2)=>{o+=`0\nLINE\n8\n${lay}\n10\n${x1}\n20\n${y1}\n11\n${x2}\n21\n${y2}\n`};
  const T=(lay,x,y,s)=>{o+=`0\nTEXT\n8\n${lay}\n10\n${x}\n20\n${y}\n40\n24\n1\n${s}\n`};
  for(const f in fl){
    const rs=fl[f],lay="demo"+f,half=Math.ceil(rs.length/2),len=half*W,M=Math.max(len,2*D+C),g=Math.max(2,0.004*M);
    rs.forEach((r,i)=>{const top=i<half,x=(top?i:i-half)*W,y0=top?C/2:-C/2,y1=top?C/2+D:-C/2-D,mid=x+W/2;
      L(lay,x,y0,x,y1);L(lay,x+W,y0,x+W,y1);L(lay,x,y1,x+W,y1);L(lay,x,y0,mid-g,y0);L(lay,mid+g,y0,x+W,y0);
      T(lay,mid-60,(y0+y1)/2,"ROOM "+r)});
    const bl=(rs.length-half)*W;if(bl<len)L(lay,bl,-C/2,len,-C/2);  // a shorter bottom row: close the corridor
    L(lay,0,-C/2,0,C/2);L(lay,len,-C/2,len,C/2);
  }
  return o+"0\nENDSEC\n0\nEOF\n";
}
function showDemo(){
  try{loadText(demoDxf());msg="A demo plan made from the units online, not your building: load your DXF to replace it."}
  catch(e){msg="No demo plan: "+e.message}
  part();
}
// Scripted gestures for the videos (no mouse is recorded): a tap mark shows where the "finger" is.
let tapSvg=null;
function tapAt(n,ms){
  const L=plan&&plan.rooms[n];if(!L)return null;
  const svg=tapSvg=document.getElementById("psvg"+L.f),F=plan.floors[L.f],r=Math.max(F.x1-F.x0,F.y1-F.y0)*0.02;
  const x=L.x+r*3,y=L.y,c=document.createElementNS("http://www.w3.org/2000/svg","circle");
  c.setAttribute("cx",x);c.setAttribute("cy",y);c.setAttribute("r",r);c.setAttribute("fill","rgba(200,40,40,.45)");
  c.setAttribute("pointer-events","none");svg.appendChild(c);setTimeout(()=>c.remove(),ms);
  const p=svg.createSVGPoint();p.x=x;p.y=y;const q=p.matrixTransform(svg.getScreenCTM());return {clientX:q.x,clientY:q.y};
}
function fire(t,c){tapSvg.dispatchEvent(new PointerEvent(t,Object.assign({bubbles:true,pointerId:1,isPrimary:true},c)))}
window.planTest={demo:showDemo,
  press(n,ms){const c=tapAt(n,ms+200);if(!c)return;fire("pointerdown",c);setTimeout(()=>fire("pointerup",c),ms)},
  dbl(n){const c=tapAt(n,700);if(!c)return;fire("pointerdown",c);
    setTimeout(()=>fire("pointerup",c),80);setTimeout(()=>fire("pointerdown",c),220);setTimeout(()=>fire("pointerup",c),300)}};
function loadText(txt){
  try{const p=build(parseDxf(txt));plan=p;floor=0;save(KF,0);for(const k in regCache)delete regCache[k];
    msg=save(KP,p)?"":"Loaded, but too big to keep in this browser: load it again next time.";
  }catch(e){msg="Couldn't read that DXF: "+e.message}
  full();
}
window.planUpdate=us=>{units=us||[];part()};
window.planHot=n=>{hot=n||0;hotUs=[];if(hot&&plan&&plan.rooms[hot])hotF=plan.rooms[hot].f;part()};  // the page can highlight a room (e.g. while ringing it)
window.planLoadText=loadText;  // also used by the scripted check
window.planRoomAt=(fi,x,y)=>roomAt(fi,x,y);  // scripted check
full();
})();)JS";
}

}  // namespace ic
