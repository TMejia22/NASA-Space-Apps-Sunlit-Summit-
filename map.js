/* ---- Map window: pan and zoom ---- */
const cv=$('map'),ctx=cv.getContext('2d'),cross=$('crosshair');
let dpr=1,mapWidth=0,mapHeight=0,mapImage=null,best=null;
let view={s:1,x:0,y:0,user:false};

function resize(){
  dpr=devicePixelRatio||1;
  const r=cv.getBoundingClientRect();
  cv.width=r.width*dpr;
  cv.height=r.height*dpr;
  if(mapWidth&&!view.user)fit();
  draw();
}
new ResizeObserver(resize).observe(cv.parentElement);
function fit(){
  if(!mapWidth||!mapHeight)return;
  const r=cv.getBoundingClientRect();
  const s=Math.min(r.width/mapWidth,r.height/mapHeight)*.94;
  view={s,x:(r.width-mapWidth*s)/2,y:(r.height-mapHeight*s)/2,user:false};
}
function setMapImage(image,width,height,marker){
  mapImage=image;
  mapWidth=width;
  mapHeight=height;
  best=marker;
  view.user=false;
  fit();
  draw();
}
function draw(){
  const r=cv.getBoundingClientRect();
  ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.clearRect(0,0,r.width,r.height);
  if(!mapWidth||!mapHeight){cross.style.display='none';return;}
  ctx.imageSmoothingEnabled=true;
  if(mapImage)ctx.drawImage(mapImage,view.x,view.y,mapWidth*view.s,mapHeight*view.s);
  if(best){
    cross.style.display='block';
    cross.style.left=(view.x+(best.x+.5)*view.s-22)+'px';
    cross.style.top=(view.y+(best.y+.5)*view.s-22)+'px';
  }else cross.style.display='none';
}
function zoomAt(px,py,f){
  if(!mapWidth)return;
  const ns=Math.max(.5,Math.min(80,view.s*f));
  f=ns/view.s;
  view.x=px-(px-view.x)*f;
  view.y=py-(py-view.y)*f;
  view.s=ns;
  view.user=true;
  draw();
}
const mid=()=>{const r=cv.getBoundingClientRect();return[r.width/2,r.height/2];};
function goTo(p){
  const r=cv.getBoundingClientRect();
  view.s=Math.max(view.s,10);
  view.x=r.width/2-(p.x+.5)*view.s;
  view.y=r.height/2-(p.y+.5)*view.s;
  view.user=true;
  draw();
}
$('zin').onclick=e=>{e.preventDefault();zoomAt(...mid(),1.4);};
$('zout').onclick=e=>{e.preventDefault();zoomAt(...mid(),1/1.4);};
$('fit').onclick=e=>{e.preventDefault();fit();draw();};
$('goBest').onclick=e=>{e.preventDefault();if(best)goTo(best);};
cv.addEventListener('wheel',e=>{
  e.preventDefault();
  const r=cv.getBoundingClientRect();
  zoomAt(e.clientX-r.left,e.clientY-r.top,Math.exp(-e.deltaY*.0015));
},{passive:false});
let drag=null;
cv.addEventListener('pointerdown',e=>{
  if(!mapWidth)return;
  cv.setPointerCapture(e.pointerId);
  drag={x:e.clientX,y:e.clientY,vx:view.x,vy:view.y};
  cv.classList.add('drag');
});
cv.addEventListener('pointermove',e=>{
  if(drag){
    view.x=drag.vx+e.clientX-drag.x;
    view.y=drag.vy+e.clientY-drag.y;
    view.user=true;
    draw();
  }
  if(mapWidth){
    const r=cv.getBoundingClientRect();
    const gx=Math.floor((e.clientX-r.left-view.x)/view.s);
    const gy=Math.floor((e.clientY-r.top-view.y)/view.s);
    $('readout').textContent=gx>=0&&gy>=0&&gx<mapWidth&&gy<mapHeight
      ?`Column ${gx}, row ${gy}`:'Drag to pan, scroll to zoom';
  }
});
cv.addEventListener('pointerup',()=>{drag=null;cv.classList.remove('drag');});
addEventListener('keydown',e=>{
  if(e.target.tagName==='INPUT'||e.target.tagName==='SELECT')return;
  const k=e.key;
  if(k==='+'||k==='=')$('zin').click();
  else if(k==='-')$('zout').click();
  else if(k==='0')$('fit').click();
  else if(k.startsWith('Arrow')){
    view.x+=k==='ArrowLeft'?40:k==='ArrowRight'?-40:0;
    view.y+=k==='ArrowUp'?40:k==='ArrowDown'?-40:0;
    view.user=true;
    draw();
  }
});
