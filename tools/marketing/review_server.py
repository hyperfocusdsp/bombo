#!/usr/bin/env python3
"""Bombo asset review server — before/after images, approve/reject, comments.
Writes decisions to ~/Pictures/bombo_screenshots/review_decisions.json."""
import json, os, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HOME = os.path.expanduser("~")
ASSETS = os.path.join(HOME, "repos/hyperfocus/src/assets")
SHOTS = os.path.join(HOME, "Pictures/bombo_screenshots")
OLD = "/tmp/bombo_review_old"
DECISIONS_FILE = os.path.join(SHOTS, "review_decisions.json")
PORT = 8473

THEMES = ["vault", "bandw", "nightrun", "matrix", "cyber", "plasma", "fallout"]

# Whitelist of servable images: key -> path
FINAL = "/tmp/bombo_review/final"
IMAGES = {
    "new_hero":   os.path.join(FINAL, "bombo-ui.webp"),
    "old_hero":   os.path.join(OLD, "bombo-ui.webp"),
    "new_og":     os.path.join(FINAL, "bombo-og.png"),
    "old_og":     os.path.join(OLD, "bombo-og.png"),
    "new_lineup": os.path.join(FINAL, "bombo-themes.webp"),
    "old_lineup": os.path.join(OLD, "bombo-themes.webp"),
}
for t in THEMES:
    IMAGES[f"theme_{t}"] = os.path.join(SHOTS, f"{t}.png")

# Review items shown as cards (order matters).
ITEMS = [
    {"id": "hero", "title": "Hero — bombo-ui.webp (FALLOUT lead)",
     "note": "Now leads with the FALLOUT finish: photoreal chassis, green VGA-phosphor scope, tall Crusher kick, cut onto the brand grid. Before = the original VAULT hero.",
     "new": "new_hero", "old": "old_hero", "layout": "tall"},
    {"id": "og", "title": "Social share card — bombo-og.png (FALLOUT)",
     "note": "1200x630 OG card: FALLOUT chassis + BOMBO wordmark / tagline / formats in Plex, amber accent. Before = the old VAULT-based card.",
     "new": "new_og", "old": "old_og", "layout": "wide"},
    {"id": "lineup", "title": "Lineup — SHELVED (silent bonus, NOT shipping on KVR)",
     "note": "The 7-finish tessellation. Per the new plan it does NOT ship on KVR/site — the other skins are an unannounced over-deliver. Kept for a post-launch reveal. Approve = OK to shelve.",
     "new": "new_lineup", "old": "old_lineup", "layout": "wide"},
    {"id": "theme_vault",   "title": "VAULT (standard issue)",   "note": "Source shot feeding the lineup + hero.", "new": "theme_vault",   "layout": "tall"},
    {"id": "theme_bandw",   "title": "BANDW (manual blueprint)", "note": "Source shot feeding the lineup.",        "new": "theme_bandw",   "layout": "tall"},
    {"id": "theme_nightrun","title": "NIGHTRUN",                 "note": "Source shot feeding the lineup.",        "new": "theme_nightrun","layout": "tall"},
    {"id": "theme_matrix",  "title": "MATRIX",                   "note": "Source shot feeding the lineup.",        "new": "theme_matrix",  "layout": "tall"},
    {"id": "theme_cyber",   "title": "CYBER",                    "note": "Source shot feeding the lineup.",        "new": "theme_cyber",   "layout": "tall"},
    {"id": "theme_plasma",  "title": "PLASMA",                   "note": "Source shot feeding the lineup.",        "new": "theme_plasma",  "layout": "tall"},
    {"id": "theme_fallout", "title": "FALLOUT (the new 7th)",    "note": "The newly-advertised finish. CRT/VGA green scope, photoreal rack art, Bombo logo on the nose.", "new": "theme_fallout", "layout": "tall"},
    {"id": "copy", "title": "Page copy — bombo.mdx (FALLOUT-forward)", "type": "text",
     "note": "Enumerated theme list removed everywhere; FALLOUT leads, flat mode nodded to, no spoilers. Lineup section deleted.",
     "content": (
        "featureList:  \"Photoreal FALLOUT finish with a green VGA-phosphor scope - plus more\n"
        "               runtime-switchable skins, flat blueprint mode included\"\n\n"
        "what's here:  \"FALLOUT finish - a weathered, photoreal chassis with a green VGA-phosphor\n"
        "               scope. Not the only skin in the unit, either - flat and blueprint finishes\n"
        "               are in there too, switchable at runtime with no reload.\"\n\n"
        "specs table:  \"Finishes | FALLOUT photoreal, plus more runtime-switchable skins\n"
        "               (flat/blueprint included)\"\n\n"
        "REMOVED:      the whole '## The lineup' section + the 7-up image (kept the surprise).\n"
        "default skin: already FALLOUT on first launch (no code change needed).")},
]

CTYPE = {".webp": "image/webp", ".png": "image/png"}

