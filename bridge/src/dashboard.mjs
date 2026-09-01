export const DASHBOARD_HTML = String.raw`<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Codex Attention</title>
<style>
:root{color-scheme:dark;font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#080a0d;color:#f5f7fa}
*{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at 20% 0,#202938 0,transparent 40%),#080a0d}
main{width:min(880px,calc(100% - 32px));margin:0 auto;padding:38px 0 80px}header{display:flex;align-items:end;justify-content:space-between;gap:20px;margin-bottom:24px}
h1{font-size:clamp(30px,5vw,52px);letter-spacing:-.045em;margin:0}.eyebrow{color:#9aa5b5;text-transform:uppercase;letter-spacing:.13em;font-size:12px;font-weight:750;margin-bottom:8px}
.count{font-variant-numeric:tabular-nums;color:#a8b1bf}.grid{display:grid;gap:12px}.card{border:1px solid #2b3442;background:rgba(18,22,29,.9);border-radius:17px;padding:17px 18px;box-shadow:0 12px 32px rgba(0,0,0,.18)}
.card.waiting_approval,.card.waiting_input{border-color:#c99738;background:rgba(52,39,17,.78)}.title{font-size:19px;font-weight:750;line-height:1.25}.meta{display:flex;gap:9px;align-items:center;color:#99a4b4;font-size:13px;margin-top:6px}.preview{color:#c5ccd6;margin-top:12px;line-height:1.4}.badges{display:flex;flex-wrap:wrap;gap:6px;margin-top:13px}.badge{border:1px solid #394352;border-radius:999px;padding:4px 8px;font-size:10px;font-weight:800;letter-spacing:.08em;text-transform:uppercase}.badge.wait{border-color:#d6a646;color:#f3c96d}.badge.unread{border-color:#4f88da;color:#83b5ff}.badge.pin{border-color:#8968dc;color:#ba9dff}.badge.run{border-color:#3d9e89;color:#69d4bd}.empty,.error{border:1px dashed #344050;border-radius:18px;padding:35px;text-align:center;color:#a7b1c0}.error{border-color:#874b55;color:#ffb0bb;margin-bottom:15px;text-align:left;padding:14px 16px}button{background:#202734;color:#eef2f7;border:1px solid #3b4657;border-radius:10px;padding:8px 12px;cursor:pointer}button:hover{background:#2a3443}@media(max-width:600px){header{align-items:start;flex-direction:column}.card{padding:15px}}
</style>
</head>
<body><main><header><div><div class="eyebrow">Physical inbox preview</div><h1>Codex Attention</h1></div><div><span class="count" id="count">Connecting…</span> <button id="token">Token</button></div></header><div id="error"></div><div id="list" class="grid"></div></main>
<script>
const list=document.querySelector('#list'),count=document.querySelector('#count'),errorBox=document.querySelector('#error');
const labels={waiting_approval:'Needs approval',waiting_input:'Needs input',new_result:'New result',unread:'Unread',pinned:'Pinned'};
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#039;'}[c]));
const age=s=>s<60?'now':s<3600?Math.floor(s/60)+'m':s<86400?Math.floor(s/3600)+'h':Math.floor(s/86400)+'d';
function badge(reason){const cls=reason.startsWith('waiting')?'wait':reason==='pinned'?'pin':reason==='unread'||reason==='new_result'?'unread':'';return '<span class="badge '+cls+'">'+esc(labels[reason]||reason)+'</span>'}
async function load(){let token=localStorage.getItem('codex-attention-token')||'';let response=await fetch('/api/v1/attention',{cache:'no-store',headers:token?{Authorization:'Bearer '+token}:{}});if(response.status===401){token=prompt('Bridge token')||'';if(token){localStorage.setItem('codex-attention-token',token);return load()}}if(!response.ok)throw new Error('HTTP '+response.status);const data=await response.json();count.textContent=data.totalCount+' item'+(data.totalCount===1?'':'s');errorBox.innerHTML=data.diagnostics?.sourceError?'<div class="error">'+esc(data.diagnostics.sourceError)+'</div>':'';if(!data.items.length){list.innerHTML='<div class="empty"><strong>Inbox clear.</strong><br>No unread, pinned, or waiting threads.</div>';return}list.innerHTML=data.items.map(i=>'<article class="card '+esc(i.status)+'"><div class="title">'+esc(i.title)+'</div><div class="meta"><span>'+esc(i.project)+'</span><span>·</span><span>'+age(i.ageSeconds)+'</span></div>'+(i.preview&&i.preview!==i.title?'<div class="preview">'+esc(i.preview)+'</div>':'')+'<div class="badges">'+i.reasons.map(badge).join('')+(i.status==='running'?'<span class="badge run">Running</span>':'')+'</div></article>').join('')}
async function tick(){try{await load()}catch(e){errorBox.innerHTML='<div class="error">'+esc(e.message)+'</div>';count.textContent='Offline'}}
document.querySelector('#token').onclick=()=>{const t=prompt('Bridge token',localStorage.getItem('codex-attention-token')||'');if(t!==null){if(t)localStorage.setItem('codex-attention-token',t);else localStorage.removeItem('codex-attention-token');tick()}};
tick();setInterval(tick,2000);
</script></body></html>`;
