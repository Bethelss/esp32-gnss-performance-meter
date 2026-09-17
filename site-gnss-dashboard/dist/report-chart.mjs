import {cursorSample,domainTicks,niceDomain,reducePoints,seriesSegments} from './chart.mjs?report=2';

const C={speed:'#FF8A3D',acceleration:'#6DB7E8',height:'#78C49A',slope:'#E8C470',red:'#F07878',grid:'#393939',muted:'#AAAAA6',text:'#F2F0EB'};
const finite=value=>Number.isFinite(value);

function context(canvas){
  const box=canvas.getBoundingClientRect(),dpr=window.devicePixelRatio||1,w=box.width,h=box.height;
  if(canvas.width!==Math.round(w*dpr)||canvas.height!==Math.round(h*dpr)){canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr);}
  const c=canvas.getContext('2d');c.setTransform(dpr,0,0,dpr,0,0);c.clearRect(0,0,w,h);
  return {c,w,h};
}

function fmt(value,precision=1){
  if(!finite(value))return '—';
  const abs=Math.abs(value),digits=abs>=100?0:abs>=10?1:precision;
  return value.toFixed(digits);
}

function line(c,points,x,y,color,width=2,dash=[]){
  c.strokeStyle=color;c.lineWidth=width;c.setLineDash(dash);c.beginPath();
  points.forEach((point,index)=>index?c.lineTo(x(point),y(point)):c.moveTo(x(point),y(point)));
  c.stroke();c.setLineDash([]);
}

function profileRows(samples,profilePoints){
  const bySequence=new Map(profilePoints.map(point=>[point.sequence,point]));
  return samples.map(sample=>{
    const profile=bySequence.get(sample.sequence);
    return {...sample,relativeAltitudeM:profile?.relativeAltitudeM??null,slopePercent:profile?.slopePercent??null,provisionalSlopePercent:profile?.provisionalSlopePercent??null};
  });
}

function xTicks(domain,count=4){return domainTicks(domain,count);}

function gapBands(samples,plot,x){
  const bands=[];
  for(let index=1;index<samples.length;index++){
    const previous=samples[index-1],current=samples[index];
    if(current.time-previous.time>.15)bands.push([x(previous),x(current)]);
  }
  return bands.filter(([left,right])=>right-left>1).map(([left,right])=>({left,right}));
}

function drawGrid(c,plot,xTicks,yCount=4){
  c.strokeStyle=C.grid;c.lineWidth=1;
  for(let index=0;index<=yCount;index++){const y=plot.top+(plot.bottom-plot.top)*index/yCount;c.beginPath();c.moveTo(plot.left,y);c.lineTo(plot.right,y);c.stroke();}
  for(const value of xTicks){const px=plot.x(value);c.beginPath();c.moveTo(px,plot.top);c.lineTo(px,plot.bottom);c.stroke();}
}

function marker(c,x,y,color){
  c.fillStyle='#171717';c.strokeStyle=color;c.lineWidth=2;c.beginPath();c.arc(x,y,3.5,0,Math.PI*2);c.fill();c.stroke();
}

