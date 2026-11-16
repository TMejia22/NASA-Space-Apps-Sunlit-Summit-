async function requestRaster(endpoint,params,kind){
  if(!BACKEND_URL)throw new Error('Analysis API is not configured.');
  const response=await fetch(endpoint,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(params)});
  if(!response.ok){
    const error=await response.json().catch(()=>({}));
    throw new Error(error.error||'Backend returned '+response.status);
  }
  const image=await createImageBitmap(await response.blob());
  const width=Number(response.headers.get('X-Raster-Width'));
  const height=Number(response.headers.get('X-Raster-Height'));
  const totalPixels=Number(response.headers.get('X-Total-Pixels'));
  const best={x:Number(response.headers.get('X-Best-X')),y:Number(response.headers.get('X-Best-Y'))};
  const validDimensions=Number.isInteger(width)&&Number.isInteger(height)&&width>0&&height>0&&
    image.width===width&&image.height===height&&totalPixels===width*height&&
    Number.isInteger(best.x)&&Number.isInteger(best.y)&&
    best.x>=0&&best.x<width&&best.y>=0&&best.y<height;
  let coverage={};
  if(kind==='global'){
    const gradedPixels=Number(response.headers.get('X-Graded-Pixels'));
    const polarScoredPixels=Number(response.headers.get('X-Polar-Scored-Pixels'));
    const fallbackPixels=Number(response.headers.get('X-Fallback-Pixels'));
    if(!Number.isInteger(gradedPixels)||gradedPixels!==totalPixels||
       !Number.isInteger(polarScoredPixels)||polarScoredPixels<0||
       polarScoredPixels>totalPixels||!Number.isInteger(fallbackPixels)||
       fallbackPixels!==totalPixels-polarScoredPixels){
      image.close();
      throw new Error('Global analysis returned invalid coverage metadata.');
    }
    coverage={gradedPixels,polarScoredPixels,fallbackPixels};
  }else{
    const validPixels=Number(response.headers.get('X-Valid-Pixels'));
    if(!Number.isInteger(validPixels)||validPixels<1||validPixels>totalPixels){
      image.close();
      throw new Error('Finalist analysis returned invalid coverage metadata.');
    }
    coverage={validPixels};
  }
  if(!validDimensions){
    image.close();
    throw new Error('Analysis returned invalid raster dimensions.');
  }
  return {
    width,height,image,totalPixels,...coverage,best,
    siteId:response.headers.get('X-Site-ID')||'',
    siteName:response.headers.get('X-Site-Name')||'',
    geoTiffUrl:response.headers.get('X-GeoTIFF-URL')||'',
    note:response.headers.get('X-Scoring-Note')||''
  };
}

const SiteAnalysis = {
  run(params){return requestRaster(BACKEND_URL,params,'global');},
  runSite(params,site){return requestRaster('/api/analyze-site?site='+encodeURIComponent(site),params,'site');}
};

/* ---- Site suitability criteria ---- */
const CRITERIA=[
  {id:'sunlight',name:'Sunlight',          hint:'Share of time the ground is lit', def:27},
  {id:'earth',   name:'Earth visibility',  hint:'Direct line of sight for comms',  def:18},
  {id:'ice',     name:'Near water-ice',    hint:'Close to shadowed ice deposits',  def:22},
  {id:'temp',    name:'Temperature balance (proxy)',hint:'Estimated from blended sunlight and shadow proximity; favors a moderate balance, not measured thermal stability',def:13},
  {id:'smooth',  name:'Smooth terrain',    hint:'Few boulders and craters',        def:20}
];