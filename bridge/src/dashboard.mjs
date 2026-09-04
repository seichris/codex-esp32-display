export const DASHBOARD_HTML = String.raw`<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Codex ESP32 Display</title>
<style>
:root{color-scheme:dark;font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#080a0d;color:#f5f7fa}
*{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at 20% 0,#202938 0,transparent 40%),#080a0d}
main{width:min(880px,calc(100% - 32px));margin:0 auto;padding:38px 0 80px}header{display:flex;align-items:end;justify-content:space-between;gap:20px;margin-bottom:24px}
h1{font-size:clamp(30px,5vw,52px);letter-spacing:-.045em;margin:0}.eyebrow{color:#9aa5b5;text-transform:uppercase;letter-spacing:.13em;font-size:12px;font-weight:750;margin-bottom:8px}.count{font-variant-numeric:tabular-nums;color:#a8b1bf}
.grid{display:grid;gap:12px}.card{width:100%;text-align:left;color:inherit;border:1px solid #2b3442;background:rgba(18,22,29,.9);border-radius:17px;padding:17px 18px;box-shadow:0 12px 32px rgba(0,0,0,.18);cursor:pointer}.card:hover,.card:focus-visible{border-color:#78aef9;background:#18202b;outline:none}.card.waiting_approval,.card.waiting_input{border-color:#c99738;background:rgba(52,39,17,.78)}
.title{font-size:19px;font-weight:750;line-height:1.25}.meta{display:flex;gap:9px;align-items:center;color:#99a4b4;font-size:13px;margin-top:6px}.preview{color:#c5ccd6;margin-top:12px;line-height:1.4}.badges{display:flex;flex-wrap:wrap;gap:6px;margin-top:13px}.badge{border:1px solid #394352;border-radius:999px;padding:4px 8px;font-size:10px;font-weight:800;letter-spacing:.08em;text-transform:uppercase}.badge.wait{border-color:#d6a646;color:#f3c96d}.badge.unread{border-color:#4f88da;color:#83b5ff}.badge.pin{border-color:#8968dc;color:#ba9dff}.badge.run{border-color:#3d9e89;color:#69d4bd}
.empty,.error{border:1px dashed #344050;border-radius:18px;padding:35px;text-align:center;color:#a7b1c0}.error{border-color:#874b55;color:#ffb0bb;margin-bottom:15px;text-align:left;padding:14px 16px}.toolbar button,.back{background:#202734;color:#eef2f7;border:1px solid #3b4657;border-radius:10px;padding:8px 12px;cursor:pointer}.toolbar button:hover,.back:hover{background:#2a3443}
.detail{border:1px solid #344050;background:rgba(16,20,27,.94);border-radius:20px;padding:22px;box-shadow:0 18px 50px rgba(0,0,0,.28)}.detail[hidden]{display:none}.detail-head{display:flex;align-items:start;justify-content:space-between;gap:16px}.detail h2{font-size:clamp(23px,4vw,34px);letter-spacing:-.03em;margin:5px 0 5px}.detail-text{white-space:pre-wrap;overflow-wrap:anywhere;line-height:1.55;color:#d8dee8;border-top:1px solid #2b3442;margin-top:18px;padding-top:18px;max-height:60vh;overflow:auto}.kind{color:#99a4b4;text-transform:uppercase;letter-spacing:.12em;font-size:11px;font-weight:800}
@media(max-width:600px){header{align-items:start;flex-direction:column}.card{padding:15px}.detail-head{flex-direction:column-reverse}}
</style>
</head>
<body><main>
<header><div><div class="eyebrow">Physical inbox preview</div><h1>Codex ESP32 Display</h1></div><div class="toolbar"><span class="count" id="count">Connecting…</span> <button id="token">Token</button></div></header>
<div id="error"></div><div id="list" class="grid"></div>
<section id="detail" class="detail" hidden><div class="detail-head"><div><div class="kind" id="detail-kind">Latest text</div><h2 id="detail-title"></h2><div class="meta" id="detail-meta"></div></div><button class="back" id="back">Back</button></div><div class="badges" id="detail-badges"></div><div class="detail-text" id="detail-text">Loading…</div></section>
</main>
<script>
const list=document.querySelector('#list'),count=document.querySelector('#count'),errorBox=document.querySelector('#error');
const detail=document.querySelector('#detail'),detailTitle=document.querySelector('#detail-title'),detailMeta=document.querySelector('#detail-meta'),detailText=document.querySelector('#detail-text'),detailKind=document.querySelector('#detail-kind'),detailBadges=document.querySelector('#detail-badges');
const labels={waiting_approval:'Needs approval',waiting_input:'Needs input',new_result:'New result',unread:'Unread',pinned:'Pinned'};
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#039;'}[c]));
const age=s=>s<60?'now':s<3600?Math.floor(s/60)+'m':s<86400?Math.floor(s/3600)+'h':Math.floor(s/86400)+'d';
const scrollDetailToEnd=()=>{detailText.scrollTop=detailText.scrollHeight};
const auth=()=>{const token=localStorage.getItem('codex-esp32-display-token')||'';return token?{Authorization:'Bearer '+token}:{}};
function badge(reason){const cls=reason.startsWith('waiting')?'wait':reason==='pinned'?'pin':reason==='unread'||reason==='new_result'?'unread':'';return '<span class="badge '+cls+'">'+esc(labels[reason]||reason)+'</span>'}
async function authorizedFetch(path){let response=await fetch(path,{cache:'no-store',headers:auth()});if(response.status===401){const token=prompt('Bridge token')||'';if(token){localStorage.setItem('codex-esp32-display-token',token);response=await fetch(path,{cache:'no-store',headers:auth()})}}return response}
async function openDetail(item){list.hidden=true;detail.hidden=false;detailTitle.textContent=item.title;detailMeta.textContent=item.project;detailKind.textContent='Loading latest text';detailText.textContent='Loading…';scrollDetailToEnd();detailBadges.innerHTML=item.reasons.map(badge).join('');try{const response=await authorizedFetch('/api/v1/threads/'+encodeURIComponent(item.id)+'/latest');if(!response.ok)throw new Error('HTTP '+response.status);const data=await response.json();detailTitle.textContent=data.title;detailMeta.textContent=data.project;detailKind.textContent=(data.kind||'latest')+(data.truncated?' · truncated':'');detailText.textContent=data.text;scrollDetailToEnd();detailBadges.innerHTML=(data.reasons||[]).map(badge).join('')}catch(error){detailKind.textContent='Could not load';detailText.textContent=error.message;scrollDetailToEnd()}}
async function load(){const response=await authorizedFetch('/api/v1/attention');if(!response.ok)throw new Error('HTTP '+response.status);const data=await response.json();count.textContent=data.totalCount+' item'+(data.totalCount===1?'':'s');errorBox.innerHTML=data.diagnostics?.sourceError?'<div class="error">'+esc(data.diagnostics.sourceError)+'</div>':'';if(!data.items.length){const emptyText=data.attentionFilter==='unread+pinned'?'No unread and pinned threads.':'No unread, pinned, or waiting threads.';list.innerHTML='<div class="empty"><strong>Inbox clear.</strong><br>'+emptyText+'</div>';return}list.innerHTML=data.items.map((item,index)=>'<button type="button" class="card '+esc(item.status)+'" data-index="'+index+'"><div class="title">'+esc(item.title)+'</div><div class="meta"><span>'+esc(item.project)+'</span><span>·</span><span>'+age(item.ageSeconds)+'</span></div>'+(item.preview&&item.preview!==item.title?'<div class="preview">'+esc(item.preview)+'</div>':'')+'<div class="badges">'+item.reasons.map(badge).join('')+(item.status==='running'?'<span class="badge run">Running</span>':'')+'</div></button>').join('');list.querySelectorAll('.card').forEach(button=>button.addEventListener('click',()=>openDetail(data.items[Number(button.dataset.index)])))}
async function tick(){try{await load()}catch(error){errorBox.innerHTML='<div class="error">'+esc(error.message)+'</div>';count.textContent='Offline'}}
document.querySelector('#back').onclick=()=>{detail.hidden=true;list.hidden=false};
document.querySelector('#token').onclick=()=>{const token=prompt('Bridge token',localStorage.getItem('codex-esp32-display-token')||'');if(token!==null){if(token)localStorage.setItem('codex-esp32-display-token',token);else localStorage.removeItem('codex-esp32-display-token');tick()}};
tick();setInterval(tick,2000);
</script></body></html>`;
