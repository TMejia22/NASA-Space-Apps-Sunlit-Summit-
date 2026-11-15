/* ---- Render the result (if any) and list the regions ---- */
function render(res){
  result=res||{width:160,height:120,values:null};
  const {width:W,height:H,values}=result;off=null;best=res&&res.best?res.best:null;
  if(values){
    off=document.createElement('canvas');off.width=W;off.height=H;
    const c=off.getContext('2d'),img=c.createImageData(W,H);let mn=Infinity,mx=-Infinity;
    for(const v of values)if(Number.isFinite(v)){mn=Math.min(mn,v);mx=Math.max(mx,v);}
    const span=(mx-mn)||1;
    for(let i=0;i<values.length;i++){const v=values[i],col=Number.isFinite(v)?ramp((v-mn)/span):[0,0,0];
      img.data.set([col[0],col[1],col[2],255],i*4);}
    c.putImageData(img,0,0);
  }
  if(!terrain||terrain.gridW!==W||terrain.gridH!==H)terrain=buildTerrain(W,H);
  $('sLoc').textContent=best?best.x+', '+best.y:'–';
  rank();
}
function rank(){
  $('regions').innerHTML='';
  REGIONS.forEach(g=>{
    const sec=document.createElement('section');sec.className='factor';sec.tabIndex=0;sec.setAttribute('role','button');
    sec.innerHTML=`<h2>${g.name}</h2>`;
    sec.onclick=()=>goTo(g);sec.onkeydown=e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();goTo(g);}};
    $('regions').appendChild(sec);});
}
let timer=null,token=0;function schedule(){clearTimeout(timer);timer=setTimeout(run,120);}
async function run(){
  const my=++token,st=$('status');
  if(total()===0){st.style.display='block';st.textContent='Raise at least one weight above 0 to run the analysis.';return;}
  st.style.display='block';st.textContent='Running analysis…';
  try{
    const res=await SiteAnalysis.run({weights:{...weights},normalized:normalized(),limits:{maxSlope:+$('slopeLimit').value}});
    if(my!==token)return;render(res);if(!view.user)fit();draw();
    if(res)st.style.display='none';else st.textContent='No analysis function connected yet. Showing the lunar surface only.';
  }catch(err){st.textContent='Analysis failed: '+err.message+'. Check that your function or server is running.';}
}
$('run').onclick=run;
labels();resize();run();

