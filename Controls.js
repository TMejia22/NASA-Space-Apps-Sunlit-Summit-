/* ---- Controls ---- */
const $=id=>document.getElementById(id);
const weights={}, locked={}; CRITERIA.forEach(c=>{weights[c.id]=c.def;locked[c.id]=false;});
CRITERIA.forEach(c=>{
  const d=document.createElement('div'); d.className='crit';
  d.innerHTML=`<div class="crit-head"><label for="w_${c.id}">${c.name}</label><span class="pct" id="p_${c.id}"></span>
      <button type="button" class="lock" id="l_${c.id}" aria-pressed="false" title="Keep this weight fixed">Lock</button></div>
    <small>${c.hint}</small><input type="range" id="w_${c.id}" min="0" max="100" value="${c.def}">`;
  $('controls').appendChild(d);
  d.querySelector('input').addEventListener('input',e=>{setWeight(c.id,+e.target.value);labels();if($('live').checked)schedule();});
  $('l_'+c.id).onclick=()=>{locked[c.id]=!locked[c.id];labels();};
});

/* Keeps the weights adding up to 100.
   The moved slider takes its new value (capped so locked sliders keep theirs);
   the remaining unlocked sliders share what is left, in proportion to their current size. */
function setWeight(id,v){
  const others=CRITERIA.map(c=>c.id).filter(k=>k!==id);
  const L=others.filter(k=>locked[k]).reduce((s,k)=>s+weights[k],0);
  const U=others.filter(k=>!locked[k]);
  v=Math.round(Math.max(0,Math.min(100-L,v)));
  if(!U.length) v=100-L;
  const rest=100-L-v, S=U.reduce((s,k)=>s+weights[k],0);
  const raw=U.map(k=>S>0?weights[k]/S*rest:rest/U.length);
  const fl=raw.map(Math.floor); let left=rest-fl.reduce((a,b)=>a+b,0);
  raw.map((r,i)=>[r-fl[i],i]).sort((a,b)=>b[0]-a[0]).forEach(p=>{if(left>0){fl[p[1]]++;left--;}});
  weights[id]=v; U.forEach((k,i)=>weights[k]=fl[i]);
}

$('slopeLimit').addEventListener('input',()=>{labels();if($('live').checked)schedule();});
const total=()=>Object.values(weights).reduce((a,b)=>a+b,0);
function normalized(){const t=total()||1,o={};for(const k in weights)o[k]=weights[k]/t;return o;}

function labels(){
  const free=CRITERIA.filter(c=>!locked[c.id]).length;
  CRITERIA.forEach(c=>{
    $('p_'+c.id).textContent=weights[c.id]+'%';
    const inp=$('w_'+c.id); inp.value=weights[c.id];
    inp.disabled=locked[c.id]||free<2;
    inp.title=locked[c.id]?'Locked: unlock to change':(free<2?'Unlock another slider to change this one':'');
    const b=$('l_'+c.id); b.textContent=locked[c.id]?'Locked':'Lock'; b.setAttribute('aria-pressed',locked[c.id]);
  });
  $('slopeVal').textContent=$('slopeLimit').value+'°';
  const nl=CRITERIA.length-free;
  $('sTotal').textContent='100%'+(nl?' ('+nl+' locked)':'');
}

$('reset').onclick=()=>{CRITERIA.forEach(c=>{weights[c.id]=c.def;locked[c.id]=false;});$('slopeLimit').value=10;labels();run();};

/* ---- Colour ramp ---- */
const STOPS=[[30,20,80],[50,80,150],[30,150,150],[140,205,100],[250,235,90]];
function ramp(t){t=Math.max(0,Math.min(1,t))*(STOPS.length-1);const i=Math.min(STOPS.length-2,t|0),f=t-i,a=STOPS[i],b=STOPS[i+1];
  return [a[0]+(b[0]-a[0])*f,a[1]+(b[1]-a[1])*f,a[2]+(b[2]-a[2])*f];}
$('ramp').style.background='linear-gradient(90deg,'+STOPS.map(s=>`rgb(${s})`).join(',')+')';