HTML = r"""<!doctype html>
<html><head><meta charset="utf-8"><title>Bombo asset review</title>
<style>
  :root{--graphite:#0E0F12;--surface:#16181d;--surface2:#1d2026;--bone:#F4F1EA;--amber:#FFB800;--muted:#8E93A0;--ok:#3ddc84;--no:#ff5a52;}
  *{box-sizing:border-box}
  body{margin:0;background:var(--graphite);color:var(--bone);font:15px/1.5 -apple-system,Segoe UI,Roboto,sans-serif}
  header{position:sticky;top:0;z-index:10;background:rgba(14,15,18,.92);backdrop-filter:blur(8px);border-bottom:1px solid #2a2d33;padding:14px 24px;display:flex;align-items:center;gap:20px}
  header h1{font-size:17px;margin:0;font-weight:600}
  header .tally{color:var(--muted);font-size:13px;font-variant-numeric:tabular-nums}
  header .spacer{flex:1}
  button.act{cursor:pointer;border:none;border-radius:6px;padding:9px 16px;font-weight:600;font-size:14px}
  .save{background:var(--amber);color:#1a1a1a}
  .finish{background:#2a2d33;color:var(--bone);border:1px solid #3a3d44}
  #status{color:var(--muted);font-size:13px;min-width:120px}
  main{max-width:1100px;margin:0 auto;padding:24px}
  .card{background:var(--surface);border:1px solid #24272e;border-radius:12px;margin:0 0 22px;overflow:hidden}
  .card.approve{border-color:rgba(61,220,132,.5)}
  .card.reject{border-color:rgba(255,90,82,.5)}
  .card h2{font-size:16px;margin:0;padding:16px 20px 4px}
  .card .note{color:var(--muted);font-size:13px;padding:0 20px 14px}
  .imgs{display:flex;gap:16px;padding:0 20px 16px;flex-wrap:wrap;align-items:flex-start}
  .imgwrap{flex:1;min-width:240px}
  .imgwrap .lbl{font-size:11px;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);margin-bottom:6px}
  .imgwrap.before .lbl{color:#b06a6a}
  .imgwrap.after .lbl{color:var(--amber)}
  .imgwrap img{width:100%;border-radius:8px;background:#000;border:1px solid #2a2d33;display:block;cursor:zoom-in}
  .tall .imgwrap{max-width:340px}
  .wide img{}
  .single .imgwrap{max-width:420px}
  pre.copy{background:#0b0c0e;border:1px solid #24272e;border-radius:8px;margin:0 20px 16px;padding:14px;font-size:12.5px;color:#cdd2da;overflow-x:auto;white-space:pre}
  .controls{display:flex;gap:12px;align-items:center;padding:14px 20px;border-top:1px solid #24272e;background:var(--surface2);flex-wrap:wrap}
  .seg{display:flex;border:1px solid #3a3d44;border-radius:8px;overflow:hidden}
  .seg button{cursor:pointer;background:transparent;color:var(--muted);border:none;padding:8px 18px;font-weight:600;font-size:13px}
  .seg button.sel.app{background:var(--ok);color:#0c1f14}
  .seg button.sel.rej{background:var(--no);color:#2a0c0a}
  .seg button:not(.sel):hover{color:var(--bone)}
  .controls textarea{flex:1;min-width:240px;background:#0b0c0e;color:var(--bone);border:1px solid #2a2d33;border-radius:8px;padding:9px 12px;font:13px/1.4 inherit;resize:vertical;min-height:38px}
  .lightbox{position:fixed;inset:0;background:rgba(0,0,0,.9);display:none;align-items:center;justify-content:center;z-index:100;cursor:zoom-out;padding:30px}
  .lightbox img{max-width:100%;max-height:100%;border-radius:8px}
  footer{color:var(--muted);font-size:12px;text-align:center;padding:0 0 40px}
</style></head>
<body>
<header>
  <h1>Bombo &mdash; asset review</h1>
  <span class="tally" id="tally"></span>
  <span class="spacer"></span>
  <span id="status"></span>
  <button class="act save" onclick="save(false)">Save</button>
  <button class="act finish" onclick="save(true)">Save &amp; finish</button>
</header>
<main id="cards"></main>
<footer>Decisions write to <code>~/Pictures/bombo_screenshots/review_decisions.json</code>. &ldquo;Save &amp; finish&rdquo; closes the server.</footer>
<div class="lightbox" id="lb" onclick="this.style.display='none'"><img id="lbimg"></div>
<script>
const ITEMS = __CONFIG__;
let state = {};
ITEMS.forEach(i => state[i.id] = {decision:null, comment:""});

function esc(s){return (s||"").replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}

function zoom(src){document.getElementById('lbimg').src=src;document.getElementById('lb').style.display='flex';}

function render(){
  const root = document.getElementById('cards');
  root.innerHTML = '';
  ITEMS.forEach(item => {
    const st = state[item.id];
    const card = document.createElement('div');
    card.className = 'card' + (st.decision ? ' '+st.decision : '');
    let imgsHtml = '';
    if(item.type === 'text'){
      imgsHtml = '<pre class="copy">'+esc(item.content)+'</pre>';
    } else {
      const cls = item.old ? item.layout : ('single '+item.layout);
      let inner = '';
      if(item.old){
        inner += '<div class="imgwrap before"><div class="lbl">Before</div><img src="/img/'+item.old+'" onclick="zoom(this.src)"></div>';
        inner += '<div class="imgwrap after"><div class="lbl">After (new)</div><img src="/img/'+item.new+'" onclick="zoom(this.src)"></div>';
      } else {
        inner += '<div class="imgwrap after"><img src="/img/'+item.new+'" onclick="zoom(this.src)"></div>';
      }
      imgsHtml = '<div class="imgs '+cls+'">'+inner+'</div>';
    }
    card.innerHTML =
      '<h2>'+esc(item.title)+'</h2>'+
      '<div class="note">'+esc(item.note)+'</div>'+
      imgsHtml+
      '<div class="controls">'+
        '<div class="seg">'+
          '<button class="app'+(st.decision==='approve'?' sel':'')+'" onclick="setd(\''+item.id+'\',\'approve\')">&#10003; Approve</button>'+
          '<button class="rej'+(st.decision==='reject'?' sel':'')+'" onclick="setd(\''+item.id+'\',\'reject\')">&#10007; Reject</button>'+
        '</div>'+
        '<textarea placeholder="Comment / change request (optional)" oninput="setc(\''+item.id+'\',this.value)">'+esc(st.comment)+'</textarea>'+
      '</div>';
    root.appendChild(card);
  });
  tally();
}
function setd(id,d){ state[id].decision = (state[id].decision===d?null:d); persistLocal(); render(); }
function setc(id,v){ state[id].comment = v; persistLocal(); }
function tally(){
  let a=0,r=0,p=0;
  ITEMS.forEach(i=>{const d=state[i.id].decision; if(d==='approve')a++;else if(d==='reject')r++;else p++;});
  document.getElementById('tally').textContent = a+' approved · '+r+' rejected · '+p+' pending';
}
function persistLocal(){ localStorage.setItem('bombo_review', JSON.stringify(state)); }
function save(finish){
  const s=document.getElementById('status'); s.textContent='Saving…';
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({items:state, finished:finish})})
  .then(r=>r.json()).then(j=>{
    s.textContent = finish ? 'Saved. Server closing — you can close this tab.' : 'Saved ✓';
    if(!finish) setTimeout(()=>{s.textContent='';},2500);
  }).catch(e=>{s.textContent='Save failed: '+e;});
}
// hydrate from any prior saved decisions, then localStorage
fetch('/decisions').then(r=>r.ok?r.json():null).then(j=>{
  if(j && j.items){ Object.keys(j.items).forEach(k=>{if(state[k])state[k]=j.items[k];}); }
  const ls = localStorage.getItem('bombo_review');
  if(ls){ try{const o=JSON.parse(ls); Object.keys(o).forEach(k=>{if(state[k])state[k]=o[k];});}catch(e){} }
  render();
}).catch(()=>render());
</script>
</body></html>"""


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, ctype, body):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            html = HTML.replace("__CONFIG__", json.dumps(ITEMS))
            self._send(200, "text/html; charset=utf-8", html.encode("utf-8"))
        elif path.startswith("/img/"):
            key = path[len("/img/"):]
            fp = IMAGES.get(key)
            if fp and os.path.isfile(fp):
                ext = os.path.splitext(fp)[1].lower()
                with open(fp, "rb") as f:
                    self._send(200, CTYPE.get(ext, "application/octet-stream"), f.read())
            else:
                self._send(404, "text/plain", b"no image")
        elif path == "/decisions":
            if os.path.isfile(DECISIONS_FILE):
                with open(DECISIONS_FILE, "rb") as f:
                    self._send(200, "application/json", f.read())
            else:
                self._send(404, "application/json", b"{}")
        else:
            self._send(404, "text/plain", b"not found")

    def do_POST(self):
        if self.path == "/save":
            length = int(self.headers.get("Content-Length", 0))
            data = json.loads(self.rfile.read(length) or b"{}")
            with open(DECISIONS_FILE, "w") as f:
                json.dump(data, f, indent=2)
            print("REVIEW_SAVED finished=%s :: %s" % (
                data.get("finished"), json.dumps(data.get("items", {})), ), flush=True)
            self._send(200, "application/json", b'{"ok":true}')
            if data.get("finished"):
                threading.Thread(target=self.server.shutdown, daemon=True).start()
        else:
            self._send(404, "text/plain", b"not found")

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    httpd = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print("Bombo review server on http://localhost:%d" % PORT, flush=True)
    httpd.serve_forever()
    print("Server stopped.", flush=True)
