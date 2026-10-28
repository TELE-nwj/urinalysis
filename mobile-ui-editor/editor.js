'use strict';
let design, baseline, selected='title', page='monitor', mode='IDLE', demoReset=false, dragKey=null, dirty=false;
const $=id=>document.getElementById(id);
const escape=value=>String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const label=key=>escape(design.labels[key]);
const target=(key,html,cls='')=>`<div class="design-target ${selected===key?'selected':''} ${cls}" tabindex="0" role="button" data-edit="${key}" aria-label="编辑${key==='title'?'标题':label(key)}">${html}</div>`;
const feedback=(text,error=false)=>{$('feedback').textContent=text;$('feedback').classList.toggle('error',error);};
async function request(url,options){const r=await fetch(url,options);const data=await r.json();if(!r.ok)throw Error(data.error||'操作失败');return data;}
function changed(){if(selected&&design) $('selectionName').textContent=selected==='title'?'界面标题':design.labels[selected];dirty=true;feedback('有未保存的设计调整');render();}
function choose(key){selected=key;render();$('selectionName').textContent=key==='title'?'界面标题':design.labels[key];$('copyField').hidden=false;$('copy').value=key==='title'?design.title:design.labels[key];}
function render(){
  const app=$('app'),scroll=app.scrollTop;
  app.style.setProperty('--primary',design.primary);app.style.setProperty('--bg',design.background);app.style.setProperty('--text',design.text);app.style.setProperty('--gap',design.gapDp+'px');app.style.setProperty('--pad',design.pagePaddingDp+'px');app.style.setProperty('--number',design.numberSp+'px');app.style.setProperty('--radius',design.cornerDp+'px');
  const offline=mode==='OFFLINE', state=offline?'IDLE':mode;
  const status=state==='MEASURING'?'测量中':state==='PAUSED'?'已暂停':'待开始';
  const action=state==='MEASURING'?'pause':state==='PAUSED'?'resume':'start';
  const metric=key=>{
    const values={volume:demoReset?'0.0':'125.0',rate:demoReset?'0.0 mL/h':'60.0 mL/h',drops:demoReset?'0 滴':'250 滴',elapsed:demoReset?'00:00:00':'25:03:21'};
    return target(key,`<div class="metric-label">${label(key)}</div><div class="${key==='volume'?'volume-value':'metric-value'}">${values[key]}</div>${key==='volume'?'<div class="volume-unit">mL</div>':''}`,'metric');
  };
  const monitor=`<button class="app-link" data-page="device">查看设备连接 / 重新连接</button>${target('clock',`<div class="metric-label">${offline?'最后':''}${label('clock')}</div><div class="app-clock">18:30:25</div>`)}<p>时间已同步</p><div class="indicator-line">${target('battery',`${label('battery')} 74%`)}${target('storage',`${label('storage')} 未确认`)}</div><p class="small">U盘状态为缓存；检查时间尚未确认</p>${design.metricOrder.map(metric).join('')}${target('state',`<div class="metric-label">${label('state')}</div><div class="status-text">${status}</div>`)}${target('alert',`${label('alert')}：正常`,'alert')}<div class="app-buttons"><button class="app-main design-target ${selected===action?'selected':''}" data-edit="${action}" data-action="measure" ${offline&&$('interactive').checked?'disabled':''}>${label(action)}</button><button class="app-outline design-target ${selected==='reset'?'selected':''}" data-edit="reset" data-action="reset" ${offline&&$('interactive').checked?'disabled':''}>${label('reset')}</button></div>`;
  const device=`<p>请先在系统设置中连接设备热点；提示无互联网时保持连接。</p><p>默认 SSID：NIAIO-Monitor（可配置，以系统热点为准）</p><p>设备名：NIAIO-1234</p><p>Wi-Fi 地址：192.168.4.2</p><div class="app-buttons"><button class="app-outline" data-demo="系统设置入口将在安卓设备中打开">打开系统 Wi-Fi 设置</button><button class="app-main" data-demo="这是连接界面的预览，没有连接真实设备">重新连接</button><button class="app-outline" data-demo="预览状态已切换为离线" data-offline>断开连接</button></div><p>连接结果：${offline?'设备离线':'已连接'}</p><p>对时结果：时间已同步</p><p>启动标识：boot-example</p><p>会话标识：session-example</p><p class="small">只通过设备 Wi-Fi 通信。前台恢复会重新连接，旧开始、暂停、复位请求不会重发。</p><p class="small">实机兼容性与测量控制尚未验证。</p><div class="app-divider"></div><label class="demo-switch">模拟演示（静态样例）<input type="checkbox" data-demo-switch></label><p class="small">模拟演示禁用真实测量控制，样例不代表设备数据。</p><p class="small">ws://192.168.4.1/ws</p><p>通信记录</p><p class="small">SIMULATED · 当前是编辑预览</p>`;
  app.innerHTML=target('title',escape(design.title),'app-title')+`<nav class="app-tabs"><button class="${page==='monitor'?'active':''} design-target" data-page="monitor" data-edit="monitorTab">${label('monitorTab')}</button><button class="${page==='device'?'active':''} design-target" data-page="device" data-edit="deviceTab">${label('deviceTab')}</button></nav><div class="app-content"><p class="connection">${offline?'设备离线':'已连接'}</p>${offline?'<p class="small">设备离线 · 以下为最后数据</p>':''}<p class="small">最后收到新数据：15:53:00（手机时间）</p>${page==='monitor'?monitor:device}</div>`;
  app.scrollTop=scroll;
  $('metricOrder').innerHTML=design.metricOrder.map((key,index)=>`<li draggable="true" data-metric="${key}"><span class="handle" aria-hidden="true">⋮⋮</span><span class="name">${label(key)}</span><button data-move="${key}" data-direction="-1" aria-label="上移${label(key)}" ${index===0?'disabled':''}>↑</button><button data-move="${key}" data-direction="1" aria-label="下移${label(key)}" ${index===3?'disabled':''}>↓</button></li>`).join('');
}
function inputs(){for(const key of ['primary','background','text','numberSp','gapDp','pagePaddingDp','cornerDp']){$(key).value=design[key];if($(key+'Value'))$(key+'Value').textContent=design[key]+(key==='numberSp'?' sp':' dp');}}
async function save(apply=false){try{feedback(apply?'正在同步…':'正在保存…');const data=await request(apply?'/api/apply':'/api/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(design)});dirty=false;feedback(data.message);}catch(e){feedback(e.message,true);}}
$('save').onclick=()=>save(false);$('apply').onclick=()=>save(true);
$('export').onclick=()=>{const blob=new Blob([JSON.stringify(design,null,2)],{type:'application/json'});const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download='niaio-ui-design.json';a.click();URL.revokeObjectURL(url);};
$('resetDesign').onclick=()=>{design=structuredClone(baseline);selected='title';inputs();choose('title');changed();};
$('copy').oninput=e=>{const value=e.target.value;if(!value.trim())return;if(selected==='title')design.title=value;else design.labels[selected]=value;changed();};
for(const key of ['primary','background','text','numberSp','gapDp','pagePaddingDp','cornerDp'])$(key).oninput=e=>{design[key]=e.target.type==='range'?Number(e.target.value):e.target.value;if($(key+'Value'))$(key+'Value').textContent=design[key]+(key==='numberSp'?' sp':' dp');changed();};
$('interactive').onchange=()=>{render();feedback($('interactive').checked?'交互预览：操作仅影响示例数据':'选择编辑：点击界面元素编辑');};
$('sampleState').onchange=e=>{mode=e.target.value;render();};
$('app').onclick=e=>{
  const el=e.target.closest('[data-edit],[data-page],[data-demo],[data-demo-switch]');if(!el)return;
  if(el.dataset.page){page=el.dataset.page;if(el.dataset.edit&&!$('interactive').checked)choose(el.dataset.edit);render();return;}
  if(!$('interactive').checked&&el.dataset.edit){choose(el.dataset.edit);return;}
  if(el.dataset.action==='measure'){mode=mode==='MEASURING'?'PAUSED':'MEASURING';$('sampleState').value=mode;render();}
  else if(el.dataset.action==='reset')$('confirmReset').showModal();
  else if(el.dataset.demo){feedback(el.dataset.demo);if(el.hasAttribute('data-offline')){mode='OFFLINE';$('sampleState').value=mode;render();}}
};
$('app').onkeydown=e=>{if((e.key==='Enter'||e.key===' ')&&e.target.matches('[data-edit]')&&e.target.tagName!=='BUTTON'){e.preventDefault();choose(e.target.dataset.edit);}};
$('cancelReset').onclick=()=>$('confirmReset').close();$('confirmResetAction').onclick=()=>{$('confirmReset').close();mode='IDLE';demoReset=true;$('sampleState').value=mode;render();};
$('metricOrder').onclick=e=>{const btn=e.target.closest('[data-move]');if(!btn)return;const i=design.metricOrder.indexOf(btn.dataset.move),j=i+Number(btn.dataset.direction);if(j<0||j>3)return;[design.metricOrder[i],design.metricOrder[j]]=[design.metricOrder[j],design.metricOrder[i]];changed();};
$('metricOrder').ondragstart=e=>{dragKey=e.target.closest('[data-metric]')?.dataset.metric;e.dataTransfer.effectAllowed='move';};
$('metricOrder').ondragover=e=>e.preventDefault();$('metricOrder').ondrop=e=>{e.preventDefault();const key=e.target.closest('[data-metric]')?.dataset.metric;if(!dragKey||!key||key===dragKey)return;design.metricOrder.splice(design.metricOrder.indexOf(dragKey),1);design.metricOrder.splice(design.metricOrder.indexOf(key),0,dragKey);dragKey=null;changed();};
window.addEventListener('beforeunload',e=>{if(dirty){e.preventDefault();e.returnValue='';}});
(async()=>{try{const data=await request('/api/config');design=data.design;baseline={schemaVersion:1,title:'尿量监测仪',primary:'#1565C0',background:'#FFFFFF',text:'#142E46',numberSp:52,gapDp:16,pagePaddingDp:20,cornerDp:20,metricOrder:['volume','rate','drops','elapsed'],labels:{volume:'累计尿量',rate:'实时流速',drops:'累计滴数',elapsed:'有效测量时间',clock:'设备时钟',battery:'电量',storage:'U盘',state:'测量状态',alert:'报警',start:'开始测量',pause:'暂停测量',resume:'继续测量',reset:'复位测量',monitorTab:'实时监测',deviceTab:'设备连接'}};inputs();choose('title');feedback('设计已载入 · 点击预览内容即可编辑');}catch(e){feedback(e.message,true);}})();

