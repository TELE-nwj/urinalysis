'use strict';
// Interactive design only. No device calls, persistent records or real file operations.
const paths = {
  drop:'M12 2.69l5.66 5.66a8 8 0 1 1-11.31 0z',
  pause:'M6 19h4V5H6v14zm8-14v14h4V5h-4z',
  play:'M8 5v14l11-7z',
  check:'M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z',
  warning:'M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z',
  wifi:'M1 9l2 2c4.97-4.97 13.03-4.97 18 0l2-2C16.93 2.93 7.08 2.93 1 9zm8 8l3 3 3-3a4.237 4.237 0 0 0-6 0zm-4-4l2 2a7.074 7.074 0 0 1 10 0l2-2C15.14 9.14 8.87 9.14 5 13z',
  battery:'M15.67 4H14V2h-4v2H8.33C7.6 4 7 4.6 7 5.33v15.33C7 21.4 7.6 22 8.33 22h7.33c.74 0 1.34-.6 1.34-1.33V5.33C17 4.6 16.4 4 15.67 4z',
  storage:'M2 20h20v-4H2v4zm2-3h2v2H4v-2zM2 4v4h20V4H2zm4 3H4V5h2v2zm-4 7h20v-4H2v4zm2-3h2v2H4v-2z',
  list:'M3 5h18v2H3V5zm0 6h18v2H3v-2zm0 6h18v2H3v-2z',
  settings:'M19.14 12.94a7.5 7.5 0 0 0 0-1.88l2.03-1.58a.5.5 0 0 0 .12-.63l-1.92-3.32a.5.5 0 0 0-.6-.22l-2.39.96a7.3 7.3 0 0 0-1.63-.94l-.36-2.54a.5.5 0 0 0-.5-.42h-3.84a.5.5 0 0 0-.5.42l-.36 2.54c-.58.24-1.13.55-1.63.94l-2.39-.96a.5.5 0 0 0-.6.22L2.7 8.85a.5.5 0 0 0 .12.63l2.03 1.58a7.5 7.5 0 0 0 0 1.88L2.82 14.5a.5.5 0 0 0-.12.63l1.92 3.32c.13.22.4.3.6.22l2.39-.96c.5.39 1.05.7 1.63.94l.36 2.54c.04.24.25.42.5.42h3.84c.25 0 .46-.18.5-.42l.36-2.54c.58-.24 1.13-.55 1.63-.94l2.39.96c.22.09.47 0 .6-.22l1.92-3.32a.5.5 0 0 0-.12-.63l-2.03-1.58zM12 15.6A3.6 3.6 0 1 1 12 8.4a3.6 3.6 0 0 1 0 7.2z',
  back:'M20 11H7.83l5.59-5.59L12 4l-8 8 8 8 1.41-1.41L7.83 13H20v-2z',
  right:'M9.29 6.71 14.58 12l-5.29 5.29L10.7 18.7 17.4 12l-6.7-6.7z',
  more:'M12 8a2 2 0 1 0 0-4 2 2 0 0 0 0 4zm0 2a2 2 0 1 0 0 4 2 2 0 0 0 0-4zm0 6a2 2 0 1 0 0 4 2 2 0 0 0 0-4z',
  chart:'M3 3h2v16h16v2H3V3zm4 12 4-5 4 3 5-7 1.6 1.2-6.2 8.7-4.1-3.1-2.7 3.5L7 15z',
  info:'M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zm1 15h-2v-6h2v6zm0-8h-2V7h2v2z',
  download:'M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z'
};
const icon = name => `<svg class="icon" viewBox="0 0 24 24" aria-hidden="true"><path d="${paths[name] || paths.info}"/></svg>`;
const escapeText = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const state = {page:'monitor',route:null,record:8,online:true,storage:'ready',measurement:'MEASURING',granularity:'minute',metric:'volume',chart:'line'};
let nextId = 9;
let records = [
  {id:8,name:'记录 0008',start:'2026-10-09 13:30:00',active:true,volume:'112.6',drops:'563',elapsed:'02:20:00',alarms:1,gap:true},
  {id:7,name:'记录 0007',start:'2026-10-09 09:00:00',active:false,volume:'186.4',drops:'932',elapsed:'03:16:24',alarms:1,gap:false},
  {id:6,name:'记录 0006',start:'2026-10-08 22:10:16',active:false,volume:'412.0',drops:'2060',elapsed:'08:42:15',alarms:0,gap:false}
];
let alerts = [
  {id:1,recordId:8,recordName:'记录 0008',time:'2026-10-09 15:42:06',reason:'少尿',recovered:'15:46:10'},
  {id:2,recordId:7,recordName:'记录 0007',time:'2026-10-09 09:18:32',reason:'无尿',recovered:'09:24:16'}
];
// Device readings remain independent from deletion of the phone's local history.
let liveReadings = {volume:'112.6',drops:'563',elapsed:'02:20:00'};
let notificationAlertId = null;
const recordAlerts = id => alerts.filter(a=>a.recordId===id);
const alertRecordName = alert => records.find(r=>r.id===alert.recordId)?.name || alert.recordName;
const recentAlerts = () => alerts.filter(a=>Date.parse('2026-10-09T15:50:00+08:00')-Date.parse(a.time.replace(' ','T')+'+08:00') < 86400000);
const app = document.getElementById('app');
const dialog = document.getElementById('dialog');
const actionButton = (text,action,cls='outline',disabled=false,extra='') => `<button class="${cls}" data-action="${action}" ${disabled?'disabled':''} ${extra}>${text}</button>`;
const selectedRecord = () => records.find(r=>r.id===state.record);
const chip = (text,tone='neutral') => `<span class="chip ${tone}">${escapeText(text)}</span>`;
const kv = (label,value) => `<div class="kv"><span>${escapeText(label)}</span><b class="nums">${escapeText(value)}</b></div>`;
function header(title,subtitle='') {
  return `<header class="app-header"><div class="identity"><h2>${title}</h2>${subtitle?`<p>${subtitle}</p>`:''}</div>${state.page==='monitor'?`<div class="connection-badge">${chip(state.online?'已连接':'Wi-Fi 未连接',state.online?'ok':'warn')}<span class="battery">${icon('battery')}${state.online?'电量 86%':'上次电量 86%'}</span></div>`:''}</header>`;
}
function subHeader(title,action='back') {
  return `<header class="app-header sub-header"><button class="icon-button" data-action="${action}" aria-label="返回">${icon('back')}</button><h2>${escapeText(title)}</h2></header>`;
}
function renderMonitor() {
  const active = records.find(r=>r.active);
  const label = state.measurement==='PAUSED'?'已暂停':state.measurement==='IDLE'?'待开始':'检测中';
  const commandLabel = state.measurement==='MEASURING'?'暂停检测':state.measurement==='PAUSED'?'继续检测':'开始检测';
  const commandIcon = state.measurement==='MEASURING'?'pause':'play';
  const volume = liveReadings.volume;
  const drops = liveReadings.drops;
  return header('尿量监测仪',state.online?'最后更新 15:50:00':'最后数据 15:50:00') + `<section class="monitor" aria-label="单屏检测">
    ${!state.online?'<div class="notice">Wi-Fi 未连接 · 以下为最后数据，检测控制不可用</div>':''}
    <div class="readout-block"><div class="row-title"><span class="muted">累计尿量</span>${chip(label,state.measurement==='PAUSED'?'warn':'neutral')}</div><div class="readout"><strong>${volume}</strong><span>mL</span></div><p class="duration">有效检测时间 <b class="nums">${liveReadings.elapsed}</b></p></div>
    <div class="metrics"><div><p>${state.online?'实时流速':'最后流速'}</p><div class="metric-value">${state.measurement==='MEASURING'?'48.0':'0.0'}<small>mL/h</small></div></div><div><p>累计滴数</p><div class="metric-value">${drops}<small>滴</small></div></div></div>
    <div class="alarm ${state.online?'':'warn'}">${icon(state.online?'check':'warning')}<b>${state.online?'报警正常':'报警状态未知'}</b><span class="hint">${state.online?'设备当前状态':'离线不可判定'}</span></div>
    <p class="command-note">${!state.online?'设备独立运行，手机记录暂未更新':state.storage==='ready'?'手机记录与 U 盘记录独立保存':'U 盘记录未保存，手机继续记录'}</p>
    <div class="actions">${actionButton(icon(commandIcon)+commandLabel,'measurement','primary',!state.online)}${actionButton('复位检测','reset','outline',!state.online)}<div class="two">${actionButton('快速连接','connect','quiet')}${actionButton('断开连接','disconnect','outline',!state.online)}</div></div>
  </section>`;
}
function storageBanner() {
  const label = {ready:'U 盘可用',absent:'U 盘未连接',error:'U 盘写入失败'}[state.storage];
  const subtitle = !state.online?'离线，仅显示上次设备状态':state.storage==='ready'?'设备缓存 · 上次检查在快照 12 秒前':'U 盘记录未保存 · 手机记录不受影响';
  return `<div class="storage-banner">${icon('storage')}<div class="storage-copy"><b>${label}</b><small>${subtitle}</small></div>${chip(state.online?(state.storage==='ready'?'可用':'未保存'):'上次状态',state.online&&state.storage==='ready'?'ok':'warn')}</div>`;
}
function recordCard(r) {
  const status = r.active?'测量中':'正常结束';
  return `<article class="record-card"><button class="record-open" data-action="detail" data-id="${r.id}" aria-label="查看${escapeText(r.name)}"><span class="record-headline"><b>${escapeText(r.name)}</b>${chip(status,r.active?'neutral':'ok')}</span><small class="nums">开始 ${r.start}</small>${icon('right')}</button><button class="record-menu icon-button" data-action="menu" data-id="${r.id}" aria-label="${escapeText(r.name)}更多选项">${icon('more')}</button></article>`;
}
function alertItem(a,removable=false) {
  return `<div class="alert-item" data-alert-id="${a.id}">${icon('warning')}<div class="alert-copy"><p>${escapeText(a.time)} · ${escapeText(a.reason)}</p><small>${escapeText(alertRecordName(a))} · ${a.recovered?escapeText(a.recovered)+' 已恢复':'未恢复'}</small></div>${removable?actionButton('删除','delete-alert','alert-delete danger-button',false,`data-id="${a.id}" aria-label="删除${escapeText(a.reason)}警报"`):''}</div>`;
}
function renderRecords() {
  const recent=recentAlerts();
  return header('记录','手机本地记录') + `<section class="scroll-content">${storageBanner()}
    <div class="section-head"><h3>异常警报</h3><small>最近 24 小时 · ${recent.length} 次</small></div><div class="alert-box">${recent.length?recent.map(a=>alertItem(a,true)).join(''):'<p class="empty">暂无已记录警报</p>'}</div><p class="hint" style="margin-top:6px">仅涵盖手机已接收的时段，离线期间的警报未知。</p>
    <div class="section-head"><h3>检测记录</h3><small>${records.length} 条</small></div>${records.length?records.map(recordCard).join(''):'<div class="empty">暂无检测记录<br><small>开始检测后会生成记录。</small></div>'}
  </section><div class="pinned-actions">${actionButton(icon('download')+'导出记录','export','outline',!records.length&&!alerts.length)}${actionButton('清除所有记录','clear','danger-button',!records.length&&!alerts.length)}</div>`;
}
function settingItem(title,detail,name,route) {
  return `<button class="setting-item" data-action="route" data-route="${route}">${icon(name)}<span class="setting-copy"><b>${title}</b><small>${detail}</small></span>${icon('right')}</button>`;
}
function renderSettings() {
  return header('设置')+`<section class="scroll-content"><div class="settings-list">${settingItem('设备连接','连接设备热点与异常通知','wifi','connection')}${settingItem('设备信息','设备时钟、地址与对时结果','info','info')}${settingItem('版本与版本更新','当前版本与更新说明','download','version')}${settingItem('通信记录','查看本机收发与连接结果','list','logs')}</div></section>`;
}
function samplePeriod(r) {
  const seconds=r.elapsed.split(':').reduce((sum,value)=>sum*60+Number(value),0);
  const start=Date.parse(r.start.replace(' ','T')+'+08:00');
  return {start,end:start+seconds*1000,seconds};
}
function timeLabel(timestamp) {
  const local=new Date(timestamp+8*3600000).toISOString();
  return {date:local.slice(0,10),time:local.slice(11,16)};
}
function minuteData() {
  let volumes = [0.8,0.6,0.8,0.8,0.6,null,0.8,1.0,0.8,0.8,0.6,0.8];
  const r=selectedRecord();
  if(!r || Number(r.volume)===0)return [];
  if(!r.gap)volumes=volumes.map(v=>v===null?0.8:v);
  const end=Math.floor(samplePeriod(r).end/60000)*60000;
  const knownSum=volumes.reduce((sum,v)=>sum+(v===null?0:v),0);
  let total = Number(r.volume)-knownSum;
  return volumes.map((value,index)=>{
    if(value!==null)total+=value;
    return {...timeLabel(end-(volumes.length-index)*60000),volume:value,rate:value===null?null:value*60,drops:value===null?null:Math.round(value*5),total:total.toFixed(1),totalDrops:Math.round(total*5)};
  });
}
function minuteRow(row) {
  const value=(number,unit)=>`${number===null?'--':number} <small>${unit}</small>`;
  return `<article class="minute-row"><div class="row-title"><b class="nums"><small>${row.date}</small><br>${row.time}:00–${row.time}:59</b><span class="hint">${row.volume===null?'数据缺失':'已接收'}</span></div><div class="minute-values"><div><span>平均速率</span><b>${value(row.rate===null?null:row.rate.toFixed(1),'mL/h')}</b></div><div><span>新增尿量</span><b>${value(row.volume===null?null:row.volume.toFixed(1),'mL')}</b></div><div><span>新增滴数</span><b>${value(row.drops,'滴')}</b></div></div><details><summary>查看累计量</summary><p class="cumulative">${row.volume===null?'未收到该分钟数据，不补零或均摊。':`累计尿量 ${row.total} mL · 累计滴数 ${row.totalDrops} 滴`}</p></details></article>`;
}
function renderDetail() {
  const r=selectedRecord();
  if(!r)return subHeader('检测记录')+'<div class="empty">该记录已删除。</div>';
  const rows=minuteData();
  const alarmCount=recordAlerts(r.id).length;
  return subHeader(r.name)+`<section class="scroll-content record-detail"><div class="section-head"><span>${chip(r.active?'测量中':'正常结束',r.active?'neutral':'ok')}</span><small class="nums">编号 ${String(r.id).padStart(4,'0')}</small></div><p class="hint nums">开始 ${r.start}</p><p class="hint">${r.active?'尚未复位结束':'已复位结束'}</p><div class="summary-grid"><div><span>累计尿量</span><b>${r.volume}</b> mL</div><div><span>累计滴数</span><b>${r.drops}</b> 滴</div><div><span>有效检测时间</span><b style="font-size:18px">${r.elapsed}</b></div><div class="has-alert"><span>历史警报</span><b>${alarmCount}</b> 次</div></div><div class="detail-actions two">${actionButton(icon('warning')+'查看警报','view-alerts','danger-button')}${actionButton(icon('chart')+'数据分析','analysis','primary')}</div><h3>每分钟记录</h3><p class="hint">${rows[0]?.date || r.start.slice(0,10)} · 新增量按自然分钟统计</p>${rows.length?rows.map(minuteRow).join(''):'<p class="empty">尚无已完成的分钟记录</p>'}</section>`;
}
const metricInfo = {volume:{name:'新增尿量',unit:'mL'},rate:{name:'平均速率',unit:'mL/h'},drops:{name:'新增滴数',unit:'滴'}};
function chartData() {
  if(state.granularity==='minute')return minuteData();
  const r=selectedRecord();
  if(!r || Number(r.volume)===0)return [];
  const period=samplePeriod(r),bins=[];
  for(let stamp=Math.floor(period.start/3600000)*3600000;stamp<period.end;stamp+=3600000) {
    const seconds=(Math.min(period.end,stamp+3600000)-Math.max(period.start,stamp))/1000;
    const gapHour=Math.floor((Math.floor(period.end/60000)*60000-7*60000)/3600000)*3600000;
    bins.push({...timeLabel(stamp),seconds,weight:seconds*[0.92,1.08,1.02][bins.length%3],partial:seconds<3600||(r.gap&&stamp===gapHour)});
  }
  const weights=bins.reduce((sum,b)=>sum+b.weight,0);
  let remainingDrops=Number(r.drops);
  return bins.map((bin,i)=>{
    const volume=Number(r.volume)*bin.weight/weights;
    const drops=i===bins.length-1?remainingDrops:Math.round(Number(r.drops)*bin.weight/weights);
    remainingDrops-=drops;
    return {...bin,volume,rate:volume/bin.seconds*3600,drops};
  });
}
function segments(items,key) {
  const result=[];let current=[];
  items.forEach((row,i)=>{if(row[key]===null){if(current.length)result.push(current);current=[];}else current.push([i,row[key]]);});
  if(current.length)result.push(current);
  return result;
}
function chartSvg(rows) {
  const info=metricInfo[state.metric];
  if(!rows.length)return '<p class="empty">暂无可分析数据</p>';
  const width=344,height=224,left=42,right=16,top=20,bottom=40;
  const values=rows.map(r=>r[state.metric]).filter(v=>v!==null);
  const max=Math.max(...values,1)*1.2;
  const inset=state.chart==='bar'?22:8;
  const x=i=>left+inset+i*(width-left-right-2*inset)/Math.max(rows.length-1,1);
  const y=v=>height-bottom-v/max*(height-top-bottom);
  let elements='';
  for(let i=0;i<4;i++){const v=max*i/3;elements+=`<line x1="${left}" x2="${width-right}" y1="${y(v)}" y2="${y(v)}" stroke="#dce4ec"/><text x="${left-8}" y="${y(v)+4}" text-anchor="end">${state.metric==='drops'?Math.round(v):v.toFixed(state.metric==='volume'&&max<10?1:0)}</text>`;}
  if(state.chart==='line')elements+=segments(rows,state.metric).map(segment=>`<polyline points="${segment.map(([i,v])=>`${x(i)},${y(v)}`).join(' ')}" fill="none" stroke="#1565c0" stroke-width="2.5"/>`).join('');
  rows.forEach((row,i)=>{
    const value=row[state.metric];
    if(value!==null){
      const label=`${row.time} ${value.toFixed(state.metric==='drops'?0:1)} ${info.unit}${row.partial?'，部分小时':''}`;
      if(state.chart==='bar'){const bw=Math.min(36,(width-left-right)/rows.length*.55);elements+=`<rect class="chart-point" aria-label="${label}" data-point="${i}" x="${x(i)-bw/2}" y="${y(value)}" width="${bw}" height="${height-bottom-y(value)}" rx="3" fill="${row.partial?'#4B83BB':'#1565C0'}"/>`;}
      else elements+=`<circle class="chart-point" aria-label="${label}" data-point="${i}" cx="${x(i)}" cy="${y(value)}" r="4.5" fill="${row.partial?'white':'#1565C0'}" stroke="${row.partial?'#1565C0':'white'}" stroke-width="2"/>`;
    }
    if(i===0||i===rows.length-1||rows.length<5||i===Math.floor(rows.length/2))elements+=`<text x="${x(i)}" y="${height-14}" text-anchor="middle">${row.time}</text>`;
  });
  return `<svg class="chart-inspector" viewBox="0 0 ${width} ${height}" role="slider" tabindex="0" aria-label="${state.granularity==='minute'?'分钟':'小时'}${info.name}${state.chart==='bar'?'柱状':'折线'}图时间点" aria-valuemin="0" aria-valuemax="${rows.length-1}" aria-valuenow="0">${elements}</svg>`;
}
function segmented(name,options,value) {return `<div class="segmented" aria-label="${name}">${options.map(([key,text])=>`<button data-action="${name}" data-value="${key}" aria-pressed="${key===value}">${text}</button>`).join('')}</div>`;}
function renderAnalysis() {
  const rows=chartData(),info=metricInfo[state.metric];
  const r=selectedRecord();
  if(!r)return subHeader('数据分析')+'<div class="empty">该记录已删除。</div>';
  return subHeader('数据分析')+`<section class="scroll-content"><p class="hint" style="margin-top:12px">${escapeText(r.name)} · 开始 ${r.start}</p>${segmented('粒度',[['minute','每分钟'],['hour','每小时']],state.granularity)}${segmented('指标',[['volume','尿量'],['rate','速率'],['drops','滴数']],state.metric)}${state.metric==='rate'?'':segmented('图型',[['line','折线图'],['bar','柱状图']],state.chart)}<div class="chart-heading"><h3>${info.name}</h3><span class="hint">${info.unit}</span></div><p class="hint">${rows.length?rows[0].date:r.start.slice(0,10)}${rows.length && rows[0].date!==rows[rows.length-1].date?' 至 '+rows[rows.length-1].date:''} · ${state.granularity==='minute'?'自然分钟':'自然小时'}</p><div class="chart-panel">${chartSvg(rows)}</div><p id="chart-reading" class="chart-reading">点击图中数据点查看数值</p><p class="chart-key">${state.granularity==='minute'?'空白断线代表未收到数据，不补零。':(state.chart==='bar'?'浅色柱表示部分小时，不能当作完整小时总量。':'空心点表示部分小时，不能当作完整小时数据。')}${state.metric==='rate'?' 速率按有效检测时长加权平均。':''}</p><h3 style="margin-top:18px">数据表</h3><table class="data-table"><thead><tr><th>时间</th><th>${info.name}</th><th>数据状态</th></tr></thead><tbody>${rows.map(r=>`<tr><td><small>${r.date}</small><br>${r.time}</td><td>${r[state.metric]===null?'--':r[state.metric].toFixed(state.metric==='drops'?0:1)} ${info.unit}</td><td>${r[state.metric]===null?'缺失':r.partial?'部分小时':'已接收'}</td></tr>`).join('')}</tbody></table></section>`;
}
function renderSettingPage() {
  const title={connection:'设备连接',info:'设备信息',version:'版本与版本更新',logs:'通信记录','app-update':'APP 更新','firmware-update':'设备固件更新'}[state.route];
  let content='';
  if(state.route==='connection')content=`<h3 style="margin-top:16px">连接设备热点</h3><div class="info-group">${kv('设备热点','NIAIO-Monitor')}${kv('连接结果',state.online?'已连接':'Wi-Fi 未连接')}</div><p class="hint">请先在手机系统设置中连接设备 Wi-Fi；提示无互联网时，请保持连接。</p><div class="stack">${actionButton('打开 Wi-Fi 设置','wifi-settings')}${actionButton('快速连接','connect','primary')}${actionButton('断开连接','disconnect','outline',!state.online)}</div><h3 style="margin-top:24px">异常通知</h3><p class="footnote">新警报通过状态栏提醒，同一持续异常不会重复刷屏。</p>${actionButton('查看通知权限','notification-settings','quiet')}<p class="hint">手机断线期间无法收到设备的新警报。</p>`;
  if(state.route==='info')content=`<div class="info-group">${kv('设备名称','NIAIO-ESP32S3')}${kv('设备地址','192.168.4.1')}${kv('设备时钟',state.online?'15:50:00':'上次 15:50:00')}${kv('对时结果','时间已同步')}${kv('启动标识','b7a1c0e2')}${kv('会话标识','7f3c9d18')}</div><p class="footnote">设备时钟由手机在连接后发送一次时分秒；之后由设备计时。</p>`;
  if(state.route==='version')content=`<div class="info-group">${kv('应用版本','1.1-v2')}</div><div class="settings-list">${settingItem('APP 更新','查看应用版本与更新说明','download','app-update')}${settingItem('设备固件更新','查看设备固件更新说明','storage','firmware-update')}</div>`;
  if(state.route==='app-update')content=`<div class="info-group">${kv('当前应用版本','1.1-v2')}</div><p class="footnote">本版本尚未开放在线更新。</p>`;
  if(state.route==='firmware-update')content=`<p class="footnote">本版本尚未开放设备固件更新。</p>`;
  if(state.route==='logs')content=`<div class="section-head"><h3>本机通信记录</h3>${actionButton('清空','clear-log','quiet')}</div><div id="log-list">${['15:50:00 收到设备状态','15:49:53 暂停指令执行完成','15:49:52 发送暂停指令','13:30:00 时间同步完成'].map((text,index)=>`<div class="log-entry">${text}<code>${['status · MEASURING · seq 2031','ack · completed · OK','command · pause','time_sync · completed · OK'][index]}</code></div>`).join('')}</div>`;
  return subHeader(title)+`<section class="scroll-content">${content}</section>`;
}
function render() {
  const nav=[['monitor','检测','drop'],['records','记录','list'],['settings','设置','settings']];
  document.getElementById('bottom-nav').innerHTML=nav.map(([key,title,name])=>`<button data-action="nav" data-page="${key}" ${state.page===key?'aria-current="page"':''}><span class="nav-icon">${icon(name)}</span><span>${title}</span></button>`).join('');
  app.innerHTML=state.route==='detail'?renderDetail():state.route==='analysis'?renderAnalysis():state.route?renderSettingPage():state.page==='monitor'?renderMonitor():state.page==='records'?renderRecords():renderSettings();
}
let snackbarTimer;
function toast(text) {const el=document.getElementById('snackbar');el.textContent=text;el.hidden=false;clearTimeout(snackbarTimer);snackbarTimer=setTimeout(()=>el.hidden=true,3500);}
function closeDialog(){dialog.close();}
function showDialog(title,body,buttons) {
  document.getElementById('dialog-body').innerHTML=`<h2 id="dialog-title">${escapeText(title)}</h2>${body}<div class="dialog-actions">${buttons.map(([label,action,cls])=>actionButton(label,action,cls||'quiet')).join('')}</div>`;
  dialog.showModal();
}
function recordMenu(id) {
  state.record=Number(id);const r=selectedRecord();
  if(!r){toast('该记录已删除');return;}
  showDialog(r.name,`<p class="hint">仅操作手机本地记录，不修改 U 盘。</p><div class="menu-actions">${actionButton('重命名','rename','outline')}${actionButton('删除记录','delete-record','danger-button',r.active)}</div>${r.active?'<p class="local-scope">当前记录尚未结束，复位后可删除。</p>':''}`,[['返回','dialog-close']]);
}
function openNotification() {
  const a=recentAlerts()[0];
  if(!a){toast('暂无已记录警报可预览');return;}
  notificationAlertId=a.id;
  const el=document.getElementById('notification');
  el.innerHTML=`<div class="notification-top">${icon('warning')}尿量监测仪 · 通知设计样例</div><h3>检测异常 · ${escapeText(a.reason)}</h3><p>${escapeText(a.time)} · ${escapeText(alertRecordName(a))}<br>设备检测到${escapeText(a.reason)}警报，请查看记录。</p><div class="notification-actions">${actionButton('关闭','notification-close','quiet')}${actionButton('查看记录','notification-open','quiet')}</div>`;
  el.hidden=false;
}
function handle(action,el) {
  if(action==='nav'){state.page=el.dataset.page;state.route=null;render();return;}
  if(action==='route'){state.route=el.dataset.route;render();return;}
  if(action==='back'){state.route=state.route==='analysis'?'detail':['app-update','firmware-update'].includes(state.route)?'version':null;render();return;}
  if(action==='dialog-close'){closeDialog();return;}
  if(action==='connect'){showDialog('快速连接','<p>请连接好 Wi-Fi</p><p class="hint" style="margin-top:8px">连接设备热点 NIAIO-Monitor 后，继续连接。</p>',[['返回','dialog-close'],['继续连接','connect-continue','primary']]);return;}
  if(action==='connect-continue'){closeDialog();state.online=true;document.getElementById('connection').value='online';render();toast('设计预览：已连接设备');return;}
  if(action==='disconnect'){state.online=false;document.getElementById('connection').value='offline';render();toast('Wi-Fi 未连接 · 设备检测独立继续');return;}
  if(action==='measurement'){
    if(state.measurement==='MEASURING')state.measurement='PAUSED';
    else if(state.measurement==='PAUSED')state.measurement='MEASURING';
    else {state.measurement='MEASURING';liveReadings={volume:'0.0',drops:'0',elapsed:'00:00:00'};const id=nextId++;records.unshift({id,name:`记录 ${String(id).padStart(4,'0')}`,start:'2026-10-09 15:50:00',active:true,volume:'0.0',drops:'0',elapsed:'00:00:00',alarms:0,gap:false});}
    render();return;
  }
  if(action==='reset'){showDialog('复位检测','<p>结束本次检测，清零当前读数。已保存的手机记录和 U 盘历史记录不会删除。</p>',[['返回','dialog-close'],['确认复位','reset-confirm','primary']]);return;}
  if(action==='reset-confirm'){closeDialog();records.forEach(r=>{if(r.active)r.active=false;});state.measurement='IDLE';liveReadings={volume:'0.0',drops:'0',elapsed:'00:00:00'};render();toast('设计预览：记录已标记正常结束');return;}
  if(action==='menu'){recordMenu(el.dataset.id);return;}
  if(action==='detail'){state.page='records';state.record=Number(el.dataset.id);state.route='detail';render();return;}
  if(action==='analysis'){state.route='analysis';render();return;}
  if(action==='view-alerts'){const r=selectedRecord();if(!r)return;const items=recordAlerts(r.id);showDialog('本次检测警报',`<p>${escapeText(r.name)}</p><div class="record-alert-list alert-box">${items.length?items.map(a=>alertItem(a)).join(''):'<p class="empty">本次检测暂无已记录警报</p>'}</div>`,[['返回','dialog-close']]);return;}
  if(action==='delete-alert'){const a=alerts.find(a=>a.id===Number(el.dataset.id));if(!a)return;showDialog('删除警报',`<p>删除手机中的“${escapeText(a.time)} · ${escapeText(a.reason)}”警报？关联记录中的历史警报数量同步更新，U 盘文件不受影响。</p>`,[['返回','dialog-close'],['删除','delete-alert-confirm','danger-button']]);dialog.dataset.alertId=a.id;return;}
  if(action==='delete-alert-confirm'){alerts=alerts.filter(a=>a.id!==Number(dialog.dataset.alertId));closeDialog();document.getElementById('notification').hidden=true;render();toast('设计预览：该手机警报已删除');return;}
  if(action==='粒度'){state.granularity=el.dataset.value;render();return;}
  if(action==='指标'){state.metric=el.dataset.value;if(state.metric==='rate')state.chart='line';render();return;}
  if(action==='图型'){state.chart=el.dataset.value;render();return;}
  if(action==='rename'){const r=selectedRecord();closeDialog();showDialog('重命名记录',`<p>编号和开始时间保持不变。</p><input id="record-name" aria-label="记录名称" maxlength="32" value="${escapeText(r.name)}">`,[['返回','dialog-close'],['保存','rename-save','primary']]);return;}
  if(action==='rename-save'){const name=document.getElementById('record-name').value.trim();if(!name){toast('请填写记录名称');return;}const r=selectedRecord();if(!r){closeDialog();return;}r.name=name;recordAlerts(r.id).forEach(a=>a.recordName=name);closeDialog();render();if(!document.getElementById('notification').hidden)openNotification();return;}
  if(action==='delete-record'){const r=selectedRecord();closeDialog();showDialog('删除记录',`<p>删除手机中的“${escapeText(r.name)}”及其分钟数据？U 盘文件不受影响。</p>`,[['返回','dialog-close'],['删除','delete-confirm','danger-button']]);return;}
  if(action==='delete-confirm'){records=records.filter(r=>r.id!==state.record);closeDialog();render();toast('设计预览：该手机记录已删除');return;}
  if(action==='clear'){showDialog('清除所有记录','<p>清除手机中的全部检测记录（含当前记录）、分钟数据和警报记录？设备继续检测，当前读数及 U 盘文件不受影响。</p>',[['返回','dialog-close'],['确认清除','clear-confirm','danger-button']]);return;}
  if(action==='clear-confirm'){records=[];alerts=[];document.getElementById('notification').hidden=true;closeDialog();render();toast('设计预览：检测记录和警报记录已全部清除');return;}
  if(action==='export'){showDialog('导出手机记录','<p>导出 CSV 文件，包含开始时间、分钟增量、累计量和数据缺失标记。</p><p class="hint" style="margin-top:8px">正式 APP 将打开系统保存/分享界面。当前仅展示设计，不生成文件。</p>',[['返回','dialog-close'],['保存到手机','export-preview','primary']]);return;}
  if(action==='export-preview'){closeDialog();toast('设计预览：正式 APP 使用系统文件保存器');return;}
  if(action==='notification-close'){document.getElementById('notification').hidden=true;return;}
  if(action==='notification-open'){document.getElementById('notification').hidden=true;const a=alerts.find(a=>a.id===notificationAlertId);if(!a){toast('该警报已删除');return;}state.page='records';state.record=a.recordId;state.route='detail';render();return;}
  if(action==='clear-log'){document.getElementById('log-list').innerHTML='<p class="empty">暂无通信记录</p>';return;}
  const notices={'wifi-settings':'正式 APP 将打开手机 Wi-Fi 设置','notification-settings':'正式 APP 将打开系统通知权限设置','update':'尚未配置更新来源，无法检查新版本','release-notes':'设计草案：检测、记录、设置三页与本地分析'};
  if(notices[action])toast(notices[action]);
}
document.addEventListener('click',event=>{
  const chart=event.target.closest('.chart-inspector');
  if(chart){const rows=chartData(),bounds=chart.getBoundingClientRect(),inset=state.chart==='bar'?22:8;const i=Math.round(((event.clientX-bounds.left)/bounds.width*344-42-inset)/(286-2*inset)*Math.max(rows.length-1,1));inspectPoint(chart,Math.max(0,Math.min(rows.length-1,i)));return;}
  const el=event.target.closest('[data-action]');if(el&&!el.disabled)handle(el.dataset.action,el);
});
function inspectPoint(chart,index){
  const row=chartData()[index],info=metricInfo[state.metric];
  if(!row)return;
  const label=`${row.date} ${row.time} · ${row[state.metric]===null?'未收到数据':row[state.metric].toFixed(state.metric==='drops'?0:1)+' '+info.unit}${row.partial?' · 部分小时':''}`;
  chart.setAttribute('aria-valuenow',String(index));chart.setAttribute('aria-valuetext',label);
  document.getElementById('chart-reading').textContent=label;
}
document.addEventListener('keydown',event=>{
  const chart=event.target.closest('.chart-inspector');
  if(chart&&['ArrowLeft','ArrowRight','Home','End'].includes(event.key)){
    event.preventDefault();const max=chartData().length-1,current=Number(chart.getAttribute('aria-valuenow'));
    const index=event.key==='Home'?0:event.key==='End'?max:current+(event.key==='ArrowRight'?1:-1);
    inspectPoint(chart,Math.max(0,Math.min(max,index)));
  }
});
document.getElementById('connection').addEventListener('change',event=>{state.online=event.target.value==='online';render();});
document.getElementById('storage').addEventListener('change',event=>{state.storage=event.target.value;render();});
document.getElementById('notify-preview').addEventListener('click',openNotification);
document.getElementById('status-icons').innerHTML=icon('wifi')+icon('battery');
render();
// Reload presentation CSS when it changes, preserving the user's current page/dialog.
let observedStyles = null;
setInterval(async () => {
  try {
    const response = await fetch('preview.css', {cache:'no-store'});
    if(!response.ok)return;
    const text = await response.text();
    if(observedStyles!==null && text!==observedStyles) {
      document.querySelector('link[rel="stylesheet"]').href='preview.css?v='+Date.now();
    }
    observedStyles=text;
  } catch (_) { /* Local preview server may be stopped. */ }
}, 2500);
