const SiteAnalysis = {
  async run(params){
    if(BACKEND_URL){
      const r=await fetch(BACKEND_URL,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(params)});
      if(!r.ok) throw new Error('Backend returned '+r.status);
      const d=await r.json(); d.values=Float32Array.from(d.values,v=>v==null?NaN:v); return d;
    }
    // TODO: call your analysis function here and return {width,height,values}.
    return null;   // null = nothing connected yet; the map shows the lunar surface only
  }
};

/* ---- Criteria and your six candidate regions ---- */
const CRITERIA=[
  {id:'sunlight',name:'Sunlight',          hint:'Share of time the ground is lit', def:27},
  {id:'earth',   name:'Earth visibility',  hint:'Direct line of sight for comms',  def:18},
  {id:'ice',     name:'Near water-ice',    hint:'Close to shadowed ice deposits',  def:22},
  {id:'temp',    name:'Stable temperature',hint:'Smaller swings are better',     def:13},
  {id:'smooth',  name:'Smooth terrain',    hint:'Few boulders and craters',        def:20}
];
/* Placeholder positions on the demo grid. Replace with real grid coordinates. */
const REGIONS=[
  {name:'Site Sector',          x:40, y:30},
  {name:'Connecting ridge',     x:105,y:28},
  {name:'Shackleton rim',       x:80, y:60},
  {name:'Peak near Shackleton', x:125,y:75},
  {name:'de Gerlache rim',      x:35, y:85},
  {name:'Malapert massif',      x:90, y:100}
];