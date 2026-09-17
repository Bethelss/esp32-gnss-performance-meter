import test from 'node:test';
import assert from 'node:assert/strict';
import {DeviceSimulator,SimulatorAdapter,MODES} from '../dist/engine.mjs';
import {roadProfile,sliceSamples} from '../dist/analytics.mjs';
import {segments,domainTicks,niceDomain,reducePoints,seriesSegments,cursorSample,gCirclePoint} from '../dist/chart.mjs';

const ticks=(device,count)=>{for(let i=0;i<count;i++)device.tick(.05);};
const mode=id=>MODES.find(item=>item.id===id);
function manualRun(id){const device=new DeviceSimulator();device.command('mode',{mode:mode(id)});device.command('arm');device.startDrive();ticks(device,700);return device;}

test('manual modes retain their deterministic standalone records',()=>{
  for(const id of ['100','200','brake','quarter','eighth']){
    const first=manualRun(id),second=manualRun(id);
    assert.equal(first.runs.length,1);
    assert.equal(first.attempt.phase,'result');
    assert.equal(first.runs[0].elapsed,second.runs[0].elapsed);
  }
});

test('an auto session stores its stream once and makes all passed child segments',()=>{
  const device=new DeviceSimulator();
  device.command('arm');device.startDrive();ticks(device,700);
  assert.equal(device.attempt.phase,'result');
  assert.equal(device.autoSessions.length,1);
  const session=device.autoSessions[0];
  assert.ok(session.samples.length>100);
  assert.equal(session.valid,true);
  assert.deepEqual(new Set(session.segments.map(segment=>segment.key)),new Set(['0-60','0-100','80-140','100-200','eighth','200-100','100-0']));
  assert.ok(!session.segments.some(segment=>segment.key==='quarter'));
  const children=device.runs.filter(run=>run.auto);
  assert.equal(children.length,session.segments.length);
  assert.ok(children.every(run=>run.sessionId===session.id && run.samples===undefined));
  const elasticity=children.find(run=>run.mode.id==='auto-elasticity');
  assert.ok(elasticity.elapsed>0);
  assert.ok(sliceSamples(session.samples,elasticity.start,elasticity.end).length>1);
});

test('cancelling before start creates no auto session, while manual finish after start saves one',()=>{
  const before=new DeviceSimulator();
  before.command('arm');before.command('cancel');
  assert.equal(before.autoSessions.length,0);
  const after=new DeviceSimulator();
  after.command('arm');after.startDrive();ticks(after,80);after.command('cancel');
  assert.equal(after.attempt.phase,'result');
  assert.equal(after.autoSessions.length,1);
});

test('quality issues only invalidate automatic windows they overlap',()=>{
  const device=new DeviceSimulator();
  device.command('arm');device.startDrive();ticks(device,90);
  device.gnss=false;ticks(device,5);device.gnss=true;ticks(device,650);
  assert.ok(device.runs.some(run=>!run.valid));
  assert.ok(device.runs.some(run=>run.valid));
});

test('schema 3 survives persistence and schema 2 auto records migrate without deleting trips',()=>{
  const device=new DeviceSimulator();device.command('arm');device.startDrive();ticks(device,700);
  const restored=new DeviceSimulator(JSON.parse(JSON.stringify(device.export())));
  assert.equal(restored.autoSessions.length,1);
  const old={schema:2,trips:[{id:'trip-1',carId:null,createdAt:1,start:0,endedAt:8,samples:restored.autoSessions[0].samples}],runs:[{id:'run-1',auto:true,tripId:'trip-1',sessionId:'old-session',mode:{id:'auto-100',name:'0–100 км/ч'},start:1,elapsed:4,valid:true,reasons:[]}],cars:[],settings:{}};
  const migrated=new DeviceSimulator(old);
  assert.equal(migrated.trips.length,1);
  assert.equal(migrated.autoSessions.length,1);
  assert.equal(migrated.autoSessions[0].segments[0].recordId,'run-1');
});

test('road profile needs accurate altitude and flags a slope beyond one percent',()=>{
  const samples=Array.from({length:61},(_,index)=>({time:index,sequence:index,distanceM:index,altitudeMslM:100+index*.015,verticalAccuracyM:1,gnss:'valid'}));
  const up=roadProfile(samples);
  assert.ok(up.available);
  assert.ok(up.averageSlopePercent>1);
  const poor=roadProfile(samples.map(sample=>({...sample,verticalAccuracyM:7})));
  assert.equal(poor.available,false);
});

test('G-circle, decimation, gaps and shared cursor helpers retain their contracts',()=>{
  const point=sample=>gCirclePoint(sample,100,100,50);
  assert.deepEqual(point({longitudinal:1,lateral:0}),[100,150]);
  const points=Array.from({length:100000},(_,index)=>({time:index*.05+(index>50000?3:0),sequence:index,speed:Math.sin(index*.04)*100}));
  points[1123].speed=350;points[54123].speed=-200;
  const reduced=segments(points).flatMap(group=>reducePoints(group,360,'speed'));
  assert.ok(reduced.includes(points[1123])&&reduced.includes(points[54123]));
  assert.equal(cursorSample(points,2501),null);
});

test('adapter reconnect does not duplicate session data and calibration remains available',()=>{
  const device=new DeviceSimulator(),adapter=new SimulatorAdapter(device);
  device.command('arm');device.startDrive();adapter.tick(.05);adapter.connect(false);
  for(let i=0;i<700;i++)adapter.tick(.05);
  adapter.connect(true);
  assert.equal(adapter.state().device.autoSessions.length,1);
  device.command('calibrate');ticks(device,70);
  assert.equal(device.calibration,'success');
});

test('report chart domains and series gaps stay readable on both axes',()=>{
  const domain=niceDomain([0,61,100],{includeZero:true,minSpan:20});
  assert.equal(domain.min,0);
  assert.ok(domain.max>=100);
  assert.equal(domainTicks(domain).length,5);
  const rows=[
    {time:0,distanceM:0,speed:0,altitude:100},
    {time:.05,distanceM:1,speed:4,altitude:100.1},
    {time:.1,distanceM:2,speed:8,altitude:null},
    {time:.15,distanceM:3,speed:12,altitude:100.3},
    {time:.4,distanceM:8,speed:20,altitude:100.5},
  ];
  assert.equal(seriesSegments(rows,'speed','distanceM').length,2);
  assert.equal(seriesSegments(rows,'altitude','distanceM').length,3);
  const dense=Array.from({length:1000},(_,index)=>({time:index*.05,distanceM:index*1.7,speed:index===412?250:index%80}));
  const reduced=reducePoints(dense,40,'speed','distanceM');
  assert.ok(reduced.includes(dense[412]));
});
