/* ---- Lunar surface: grey regolith with craters, drawn once per grid size ---- */
function buildTerrain(W,H){
  const S=4,w=W*S,h=H*S,c=document.createElement('canvas');c.width=w;c.height=h;c.gridW=W;c.gridH=H;
  const g=c.getContext('2d'),n=noiseField(w,h,7),img=g.createImageData(w,h);
  for(let i=0;i<n.length;i++){const v=78+n[i]*70;img.data.set([v,v,v+3,255],i*4);}
  g.putImageData(img,0,0);
  let s=4242;const rnd=()=>((s=(s*16807)%2147483647)/2147483647);
  for(let k=0;k<90;k++){
    const r=5+Math.pow(rnd(),2.6)*46,x=rnd()*w,y=rnd()*h,gr=g.createRadialGradient(x,y,0,x,y,r);
    gr.addColorStop(0,'rgba(0,0,0,.30)');gr.addColorStop(.72,'rgba(0,0,0,.22)');
    gr.addColorStop(.9,'rgba(255,255,255,.34)');gr.addColorStop(1,'rgba(255,255,255,0)');
    g.fillStyle=gr;g.beginPath();g.arc(x,y,r,0,6.283);g.fill();
  }
  return c;
}
$('overlay').addEventListener('input',e=>{OV=e.target.value/100;$('ovVal').textContent=e.target.value+'%';draw();});

/* ---- Map window: pan and zoom ---- */
const cv=$('map'),ctx=cv.getContext('2d'),cross=$('crosshair');
let dpr=1,result=null,off=null,best=null,terrain=null,OV=.6,view={s:1,x:0,y:0};
function resize(){dpr=devicePixelRatio||1;const r=cv.getBoundingClientRect();cv.width=r.width*dpr;cv.height=r.height*dpr;if(result&&!view.user)fit();draw();}
new ResizeObserver(resize).observe(cv.parentElement);
function fit(){if(!result)return;const r=cv.getBoundingClientRect(),s=Math.min(r.width/result.width,r.height/result.height)*.94;
  view={s,x:(r.width-result.width*s)/2,y:(r.height-result.height*s)/2,user:false};}
function draw(){
  const r=cv.getBoundingClientRect();ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,r.width,r.height);
  if(!result){cross.style.display='none';return;}
  ctx.imageSmoothingEnabled=true;
  if(terrain)ctx.drawImage(terrain,view.x,view.y,result.width*view.s,result.height*view.s);
  if(off){ctx.globalAlpha=OV;ctx.drawImage(off,view.x,view.y,result.width*view.s,result.height*view.s);ctx.globalAlpha=1;}
  ctx.lineJoin='round';ctx.font='12px Helvetica,Arial,sans-serif';
  REGIONS.forEach(g=>{const cx=view.x+(g.x+.5)*view.s,cy=view.y+(g.y+.5)*view.s;
    ctx.fillStyle='#fff';ctx.beginPath();ctx.arc(cx,cy,3,0,6.283);ctx.fill();
    ctx.lineWidth=3;ctx.strokeStyle='rgba(0,0,0,.75)';ctx.strokeText(g.name,cx+8,cy+4);ctx.fillStyle='#fff';ctx.fillText(g.name,cx+8,cy+4);});
  if(best){cross.style.display='block';
    cross.style.left=(view.x+(best.x+.5)*view.s-22)+'px';cross.style.top=(view.y+(best.y+.5)*view.s-22)+'px';}
  else cross.style.display='none';
}
function zoomAt(px,py,f){const ns=Math.max(.5,Math.min(80,view.s*f));f=ns/view.s;view.x=px-(px-view.x)*f;view.y=py-(py-view.y)*f;view.s=ns;view.user=true;draw();}
const mid=()=>{const r=cv.getBoundingClientRect();return[r.width/2,r.height/2];};
function goTo(p){const r=cv.getBoundingClientRect();view.s=Math.max(view.s,10);view.x=r.width/2-(p.x+.5)*view.s;view.y=r.height/2-(p.y+.5)*view.s;view.user=true;draw();}
$('zin').onclick=e=>{e.preventDefault();zoomAt(...mid(),1.4);};
$('zout').onclick=e=>{e.preventDefault();zoomAt(...mid(),1/1.4);};
$('fit').onclick=e=>{e.preventDefault();fit();draw();};
$('goBest').onclick=e=>{e.preventDefault();if(best)goTo(best);};
cv.addEventListener('wheel',e=>{e.preventDefault();const r=cv.getBoundingClientRect();zoomAt(e.clientX-r.left,e.clientY-r.top,Math.exp(-e.deltaY*.0015));},{passive:false});
let drag=null;
cv.addEventListener('pointerdown',e=>{cv.setPointerCapture(e.pointerId);drag={x:e.clientX,y:e.clientY,vx:view.x,vy:view.y};cv.classList.add('drag');});
cv.addEventListener('pointermove',e=>{
  if(drag){view.x=drag.vx+e.clientX-drag.x;view.y=drag.vy+e.clientY-drag.y;view.user=true;draw();}
  if(result){const r=cv.getBoundingClientRect(),gx=Math.floor((e.clientX-r.left-view.x)/view.s),gy=Math.floor((e.clientY-r.top-view.y)/view.s);
    if(gx>=0&&gy>=0&&gx<result.width&&gy<result.height)$('readout').textContent=`Column ${gx}, row ${gy}`;
    else $('readout').textContent='Drag to pan, scroll to zoom';}
});
cv.addEventListener('pointerup',()=>{drag=null;cv.classList.remove('drag');});
addEventListener('keydown',e=>{if(e.target.tagName==='INPUT')return;const k=e.key;
  if(k==='+'||k==='=')$('zin').click();else if(k==='-')$('zout').click();else if(k==='0')$('fit').click();
  else if(k.startsWith('Arrow')){view.x+=k==='ArrowLeft'?40:k==='ArrowRight'?-40:0;view.y+=k==='ArrowUp'?40:k==='ArrowDown'?-40:0;view.user=true;draw();}});