export function drawPerformanceReport({mainCanvas,slopeCanvas,samples,profilePoints,xKey='time',visible={speed:true,longitudinal:true,height:true},cursor=null}){
  const rows=profileRows(samples,profilePoints),xValues=rows.map(row=>row[xKey]).filter(finite);
  if(!xValues.length)return {hasRoad:false,hasDistance:false,cursor:null};
  const xDomain={min:Math.min(...xValues),max:Math.max(...xValues)||Math.min(...xValues)+1};
  if(xDomain.max===xDomain.min)xDomain.max=xDomain.min+1;
  const hasDistance=rows.length>1&&rows.every(row=>finite(row.distanceM))&&xDomain.max>xDomain.min;
  const heightValues=rows.map(row=>row.relativeAltitudeM).filter(finite);
  const slopeValues=rows.map(row=>row.slopePercent).filter(finite);
  const provisionalSlopeValues=rows.map(row=>row.provisionalSlopePercent).filter(finite);
  const hasRoad=heightValues.length>1;
  const speedDomain=niceDomain(rows.map(row=>row.speed),{includeZero:true,minSpan:20});
  const longValues=rows.map(row=>row.longitudinal).filter(finite);
  const longDomain=niceDomain(longValues,{includeZero:true,symmetric:Math.min(...longValues,0)<0&&Math.max(...longValues,0)>0,minSpan:.25});
  const heightDomain=hasRoad?niceDomain(heightValues,{minSpan:.5}):null;
  const slopeEdge=Math.max(1.25,...slopeValues.concat(provisionalSlopeValues).map(value=>Math.abs(value)*1.12));
  const slopeDomain={min:-slopeEdge,max:slopeEdge,step:1};
  const main=context(mainCanvas),slope=context(slopeCanvas);
  const left=heightDomain?58:34,mainPlot={left,right:main.w-42,top:18,bottom:main.h-12};
  mainPlot.x=value=>mainPlot.left+(value-xDomain.min)/(xDomain.max-xDomain.min)*(mainPlot.right-mainPlot.left);
  const slopePlot={left,right:slope.w-42,top:12,bottom:slope.h-25};
  slopePlot.x=value=>slopePlot.left+(value-xDomain.min)/(xDomain.max-xDomain.min)*(slopePlot.right-slopePlot.left);
  const xMap=value=>mainPlot.x(value),speedY=value=>mainPlot.bottom-(value-speedDomain.min)/(speedDomain.max-speedDomain.min)*(mainPlot.bottom-mainPlot.top),longY=value=>mainPlot.bottom-(value-longDomain.min)/(longDomain.max-longDomain.min)*(mainPlot.bottom-mainPlot.top),heightY=value=>mainPlot.bottom-(value-heightDomain.min)/(heightDomain.max-heightDomain.min)*(mainPlot.bottom-mainPlot.top),slopeY=value=>slopePlot.bottom-(value-slopeDomain.min)/(slopeDomain.max-slopeDomain.min)*(slopePlot.bottom-slopePlot.top);
  const ticks=xTicks(xDomain);
  drawGrid(main.c,mainPlot,ticks);drawGrid(slope.c,slopePlot,ticks,2);
  for(const band of gapBands(rows,mainPlot,xMap)){main.c.fillStyle='#F0787814';main.c.fillRect(band.left,mainPlot.top,band.right-band.left,mainPlot.bottom-mainPlot.top);slope.c.fillStyle='#F0787814';slope.c.fillRect(band.left,slopePlot.top,band.right-band.left,slopePlot.bottom-slopePlot.top);}
  const drawSeries=(key,y,color,shown)=>{if(!shown)return;for(const group of seriesSegments(rows,key,xKey)){line(main.c,reducePoints(group,Math.max(40,Math.floor(mainPlot.right-mainPlot.left)),key,xKey),point=>xMap(point[xKey]),point=>y(point[key]),color);}};
  drawSeries('speed',speedY,C.speed,visible.speed);drawSeries('longitudinal',longY,C.acceleration,visible.longitudinal);if(heightDomain)drawSeries('relativeAltitudeM',heightY,C.height,visible.height);
  main.c.font='500 11px "Roboto Condensed",sans-serif';
  for(const fraction of [0,.5,1]){
    const y=mainPlot.bottom-(mainPlot.bottom-mainPlot.top)*fraction;
    if(heightDomain){main.c.fillStyle=C.height;main.c.textAlign='left';main.c.fillText(fmt(heightDomain.min+(heightDomain.max-heightDomain.min)*fraction),2,y+4);}
    main.c.fillStyle=C.acceleration;main.c.textAlign='left';main.c.fillText(fmt(longDomain.min+(longDomain.max-longDomain.min)*fraction),heightDomain?31:2,y+4);
    main.c.fillStyle=C.speed;main.c.textAlign='right';main.c.fillText(fmt(speedDomain.min+(speedDomain.max-speedDomain.min)*fraction),main.w-2,y+4);
  }
  const zeroY=slopeY(0);slope.c.strokeStyle=C.muted;slope.c.lineWidth=1.25;slope.c.beginPath();slope.c.moveTo(slopePlot.left,zeroY);slope.c.lineTo(slopePlot.right,zeroY);slope.c.stroke();
  for(const threshold of [-1,1]){const y=slopeY(threshold);slope.c.strokeStyle=C.slope;slope.c.lineWidth=1;slope.c.setLineDash([4,4]);slope.c.beginPath();slope.c.moveTo(slopePlot.left,y);slope.c.lineTo(slopePlot.right,y);slope.c.stroke();slope.c.setLineDash([]);}
  if(slopeValues.length){for(const group of seriesSegments(rows,'slopePercent',xKey)){const reduced=reducePoints(group,Math.max(40,Math.floor(slopePlot.right-slopePlot.left)),'slopePercent',xKey);line(slope.c,reduced,point=>slopePlot.x(point[xKey]),point=>slopeY(point.slopePercent),C.slope,2);for(let index=1;index<reduced.length;index++){const a=reduced[index-1],b=reduced[index];if(Math.abs(a.slopePercent)>1||Math.abs(b.slopePercent)>1)line(slope.c,[a,b],point=>slopePlot.x(point[xKey]),point=>slopeY(point.slopePercent),C.red,2.5);}}}
  if(provisionalSlopeValues.length){for(const group of seriesSegments(rows,'provisionalSlopePercent',xKey)){const reduced=reducePoints(group,Math.max(40,Math.floor(slopePlot.right-slopePlot.left)),'provisionalSlopePercent',xKey);line(slope.c,reduced,point=>slopePlot.x(point[xKey]),point=>slopeY(point.provisionalSlopePercent),C.slope,1.5,[4,4]);}}
  slope.c.font='500 11px "Roboto Condensed",sans-serif';slope.c.fillStyle=C.slope;slope.c.textAlign='right';slope.c.fillText('+1%',slope.w-2,slopeY(1)+4);slope.c.fillStyle=C.muted;slope.c.fillText('0%',slope.w-2,slopeY(0)+4);slope.c.fillStyle=C.slope;slope.c.fillText('−1%',slope.w-2,slopeY(-1)+4);
  slope.c.fillStyle=C.muted;slope.c.textAlign='center';
  for(const value of ticks){const relative=xKey==='time'?value-xDomain.min:value-xDomain.min;slope.c.fillText(xKey==='time'?fmt(relative)+' с':Math.round(relative)+' м',slopePlot.x(value),slope.h-5);}
  let cursorData=null;
  if(cursor!==null&&cursor!==undefined){
    const at=xDomain.min+(xDomain.max-xDomain.min)*cursor,sample=xKey==='time'?cursorSample(rows,at):rows.reduce((best,row)=>!best||Math.abs(row[xKey]-at)<Math.abs(best[xKey]-at)?row:best,null);
    const row=sample?rows.find(candidate=>candidate.sequence===sample.sequence)||sample:null;
    const px=xMap(at);
    for(const {c,plot} of [{c:main.c,plot:mainPlot},{c:slope.c,plot:slopePlot}]){c.strokeStyle=C.text;c.globalAlpha=.85;c.lineWidth=1;c.beginPath();c.moveTo(px,plot.top);c.lineTo(px,plot.bottom);c.stroke();c.globalAlpha=1;}
    if(row){if(visible.speed)marker(main.c,xMap(row[xKey]),speedY(row.speed),C.speed);if(visible.longitudinal)marker(main.c,xMap(row[xKey]),longY(row.longitudinal),C.acceleration);if(heightDomain&&visible.height&&finite(row.relativeAltitudeM))marker(main.c,xMap(row[xKey]),heightY(row.relativeAltitudeM),C.height);if(finite(row.slopePercent))marker(slope.c,slopePlot.x(row[xKey]),slopeY(row.slopePercent),C.slope);cursorData={sample:row,at,hasGap:false};}else cursorData={sample:null,at,hasGap:true};
  }
  mainCanvas.dataset.plotLeft=String(mainPlot.left);mainCanvas.dataset.plotRight=String(mainPlot.right);slopeCanvas.dataset.plotLeft=String(slopePlot.left);slopeCanvas.dataset.plotRight=String(slopePlot.right);
  return {hasRoad,hasDistance,cursor:cursorData};
}
