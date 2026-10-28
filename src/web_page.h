#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-br"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Torrador</title>
<style>
:root{--bg:#17120f;--card:#231b16;--line:#3a2e26;--tx:#f3e9e1;--mu:#a8968a;--bt:#e8a33c;--et:#d9534f;--sp:#8ab4f8;--ok:#4caf7a;--warn:#e0b341}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--tx);font:15px system-ui,sans-serif}
main{max-width:760px;margin:auto;padding:16px;display:grid;gap:12px}
h1{font-size:20px;margin:4px 0}.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.sb{justify-content:space-between}
.temps{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
.big{font-size:36px;font-weight:700;font-variant-numeric:tabular-nums}.lbl{color:var(--mu);font-size:13px}
.err{color:var(--et);font-size:13px;min-height:1em}input[type=range]{width:100%;accent-color:var(--bt)}
button,select,.btn{font:inherit;border:0;border-radius:10px;padding:9px 14px;cursor:pointer;background:var(--line);color:var(--tx);text-decoration:none}
button:disabled{opacity:.45;cursor:default}
button.on{background:var(--et);color:#fff}button.sel{background:var(--bt);color:#000}button.go{background:var(--ok);color:#000}
#stop{width:100%;background:#b3261e;color:#fff;font-size:20px;font-weight:700;padding:16px}
.banner{border-radius:10px;padding:10px 12px;font-weight:600}.fault{background:#5c1a17;color:#ffd7d3}.block{background:#3d3215;color:var(--warn)}
.pill{display:inline-block;padding:2px 8px;border-radius:99px;background:var(--line);font-size:13px}.pill.hot{background:var(--et);color:#fff}
canvas{width:100%;height:240px;display:block}
input[type=number],input[type=text]{font:inherit;padding:7px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--tx)}
input[type=number]{width:80px}#pname{width:100%;margin:8px 0}
table{width:100%;border-collapse:collapse}td,th{padding:3px;text-align:left;font-weight:normal}th{color:var(--mu);font-size:13px}
td input{width:100%!important}.legend span{margin-right:12px;font-size:13px}.legend i{display:inline-block;width:14px;height:3px;vertical-align:middle;margin-right:4px}
.hide{display:none!important}
</style></head><body><main>
<h1>☕ Torrador <span id="conn" class="pill">…</span></h1>
<div id="fault" class="banner fault hide"></div>

<div class="temps" id="temps">
 <div class="card"><div class="lbl">BT · grão</div><div class="big" id="bt" style="color:var(--bt)">--</div><div class="err" id="bt_err"></div></div>
 <div class="card" id="et_card"><div class="lbl">ET · ar</div><div class="big" id="et" style="color:var(--et)">--</div><div class="err" id="et_err"></div></div>
 <div class="card"><div class="lbl">Torra <span id="rprof"></span></div><div class="big" id="rtime">--:--</div>
  <button id="roastbtn" class="go">Iniciar torra</button></div>
</div>

<div class="card">
 <div class="row sb">
  <div class="row"><button id="tab_roast" class="sel">Torra</button><button id="tab_live">Ao vivo</button></div>
  <div class="row lbl"><label><input type="checkbox" id="show_last" checked> última torra</label><label><input type="checkbox" id="show_prof" checked> perfil</label></div>
 </div>
 <canvas id="chart" style="margin-top:8px"></canvas>
 <div class="legend lbl"><span><i style="background:var(--bt)"></i>BT</span><span id="lg_et"><i style="background:var(--et)"></i>ET</span><span><i style="background:var(--sp)"></i>alvo</span><span><i style="background:var(--bt);opacity:.35"></i>última torra</span><span><i style="background:rgba(217,83,79,.35);height:8px"></i>resistência</span></div>
</div>

<div class="card">
 <div class="row sb"><b>Fan</b><span><span id="fanv">0</span>% <span class="lbl">aplicado <span id="fanout">0</span>% · ZC <span id="zc">0</span> Hz</span></span></div>
 <input type="range" id="fan" min="0" max="100" step="5" value="0">
 <div class="lbl">Mínimo para aquecer: <span id="fanmin">-</span>% <span id="cooling" class="pill hide">resfriando</span> <span id="fanprof" class="pill hide">controlada pelo perfil</span></div>
</div>

<div class="card">
 <div class="row sb"><b>Resistência</b><span class="pill" id="ssr">SSR off</span></div>
 <div id="block" class="banner block hide" style="margin:10px 0"></div>
 <div class="row" style="margin:10px 0">
  <button id="heat">Desligada</button>
  <button id="m_manual" class="sel">Manual</button><button id="m_auto">Automático</button><button id="m_profile">Perfil</button>
 </div>
 <div id="manual">
  <div class="row sb"><span>Potência</span><span><span id="powv">0</span>% <span class="lbl">aplicado <span id="heatout">0</span>%</span></span></div>
  <input type="range" id="power" min="0" max="100" step="1" value="0">
 </div>
 <div id="auto" class="hide row">
  <span>Alvo BT</span><input type="number" id="sp" min="0" max="240" step="1"> °C <button id="spset">Aplicar</button>
 </div>
 <div id="profmode" class="hide">
  <div>Perfil ativo: <b id="pactive">nenhum</b></div>
  <div class="lbl">Escolha e edite os perfis no quadro "Perfis" abaixo. Ao iniciar a torra, a resistência e a fan seguem o perfil.</div>
 </div>
 <div class="lbl" id="ctlinfo" style="margin-top:6px"></div>
</div>

<div class="card">
 <b>Perfis</b>
 <div class="row" style="margin-top:8px"><select id="plist"></select><button id="popen">Abrir</button><button id="pnew">Novo</button><button id="pfromlast">Copiar da última torra</button></div>
 <div id="editor" class="hide" style="margin-top:10px">
  <input type="text" id="pname" placeholder="Nome do perfil" maxlength="40">
  <table><thead><tr><th>Tempo (min:seg)</th><th>BT alvo (°C)</th><th>Fan (%)</th><th></th></tr></thead><tbody id="ptable"></tbody></table>
  <button id="padd" style="margin:6px 0">+ ponto</button>
  <canvas id="pchart" style="height:150px"></canvas>
  <div class="row" style="margin-top:8px"><button id="psave" class="sel">Salvar</button><button id="puse" class="go">Salvar e usar na torra</button><button id="pdel">Excluir</button></div>
  <div class="lbl" id="pmsg"></div>
 </div>
</div>

<div class="card">
 <div class="row sb"><b>Última torra salva</b><a class="btn" href="/api/last?dl=1" id="lastdl">Baixar CSV</a></div>
 <div class="lbl" id="lastinfo" style="margin-top:6px">Nenhuma torra salva ainda.</div>
</div>

<button id="stop">PARAR AQUECIMENTO</button>
<button id="reset" class="hide">Limpar falha</button>
</main><script>
const $=id=>document.getElementById(id);
let S={},editing={},live=[],tab='roast',prevRoasting=false;
let cur={id:-1,rows:[],busy:false},last=null,active={id:'',pts:[]},ed={id:'',rows:[]};
const fmtT=s=>{s=Math.max(0,Math.round(s));return Math.floor(s/60)+':'+String(s%60).padStart(2,'0')};
const parseT=v=>{v=String(v).trim();if(v.includes(':')){const[a,b]=v.split(':');return(+a)*60+(+b)}return Math.round(parseFloat(v)*60)};
const fmt=v=>v==null?'--':v.toFixed(1)+'°';
const css=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
function parseCsv(txt){const meta={},rows=[];for(const l of txt.split('\n')){if(!l)continue;
  if(l[0]=='#'){const i=l.indexOf('=');meta[l.slice(1,i)]=l.slice(i+1);continue}
  if(!/^\d/.test(l))continue;rows.push(l.split(',').map(v=>v===''?null:+v))}return{meta,rows}}
async function api(path,params,method){
  const r=await fetch(path,method=='GET'?{}:{method:'POST',body:new URLSearchParams(params||{})});
  if(!r.ok)throw new Error(await r.text());
  const ct=r.headers.get('content-type')||'';return ct.includes('json')?r.json():r.text()}
async function post(path,params){try{render(await api(path,params))}catch(e){alert(e.message)}}

// ---------- controles ----------
function slider(id,key,valId){const el=$(id);let t;
  el.oninput=()=>{editing[id]=Date.now();$(valId).textContent=el.value;clearTimeout(t);t=setTimeout(()=>post('/api/cmd',{[key]:el.value}),150)}}
slider('fan','fan','fanv');slider('power','power','powv');
$('heat').onclick=()=>post('/api/cmd',{heat:S.heat_on?0:1});
for(const m of['manual','auto','profile'])$('m_'+m).onclick=()=>post('/api/cmd',{mode:m});
$('spset').onclick=()=>{editing.sp=0;post('/api/cmd',{sp:$('sp').value})};
$('sp').oninput=()=>editing.sp=Date.now()+1e9;
$('stop').onclick=()=>post('/api/stop');
$('reset').onclick=()=>post('/api/reset');
$('roastbtn').onclick=()=>{if(S.roasting){if(confirm('Finalizar a torra? Ela será salva como última torra.'))post('/api/roast/stop')}
  else{if(S.mode=='profile'&&!S.prof_id)return alert('Escolha um perfil antes (quadro Perfis → Salvar e usar na torra).');post('/api/roast/start')}};
$('tab_roast').onclick=()=>{tab='roast';draw()};$('tab_live').onclick=()=>{tab='live';draw()};
$('show_last').onchange=$('show_prof').onchange=()=>draw();

function setIfIdle(id,v){if(!editing[id]||Date.now()-editing[id]>1500)$(id).value=v}
function render(s){S=s;
  $('bt').textContent=fmt(s.bt);$('et').textContent=fmt(s.et);$('bt_err').textContent=s.bt_err;$('et_err').textContent=s.et_err;
  $('et_card').classList.toggle('hide',!s.et_on);$('lg_et').classList.toggle('hide',!s.et_on);
  const profRun=s.mode=='profile'&&s.roasting;
  setIfIdle('fan',s.fan_req);$('fan').disabled=profRun;$('fanprof').classList.toggle('hide',!profRun);
  $('fanv').textContent=s.fan_req;$('fanout').textContent=s.fan;$('zc').textContent=s.zc_hz;$('fanmin').textContent=s.fan_min;
  $('cooling').classList.toggle('hide',!s.cooling);
  setIfIdle('power',s.power_req);$('powv').textContent=s.power_req;$('heatout').textContent=s.heat.toFixed(0);
  setIfIdle('sp',s.sp);
  $('heat').textContent=s.heat_on?'Ligada':'Desligada';$('heat').classList.toggle('on',s.heat_on);
  for(const m of['manual','auto','profile'])$('m_'+m).classList.toggle('sel',s.mode==m);
  $('manual').classList.toggle('hide',s.mode!='manual');$('auto').classList.toggle('hide',s.mode!='auto');$('profmode').classList.toggle('hide',s.mode!='profile');
  $('pactive').textContent=s.prof_name||'nenhum';
  $('ctlinfo').textContent=s.mode=='manual'?'':(s.sp_now!=null?'Alvo agora: '+s.sp_now.toFixed(1)+' °C · ':'')+'saída PID '+s.heat.toFixed(0)+'%'+(s.prof_done?' · perfil terminou':'');
  $('ssr').textContent=s.ssr?'SSR ON':'SSR off';$('ssr').classList.toggle('hot',s.ssr);
  $('block').textContent='🔒 '+s.block;$('block').classList.toggle('hide',!s.block);
  $('fault').textContent='⚠ '+s.fault;$('fault').classList.toggle('hide',!s.fault);$('reset').classList.toggle('hide',!s.fault);
  $('rtime').textContent=s.roasting?fmtT(s.roast_t):(cur.rows.length?fmtT(cur.rows[cur.rows.length-1][0]):'--:--');
  $('rprof').textContent=s.roasting&&s.mode=='profile'?'· '+s.prof_name:'';
  $('roastbtn').textContent=s.roasting?'Finalizar torra':'Iniciar torra';$('roastbtn').className=s.roasting?'on':'go';
}

// ---------- sincronização dos dados ----------
async function syncRoast(s){
  if(s.roast_id!==cur.id){cur={id:s.roast_id,rows:[],busy:false}}
  if(cur.busy||s.roast_id==0||s.roast_n<=cur.rows.length)return;
  cur.busy=true;const want=cur.id;
  try{const p=parseCsv(await api('/api/roast?from='+cur.rows.length,null,'GET'));
    if(+p.meta.id===want&&+p.meta.from===cur.rows.length&&cur.id===want)cur.rows.push(...p.rows)}catch(e){}
  cur.busy=false}
async function loadLast(){
  try{const p=parseCsv(await api('/api/last',null,'GET'));last=p;
    const bts=p.rows.map(r=>r[1]).filter(v=>v!=null);
    $('lastinfo').textContent='Duração '+fmtT(+p.meta.duration)+(p.meta.profile?' · perfil "'+p.meta.profile+'"':' · sem perfil')+(bts.length?' · BT final '+bts[bts.length-1].toFixed(1)+' °C · BT máx '+Math.max(...bts).toFixed(1)+' °C':'');
    $('lastdl').classList.remove('hide')}
  catch(e){last=null;$('lastinfo').textContent='Nenhuma torra salva ainda.';$('lastdl').classList.add('hide')}}
async function syncActive(s){
  if(s.prof_id===active.id)return;active={id:s.prof_id,pts:[]};
  if(!s.prof_id)return;try{active.pts=parseCsv(await api('/api/profile?id='+s.prof_id,null,'GET')).rows}catch(e){}}

async function poll(){
  try{const s=await api('/api/state',null,'GET');render(s);
    $('conn').textContent='online';$('conn').style.background='var(--ok)';
    live.push([s.uptime,s.bt,s.et,s.heat]);if(live.length>1200)live.shift();
    await syncRoast(s);await syncActive(s);
    if(prevRoasting&&!s.roasting)loadLast();prevRoasting=s.roasting;
    draw();
  }catch(e){$('conn').textContent='offline';$('conn').style.background='var(--et)'}
  setTimeout(poll,500)}

// ---------- gráfico ----------
function drawChart(c,series,bars,xmin){
  const d=devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;c.width=W*d;c.height=H*d;
  const g=c.getContext('2d');g.scale(d,d);g.clearRect(0,0,W,H);
  xmin=xmin||0;let xmax=xmin+60,lo=20,hi=100;
  for(const s of series)for(const p of s.pts){if(p[0]>xmax)xmax=p[0];if(p[1]!=null){lo=Math.min(lo,p[1]);hi=Math.max(hi,p[1])}}
  hi+=10;const L=32,B=18,x=t=>L+(t-xmin)/(xmax-xmin)*(W-L-6),y=v=>H-B-(v-lo)/(hi-lo)*(H-B-6);
  g.font='11px system-ui';g.lineWidth=1;g.strokeStyle=css('--line');g.fillStyle=css('--mu');
  for(let v=Math.ceil(lo/50)*50;v<=hi;v+=50){g.beginPath();g.moveTo(L,y(v));g.lineTo(W,y(v));g.stroke();g.fillText(v,2,y(v)+4)}
  const span=xmax-xmin,step=span>1200?300:span>300?60:30;
  for(let t=Math.ceil(xmin/step)*step;t<=xmax;t+=step)g.fillText(fmtT(t-xmin),x(t)-10,H-4);
  if(bars){g.fillStyle='rgba(217,83,79,.3)';const w=Math.max(1,(W-L)/span);
    for(const[t,h]of bars)if(h>0){const hh=h/100*(H-B)*.25;g.fillRect(x(t),H-B-hh,w,hh)}}
  for(const s of series){g.strokeStyle=s.color;g.globalAlpha=s.alpha||1;g.lineWidth=s.width||2;g.setLineDash(s.dash||[]);
    g.beginPath();let pen=false;for(const p of s.pts){if(p[1]==null){pen=false;continue}pen?g.lineTo(x(p[0]),y(p[1])):g.moveTo(x(p[0]),y(p[1]));pen=true}g.stroke()}
  g.globalAlpha=1;g.setLineDash([])}
function draw(){
  $('tab_roast').classList.toggle('sel',tab=='roast');$('tab_live').classList.toggle('sel',tab=='live');
  const bt=css('--bt'),et=css('--et'),sp=css('--sp'),series=[];
  if(tab=='live'){if(live.length){series.push({pts:live.map(h=>[h[0],h[1]]),color:bt},{pts:live.map(h=>[h[0],h[2]]),color:et});
    drawChart($('chart'),series,live.map(h=>[h[0],h[3]]),live[0][0])}else drawChart($('chart'),[]);return}
  if($('show_last').checked&&last){series.push({pts:last.rows.map(r=>[r[0],r[1]]),color:bt,alpha:.35});
    if(S.et_on)series.push({pts:last.rows.map(r=>[r[0],r[2]]),color:et,alpha:.35})}
  if($('show_prof').checked&&active.pts.length)series.push({pts:active.pts.map(p=>[p[0],p[1]]),color:sp,dash:[6,4],width:1.5,alpha:.7});
  const r=cur.rows;
  series.push({pts:r.map(p=>[p[0],p[3]]),color:sp,width:1.5},{pts:r.map(p=>[p[0],p[1]]),color:bt});
  if(S.et_on)series.push({pts:r.map(p=>[p[0],p[2]]),color:et});
  drawChart($('chart'),series,r.map(p=>[p[0],p[4]]))}

// ---------- editor de perfis ----------
async function loadList(sel){
  try{const l=await api('/api/profiles',null,'GET');const ps=$('plist');ps.innerHTML='';
    if(!l.items.length)ps.innerHTML='<option value="">(nenhum perfil salvo)</option>';
    for(const it of l.items){const o=document.createElement('option');o.value=it.id;o.textContent=it.name+(it.id==l.active?' ★':'');ps.appendChild(o)}
    if(sel)ps.value=sel;else if(l.active)ps.value=l.active}catch(e){}}
function openEditor(id,name,rows){ed={id,rows};$('pname').value=name;$('editor').classList.remove('hide');$('pmsg').textContent='';renderEd()}
function renderEd(){const tb=$('ptable');tb.innerHTML='';
  ed.rows.forEach((r,i)=>{const tr=document.createElement('tr');
    tr.innerHTML=`<td><input type="text" value="${fmtT(r[0])}"></td><td><input type="number" value="${r[1]}" step="1"></td><td><input type="number" value="${r[2]}" min="${S.fan_min||0}" max="100" step="5"></td><td><button>✕</button></td>`;
    const[a,b,c]=tr.querySelectorAll('input');
    a.onchange=()=>{r[0]=parseT(a.value);a.value=fmtT(r[0]);drawEd()};b.oninput=()=>{r[1]=+b.value;drawEd()};c.oninput=()=>r[2]=+c.value;
    tr.querySelector('button').onclick=()=>{ed.rows.splice(i,1);renderEd()};tb.appendChild(tr)});
  drawEd()}
function drawEd(){const pts=ed.rows.map(r=>[r[0],r[1]]).filter(p=>!isNaN(p[0]));
  drawChart($('pchart'),[{pts,color:css('--sp')}].concat(last?[{pts:last.rows.map(r=>[r[0],r[1]]),color:css('--bt'),alpha:.35}]:[]))}
function edData(){return ed.rows.map(r=>r.join(',')).join('\n')}
async function saveEd(){
  const r=await api('/api/profile/save',{name:$('pname').value,data:edData()});
  ed.id=r.id;$('pmsg').textContent='Salvo ✓';await loadList(r.id);active.id=null;return r.id}
$('pnew').onclick=()=>openEditor('','',[[0,100,80],[240,150,70],[480,180,60],[660,200,60],[720,205,60]]);
$('popen').onclick=async()=>{const id=$('plist').value;if(!id)return;
  try{const p=parseCsv(await api('/api/profile?id='+id,null,'GET'));openEditor(id,p.meta.name||id,p.rows)}catch(e){alert(e.message)}};
$('pfromlast').onclick=()=>{if(!last||!last.rows.length)return alert('Nenhuma torra salva ainda');
  const rows=[],min=S.fan_min||0,end=last.rows[last.rows.length-1][0];
  for(const r of last.rows)if(r[1]!=null&&(r[0]%60==0||r[0]==end)&&(!rows.length||rows[rows.length-1][0]<r[0]))rows.push([r[0],Math.round(r[1]),Math.max(min,r[5]||min)]);
  openEditor('','Cópia da última torra',rows.slice(0,24))};
$('padd').onclick=()=>{const l=ed.rows[ed.rows.length-1]||[0,100,80];ed.rows.push([l[0]+60,l[1]+5,l[2]]);renderEd()};
$('psave').onclick=async()=>{try{await saveEd()}catch(e){$('pmsg').textContent='Erro: '+e.message}};
$('puse').onclick=async()=>{try{const id=await saveEd();await post('/api/profile/use',{id});await post('/api/cmd',{mode:'profile'});
  $('pmsg').textContent='Salvo e ativado ✓ — modo Perfil selecionado, é só iniciar a torra.'}catch(e){$('pmsg').textContent='Erro: '+e.message}};
$('pdel').onclick=async()=>{if(!ed.id)return $('editor').classList.add('hide');
  if(!confirm('Excluir o perfil "'+$('pname').value+'"?'))return;
  try{await api('/api/profile/delete',{id:ed.id});$('editor').classList.add('hide');active.id=null;loadList()}catch(e){alert(e.message)}};

loadList();loadLast();poll();
</script></body></html>)HTML";
