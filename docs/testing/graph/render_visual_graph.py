"""生成自包含 SVG 分支图；Git 连线严格来自提交 parent，工程阶段单独展示。"""
from pathlib import Path
import argparse, subprocess, json

p=argparse.ArgumentParser()
p.add_argument('--repo',default='.')
p.add_argument('--out',default=str(Path(__file__).with_name('index.html')))
a=p.parse_args()
def git(*args):
    return subprocess.check_output(['git','-C',a.repo,'-c','core.quotePath=false',*args],encoding='utf8')
commits=[]
for row in git('log','--all','--topo-order','--format=%H%x00%P%x00%s','--max-count=120').splitlines():
    sha,parents,title=row.split('\0',2)
    commits.append(dict(id=sha,parents=parents.split(),title=title,refs=[]))
byid={c['id']:c for c in commits}
for row in git('for-each-ref','--format=%(refname)%00%(objectname)','refs/heads','refs/remotes','refs/tags').splitlines():
    ref,sha=row.split('\0')
    ref=ref.removeprefix('refs/heads/').removeprefix('refs/remotes/').removeprefix('refs/tags/')
    if sha in byid:byid[sha]['refs'].append(ref)
# 每个未占用的分支头开始一条轨道，第一父提交延续轨道；汇合时保留既有位置。
lanes={};nextlane=0
for c in commits:
    if c['id'] not in lanes:lanes[c['id']]=nextlane;nextlane+=1
    if c['parents'] and c['parents'][0] not in lanes:lanes[c['parents'][0]]=lanes[c['id']]
    for parent in c['parents'][1:]:
        if parent not in lanes:lanes[parent]=nextlane;nextlane+=1
