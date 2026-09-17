const finite=n=>Number.isFinite(n),clamp=(n,a,b)=>Math.max(a,Math.min(b,n));
export const altitudeUsable=s=>s?.gnss==='valid'&&finite(s.altitudeMslM)&&finite(s.verticalAccuracyM)&&s.verticalAccuracyM<=5;
export function interpolateSample(a,b,time){
  const f=clamp((time-a.time)/(b.time-a.time||1),0,1),out={...a,time};
  for(const key of ['speed','longitudinal','lateral','altitudeMslM','verticalAccuracyM','distanceM'])
    if(finite(a[key])&&finite(b[key]))out[key]=a[key]+(b[key]-a[key])*f;
  return out;
}
export function sliceSamples(samples,start,end){
  const body=samples.filter(s=>s.time>=start&&s.time<=end),before=[...samples].reverse().find(s=>s.time<start),after=samples.find(s=>s.time>start),last=[...samples].reverse().find(s=>s.time<end),next=samples.find(s=>s.time>end);
  if(before&&after)body.unshift(interpolateSample(before,after,start));
  if(last&&next)body.push(interpolateSample(last,next,end));
  return body;
}
function regression(points){
  if(points.length<2)return null;
  const mx=points.reduce((n,p)=>n+p.x,0)/points.length,my=points.reduce((n,p)=>n+p.y,0)/points.length;
  const den=points.reduce((n,p)=>n+(p.x-mx)**2,0);return den?points.reduce((n,p)=>n+(p.x-mx)*(p.y-my),0)/den:null;
}
export function roadProfile(samples){
  const valid=samples.filter(altitudeUsable),first=valid[0];
  if(!first)return {available:false,hasAltitude:false,points:samples.map(s=>({...s,relativeAltitudeM:null,slopePercent:null,provisionalSlopePercent:null}))};
  const last=valid.at(-1),distance=(last.distanceM??0)-(first.distanceM??0),hasAltitude=true,available=valid.length>=2&&distance>=30;
  const result=samples.map(s=>{
    if(!altitudeUsable(s))return {...s,relativeAltitudeM:null,slopePercent:null,provisionalSlopePercent:null};
    const around=valid.filter(p=>Math.abs((p.distanceM??0)-(s.distanceM??0))<=25);
    const span=around.length?(around.at(-1).distanceM-around[0].distanceM):0;
    const slope=regression(around.map(p=>({x:p.distanceM,y:p.altitudeMslM}))),slopePercent=slope!==null&&span>=20?slope*100:null;
    return {...s,relativeAltitudeM:s.altitudeMslM-first.altitudeMslM,slopePercent,provisionalSlopePercent:slopePercent===null&&slope!==null?slope*100:null};
  });
  const average=available?(last.altitudeMslM-first.altitudeMslM)/distance*100:null;
  const slopes=result.map(s=>s.slopePercent).filter(finite);
  return {available,hasAltitude,startAltitudeM:first.altitudeMslM,endAltitudeM:last.altitudeMslM,deltaAltitudeM:last.altitudeMslM-first.altitudeMslM,distanceM:distance,averageSlopePercent:average,maxUphillPercent:slopes.length?Math.max(...slopes):null,maxDownhillPercent:slopes.length?Math.min(...slopes):null,points:result};
}
export function summary(samples){
  const profile=roadProfile(samples),speed=samples.map(s=>s.speed).filter(finite),long=samples.map(s=>s.longitudinal).filter(finite);
  return {...profile,maxSpeed:Math.max(0,...speed),peakAcceleration:Math.max(0,...long),peakBraking:Math.min(0,...long)};
}
