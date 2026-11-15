/* ---- Noise helper (used only to draw the lunar surface) ---- */
function noiseField(W,H,seed){
  let s=seed+1; const rnd=()=>((s=(s*16807)%2147483647)/2147483647);
  const out=new Float32Array(W*H); let amp=1,tot=0;
  for(let o=0;o<4;o++){
    const cell=40>>o,gw=Math.ceil(W/cell)+2,gh=Math.ceil(H/cell)+2,g=[];
    for(let i=0;i<gw*gh;i++)g.push(rnd());
    for(let y=0;y<H;y++)for(let x=0;x<W;x++){
      const fx=x/cell,fy=y/cell,ix=fx|0,iy=fy|0,tx=fx-ix,ty=fy-iy,sx=tx*tx*(3-2*tx),sy=ty*ty*(3-2*ty);
      const a=g[iy*gw+ix],b=g[iy*gw+ix+1],c=g[(iy+1)*gw+ix],d=g[(iy+1)*gw+ix+1],top=a+(b-a)*sx;
      out[y*W+x]+=amp*(top+((c+(d-c)*sx)-top)*sy);
    }
    tot+=amp;amp*=.5;
  }
  let mn=1e9,mx=-1e9;for(const v of out){mn=Math.min(mn,v/tot);mx=Math.max(mx,v/tot);}
  for(let i=0;i<out.length;i++)out[i]=(out[i]/tot-mn)/(mx-mn);
  return out;
}