for c in commits:c['lane']=lanes[c['id']]
payload=json.dumps(dict(commits=commits),ensure_ascii=False).replace('</','<\\/')
page=r'''<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>urinalysis · 可视化分支图</title>
<style>
:root{color-scheme:dark;--bg:#111820;--surface:#17222e;--line:#354553;--text:#edf3f9;--muted:#b6c5d4;--green:#8ae0b7;--blue:#a7c8ff;--amber:#f4cd87;--purple:#d4b3f5}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.6 'Segoe UI','Microsoft YaHei',sans-serif}main{max-width:1000px;margin:auto;padding:24px}h1{font-size:25px;line-height:1.3;margin:0 0 8px}p{margin:6px 0;color:var(--muted)}a{color:var(--blue);text-underline-offset:4px}header a{font-size:13px}button{font:inherit;background:transparent;color:var(--muted);border:1px solid var(--line);border-radius:6px;padding:8px 12px;cursor:pointer}button:hover{background:#233346;color:var(--text)}button:focus-visible,[role=button]:focus-visible{outline:2px solid var(--blue);outline-offset:4px}button[aria-selected=true]{background:#29423e;border-color:var(--green);color:var(--green)}.tabs{display:flex;gap:8px;flex-wrap:wrap;margin:22px 0 14px}.toolbar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:0 0 14px}.toolbar p{flex:1;min-width:170px;font-size:13px}.toolbar button{padding:5px 10px}.legend{display:flex;gap:14px;flex-wrap:wrap;font-size:12px;color:var(--muted);margin:8px 0}.legend span:before{content:'';display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--c);margin-right:6px}.canvas{overflow:auto;background:var(--surface);border-radius:10px;border:1px solid var(--line);scrollbar-color:#627991 var(--surface)}svg{display:block;width:100%;height:auto;min-width:390px}svg text{font-family:'Segoe UI','Microsoft YaHei',sans-serif;pointer-events:none}svg .hit{fill:transparent;cursor:pointer}svg g[role=button]:hover .hit,svg g.selected .hit{fill:#263b4c}svg .edge{fill:none;stroke-width:3;stroke-linecap:round}svg .node{stroke:var(--surface);stroke-width:4}svg .label{fill:var(--text);font-size:16px;font-weight:600}svg .sub{fill:var(--muted);font-size:12px}svg .hash{font-family:Consolas,monospace;font-size:12px}.details{margin-top:16px;padding:16px 0;border-top:1px solid var(--line)}.details h2{font-size:17px;margin:0 0 6px}.details p{font-size:14px}.detail-name{color:var(--text);font-weight:600}.detail-data{overflow-wrap:anywhere}footer{margin:24px 0;color:var(--muted);font-size:12px}::selection{background:#395975;color:white}button,svg a{-webkit-tap-highlight-color:transparent}@media(max-width:540px){main{padding:18px 14px}h1{font-size:22px}.tabs button{flex:1;padding:9px 6px;font-size:14px}.toolbar p{flex-basis:100%}}
</style></head><body><main>
<header><h1>urinalysis 分支图</h1><p>沿着连线查看分支、版本和每次测试的变化。</p><a href="https://github.com/TELE-nwj/urinalysis/tree/codex/ch376-test-records-20261015/docs/testing/CH376-20261012-15.md" target="_blank" rel="noopener">查看测试归档 ↗</a></header>
<nav class="tabs" role="tablist" aria-label="分支图类型"><button id="versions" role="tab" aria-selected="true" aria-controls="graphpanel">工程版本 · third / A—E</button><button id="git" role="tab" aria-selected="false" aria-controls="graphpanel" tabindex="-1">Git 提交与分支</button></nav>
<div class="toolbar"><p id="caption"></p><button id="minus" aria-label="缩小分支图">−</button><button id="fit">适应宽度</button><button id="plus" aria-label="放大分支图">＋</button></div>
<div class="legend" id="legend"></div><section class="canvas" id="graphpanel" role="tabpanel" aria-label="可视化版本分支图"><svg id="graph" xmlns="http://www.w3.org/2000/svg" aria-label="节点和连线展示的版本关系图"></svg></section>
<section class="details" aria-live="polite"><h2 id="detailtitle">点击节点查看详情</h2><p id="detailbody">每个圆点代表一个版本或提交，连线展示演进关系。</p><p class="detail-data" id="detailmeta"></p></section>
<footer>工程版本图按已归档的开发阶段整理；Git 图根据真实提交父子关系生成。正式 third 与测试副本分别保留。</footer>
</main><script>
const DATA=__PAYLOAD__;
const colors=['#8ae0b7','#a7c8ff','#d4b3f5','#f4cd87','#efaa9e'];
const svg=document.getElementById('graph');const NS='http://www.w3.org/2000/svg';let mode='versions',zoom=1;
const stages=[
{id:'first',x:48,y:48,title:'第一版',sub:'GPIO 液滴检测 · CH376T',body:'最初的 ESP32 工程，尚未进行分层整理。',from:[]},
{id:'layer',x:48,y:150,title:'第二版 · 分层阶段',sub:'业务分层、串口屏与任务调度',body:'对应仓库中保存的第二版分层与串口屏阶段目录。图中为工程阶段概览，不表示这些目录各自是 Git 分支。',from:['first']},
{id:'second',x:48,y:252,title:'second / second sol',sub:'按键、电量、实时时间与串口屏',body:'second 是参考工程；second sol 代表后续重构阶段，本机 second sol 目录本次未改动。',from:['layer']},
{id:'scend',x:48,y:354,title:'scend sol 版本',sub:'Android APP ＋ ESP32 Wi-Fi',body:'10月9日交付版本，包括 Android APK、APP 源码与 ESP32 SoftAP 移动通信工程。',from:['second']},
{id:'third',x:48,y:456,title:'third · 正式基线',sub:'原始基线保留 · 101 个文件哈希一致',body:'正式 third 目录保持原样。CH376 修复与诊断在独立副本进行，没有把 E 版覆盖为正式版。',from:['scend'],mark:'基线'},
{id:'copy',x:100,y:568,title:'third_logtest_20261012',sub:'独立测试副本 · 日志增强',body:'由正式 third 建立的独立测试工程，用于保留测试过程和诊断输出。',from:['third'],color:1},
{id:'A',x:100,y:670,title:'A · 地址诊断',sub:'采集目录项、簇号与 FAT 证据',body:'补充磁盘地址、文件目录项及关键扇区诊断，排查日志成功但电脑看不到文件的问题。',from:['copy'],color:1},
{id:'B',x:100,y:772,title:'B · 兼容验证',sub:'检查历史 1024 字节假设',body:'保留兼容性验证、原始日志及对应模型证据，供追溯。',from:['A'],color:1},
{id:'C',x:100,y:874,title:'C · 会话修复',sub:'暂停后继续复用同一检测文件',body:'修复开始／暂停／继续的文件生命周期。该阶段仍发现目录簇号为 0 的损坏证据，不能视为最终验收通过。',from:['B'],color:1},
{id:'D',x:100,y:976,title:'D · 写盘保护候选',sub:'发现握手问题 · 保留失败记录',body:'引入写盘保护后的候选版本。错误位置的 READ_VAR 干扰 CH376 写入握手，未作为可用最终版本。',from:['C'],color:2},
{id:'E',x:100,y:1078,title:'E · 握手修复',sub:'最新测试 · 94 项模型通过',body:'移除错误插入的握手读取，保留 FAT 写前检查和写后回读。实机一次建文件、暂停继续及第一分钟追加回读通过。电脑 TXT、多分钟及完整 FAT 验收仍未闭环。',from:['D'],color:0,mark:'当前测试'}
];
function el(tag,attrs={},text,parent=svg){const e=document.createElementNS(NS,tag);for(const[k,v]of Object.entries(attrs))e.setAttribute(k,v);if(text!==undefined)e.textContent=text;parent.appendChild(e);return e}
function wrap(s,max){let out=[],line='',n=0;for(const ch of s){let w=ch.charCodeAt(0)>255?1:.56;if(n+w>max){out.push(line);line='';n=0}line+=ch;n+=w}if(line)out.push(line);return out}
function edge(a,b,color){el('path',{d:`M ${a.x} ${a.y} C ${a.x} ${(a.y+b.y)/2}, ${b.x} ${(a.y+b.y)/2}, ${b.x} ${b.y}`,stroke:color,class:'edge'})}
function select(n,g){svg.querySelectorAll('.selected').forEach(e=>e.classList.remove('selected'));g.classList.add('selected');document.getElementById('detailtitle').textContent=n.title;document.getElementById('detailbody').textContent=n.body||n.title;const meta=document.getElementById('detailmeta');meta.replaceChildren();if(n.sha){meta.append('提交：'+n.sha+' · ');let a=document.createElement('a');a.href='https://github.com/TELE-nwj/urinalysis/commit/'+n.sha;a.textContent='查看提交';a.target='_blank';a.rel='noopener';meta.append(a)}else meta.textContent='阶段：'+n.id+' · '+n.sub}
function node(n){const color=colors[n.color||0];let g=el('g',{role:'button',tabindex:0,'aria-label':n.title+'，'+n.sub});el('rect',{x:20,y:n.y-26,width:540,height:n.h||88,rx:6,class:'hit'},undefined,g);el('circle',{cx:n.x,cy:n.y,r:8,fill:color,class:'node'},undefined,g);let tx=156;wrap(n.title,23).forEach((l,i)=>el('text',{x:tx,y:n.y-3+i*22,class:'label'},l,g));const rows=wrap(n.title,23).length;wrap(n.sub,29).forEach((l,i)=>el('text',{x:tx,y:n.y+rows*22+i*18,class:n.sha?'sub hash':'sub'},l,g));if(n.mark)el('text',{x:n.x+14,y:n.y-15,fill:color,'font-size':11},n.mark,g);g.addEventListener('click',()=>select(n,g));g.addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();select(n,g)}})}
function render(){svg.replaceChildren();zoom=1;svg.style.width='100%';document.getElementById('versions').setAttribute('aria-selected',mode==='versions');document.getElementById('git').setAttribute('aria-selected',mode==='git');for(const id of ['versions','git'])document.getElementById(id).tabIndex=id===mode?0:-1;document.getElementById('detailtitle').textContent='点击节点查看详情';document.getElementById('detailbody').textContent='每个圆点代表一个版本或提交，连线展示演进关系。';document.getElementById('detailmeta').textContent='';
if(mode==='versions'){svg.setAttribute('viewBox','0 0 580 1170');document.getElementById('caption').textContent='工程阶段概览 · 从早期版本到 third 与 A—E';document.getElementById('legend').innerHTML='<span style="--c:#8ae0b7">主工程 / 最新 E</span><span style="--c:#a7c8ff">测试演进</span><span style="--c:#d4b3f5">未通过候选</span>';for(const n of stages)for(const p of n.from)edge(stages.find(s=>s.id===p),n,colors[n.color||0]);stages.forEach(node)}else{let list=DATA.commits.map((c,i)=>({id:c.id,sha:c.id,x:40+c.lane*40,y:48+i*146,title:c.title,sub:c.id.slice(0,7)+(c.refs.length?' · '+c.refs.join(' / '):''),body:'分支引用：'+(c.refs.join('、')||'此提交没有分支头引用')+'。父提交：'+(c.parents.map(p=>p.slice(0,7)).join('、')||'无（初始提交）'),color:c.lane%5,h:138,parents:c.parents}));svg.setAttribute('viewBox',`0 0 580 ${list.length*146+20}`);document.getElementById('caption').textContent='真实 Git 历史 · '+list.length+' 个提交 · 点击节点看分支引用';document.getElementById('legend').innerHTML='<span style="--c:#8ae0b7">归档 / 远端 main</span><span style="--c:#a7c8ff">mobile / 本地 main</span><span style="--c:#d4b3f5">早期分支</span>';for(const n of list)for(const p of n.parents){const parent=list.find(c=>c.id===p);if(parent)edge(n,parent,colors[n.color])}for(const n of list){n.sub=n.sha.slice(0,7);node(n);if(DATA.commits.find(c=>c.id===n.id).refs.length){const refs=DATA.commits.find(c=>c.id===n.id).refs;const short=refs.map(r=>r.replace('codex/','').replace('origin/','远端/')).join(' · ');wrap(short,38).slice(0,3).forEach((t,j)=>el('text',{x:156,y:n.y+70+j*16,fill:colors[n.color],'font-size':11},t))}}}
document.getElementById('graphpanel').scrollTop=0}
for(const id of ['versions','git']){const b=document.getElementById(id);b.addEventListener('click',()=>{mode=id;render()});b.addEventListener('keydown',e=>{if(e.key==='ArrowRight'||e.key==='ArrowLeft'){e.preventDefault();mode=mode==='versions'?'git':'versions';render();document.getElementById(mode).focus()}})}
document.getElementById('plus').onclick=()=>{zoom=Math.min(2.5,zoom+.25);svg.style.width=zoom*100+'%'};document.getElementById('minus').onclick=()=>{zoom=Math.max(.65,zoom-.25);svg.style.width=zoom*100+'%'};document.getElementById('fit').onclick=()=>{zoom=1;svg.style.width='100%'};render();
</script></body></html>'''.replace('__PAYLOAD__',payload)
Path(a.out).write_text(page,encoding='utf8')
print(json.dumps({'html':a.out,'commits':len(commits),'parent_edges':sum(len(c['parents']) for c in commits)},ensure_ascii=False))
