/* ---- Render global and finalist-site suitability results ---- */
let timer=null,token=0,analysisResult=null;

function setStatus(message){
  const status=$('status');
  status.textContent=message;
  status.style.display=message?'block':'none';
}
function updateAnalysisDetails(res){
  const best=res&&res.best?res.best:null;
  $('sLoc').textContent=best?best.x+', '+best.y:'–';
  const download=$('downloadResult');
  if(res&&res.geoTiffUrl){
    download.href=res.geoTiffUrl;
    download.download=res.siteId?'suitability_'+res.siteId+'.tif':'suitability_output.tif';
    download.hidden=false;
  }
  else download.hidden=true;
}
function render(res){
  const previous=analysisResult;
  analysisResult=res;
  updateAnalysisDetails(res);
  setMapImage(res.image,res.width,res.height,res.best);
  if(previous&&previous.image)previous.image.close();
}
function schedule(){clearTimeout(timer);timer=setTimeout(run,120);}
async function run(){
  const my=++token;
  const site=$('siteView').value;
  if(activeTotal()===0){
    setStatus('Raise at least one available criterion weight above 0 to run the analysis.');
    return;
  }
  setStatus(site==='global'?'Running global analysis…':'Scoring '+site+' at 5 m resolution…');
  try{
    const params={weights:{...weights},normalized:normalized(),limits:{maxSlope:+$('slopeLimit').value}};
    const res=site==='global'?await SiteAnalysis.run(params):await SiteAnalysis.runSite(params,site);
    if(my!==token){if(res.image)res.image.close();return;}
    render(res);
    setStatus(res.note||'');
  }catch(err){
    setStatus('Analysis failed: '+err.message+'. Check that your function or server is running.');
  }
}
$('run').onclick=run;
$('siteView').addEventListener('change',run);
labels();
resize();
run();
