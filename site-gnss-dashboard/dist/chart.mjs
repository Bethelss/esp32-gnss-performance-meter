const finite=value=>Number.isFinite(value);

export function segments(samples){
  return seriesSegments(samples,'time','time',()=>true);
}

export function seriesSegments(samples,key,xKey='time',valid=finite){
  const groups=[];let group=[];let previous=null;
  for(const sample of samples){
    const usable=finite(sample?.[xKey])&&valid(sample?.[key]);
    if(!usable){if(group.length)groups.push(group);group=[];previous=null;continue;}
    if(previous&&sample.time-previous.time>.15){groups.push(group);group=[];}
    group.push(sample);previous=sample;
  }
  if(group.length)groups.push(group);
  return groups;
}

export function reducePoints(samples,width,key,xKey='time'){
  if(samples.length<=width*2)return samples;
  const buckets=new Map(),first=samples[0][xKey],span=samples.at(-1)[xKey]-first||1;
  const keep=new Set([samples[0],samples.at(-1)]);
  for(const sample of samples){
    const bucket=Math.floor((sample[xKey]-first)/span*width),pair=buckets.get(bucket)||[sample,sample];
    if(sample[key]<pair[0][key])pair[0]=sample;
    if(sample[key]>pair[1][key])pair[1]=sample;
    buckets.set(bucket,pair);
  }
  for(const pair of buckets.values())pair.forEach(sample=>keep.add(sample));
  return [...keep].sort((a,b)=>a[xKey]-b[xKey]);
}

export function cursorSample(samples,at){
  let lo=0,hi=samples.length;
  while(lo<hi){const mid=(lo+hi)>>1;if(samples[mid].time<=at)lo=mid+1;else hi=mid;}
  const sample=samples[Math.max(0,lo-1)];
  return !sample||at-sample.time>.15?null:sample;
}

export function niceDomain(values,{includeZero=false,symmetric=false,minSpan=0.1,ticks=4}={}){
  const usable=values.filter(finite);if(!usable.length)return null;
  let min=Math.min(...usable),max=Math.max(...usable),sourceMin=min;
  if(includeZero){min=Math.min(min,0);max=Math.max(max,0);}
  if(symmetric){const edge=Math.max(Math.abs(min),Math.abs(max),minSpan/2)*1.12;min=-edge;max=edge;}
  else{const span=Math.max(max-min,minSpan),pad=span*.12;min-=pad;max+=pad;if(includeZero&&sourceMin>=0)min=0;}
  const step=niceStep((max-min)/ticks);
  return {min:Math.floor(min/step)*step,max:Math.ceil(max/step)*step,step};
}

export function niceStep(raw){
  const power=10**Math.floor(Math.log10(Math.max(raw,Number.EPSILON))),ratio=raw/power;
  const base=ratio<=1?1:ratio<=2?2:ratio<=2.5?2.5:ratio<=5?5:10;
  return base*power;
}

export function domainTicks(domain,count=4){
  if(!domain)return [];
  return Array.from({length:count+1},(_,index)=>domain.min+(domain.max-domain.min)*index/count);
}

// Vehicle acceleration is positive forward and left. The ball moves with inertia:
// forward acceleration pushes it down/backward; a left turn pushes it right.
export function gCirclePoint(sample,x,y,radius){
  const magnitude=Math.hypot(sample.lateral,sample.longitudinal),scale=radius/Math.max(1,magnitude);
  return [x+sample.lateral*scale,y+sample.longitudinal*scale];
}
