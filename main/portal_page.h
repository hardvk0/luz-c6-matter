#pragma once
// Plantilla de la página del portal (sin dependencias externas: funciona sin Internet).

static const char PAGE_HEAD[] = R"HTML(<!doctype html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>Configurar luz</title>
<style>
:root{--bg:#f3f5f8;--card:#fff;--ink:#1c2330;--mute:#5d6878;--line:#d9dee6;--acc:#e0a21b;--acc-ink:#1c1500;--bad:#b3261e}
@media(prefers-color-scheme:dark){:root{--bg:#12151b;--card:#1a1f27;--ink:#e8ebf0;--mute:#98a2b3;--line:#2b323d;--acc:#f0b93d;--bad:#ff8a80}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;padding:max(16px,env(safe-area-inset-top)) 16px 40px}
main{max-width:560px;margin:0 auto}
header{display:flex;align-items:center;justify-content:space-between;gap:16px;margin:8px 0 20px}
h1{font-size:1.35rem;margin:0;font-weight:650}
header p{margin:0;color:var(--mute);font-size:.88rem}
.lamp{display:flex;align-items:center;gap:10px;padding:8px 16px 8px 8px;border:2px solid var(--line);border-radius:999px;background:var(--card);color:var(--ink);font:inherit;cursor:pointer}
.lamp i{width:28px;height:28px;border-radius:50%;background:var(--line);transition:background .15s,box-shadow .15s}
.lamp[data-on="1"] i{background:var(--acc);box-shadow:0 0 14px 2px var(--acc)}
fieldset{border:1px solid var(--line);background:var(--card);border-radius:12px;margin:0 0 16px;padding:8px 16px 16px}
legend{padding:0 6px;font-weight:650}
label{display:block;margin-top:12px;font-size:.92rem}
input,select,textarea{width:100%;margin-top:4px;padding:10px 12px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--ink);font:inherit}
textarea{font-family:ui-monospace,Menlo,monospace;font-size:.82rem;min-height:84px}
.chk{display:flex;gap:10px;align-items:center}.chk input{width:auto;margin:0}
.hint{color:var(--mute);font-size:.84rem;margin:4px 0 0}
.go,.sec{width:100%;font:inherit;cursor:pointer;border-radius:10px;padding:12px}
.go{border:0;background:var(--acc);color:var(--acc-ink);font-weight:650}
.sec{margin-top:8px;border:1px solid var(--line);background:transparent;color:var(--ink)}
.danger{color:var(--bad);border-color:var(--bad)}
code{font-family:ui-monospace,Menlo,monospace;background:var(--bg);padding:1px 5px;border-radius:5px;word-break:break-all}
.err{border:1px solid var(--bad);color:var(--bad);border-radius:10px;padding:10px 14px;margin-bottom:16px}
:focus-visible{outline:3px solid var(--acc);outline-offset:2px}
</style></head><body><main>
)HTML";

static const char PAGE_SCRIPT[] = R"HTML(
<script>
const $=s=>document.querySelector(s);
async function rel(m){try{const r=await fetch('/relay',{method:m});$('#lamp').dataset.on=(await r.text())==='ON'?'1':'0'}catch(e){}}
rel('GET');
$('#ota').onsubmit=e=>{e.preventDefault();const f=$('#fw').files[0];if(!f)return;
 const x=new XMLHttpRequest();x.open('POST','/ota');
 x.upload.onprogress=p=>$('#pg').textContent=Math.round(p.loaded/p.total*100)+' %';
 x.onload=()=>$('#pg').textContent=x.status==200?'Listo. Reiniciando…':'Error: '+x.responseText;
 x.onerror=()=>$('#pg').textContent='Error de conexión';x.send(f)};
</script></main></body></html>
)HTML";
