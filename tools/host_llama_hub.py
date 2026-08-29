#!/usr/bin/env python3
"""Host hub: modo Lanzamiento (descargar/arrancar/parar) + modo Inspección (spy)."""
from __future__ import annotations

import argparse
import http.server
import json
import os
import platform
import shutil
import signal
import socket
import socketserver
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Dict, List, Optional

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import host_llama_spy as spy  # noqa: E402

LLAMA_TAG = "b10333"
CACHE_ROOT = Path(os.environ.get("XDG_CACHE_HOME", str(Path.home() / ".cache"))) / "tuide" / "models"
CATALOG_PATH = TOOLS_DIR / "host_models_catalog.json"

print_lock = threading.Lock()
state_lock = threading.Lock()
downloads_lock = threading.Lock()
downloads: Dict[str, Dict[str, Any]] = {}
roles: Dict[str, Dict[str, Any]] = {"chat": {}, "embed": {}}
catalog_extra: List[Dict[str, Any]] = []

CFG = {
    "chat_port": int(os.environ.get("TUIDE_HOST_CHAT_PORT", "8080")),
    "embed_port": int(os.environ.get("TUIDE_HOST_EMBED_PORT", "18765")),
    "web_host": "127.0.0.1",
    "web_port": int(os.environ.get("TUIDE_HOST_WEB_PORT", "18767")),
    "ngl": os.environ.get("TUIDE_HOST_NGL", "99"),
    "chat_ctx": os.environ.get("TUIDE_HOST_CHAT_CTX", "32768"),
    "embed_ctx": os.environ.get("TUIDE_HOST_EMBED_CTX", "2048"),
    "embed_np": os.environ.get("TUIDE_HOST_EMBED_NP", "8"),
}


def spy_enabled() -> bool:
    return os.environ.get("TUIDE_HOST_SPY", "1") != "0"


def log(msg: str) -> None:
    with print_lock:
        sys.stdout.write(f"[hub] {msg}\n")
        sys.stdout.flush()


def human_size(n: int) -> str:
    if n >= 1073741824:
        return f" {n / 1073741824:.1f}G".strip()
    if n >= 1048576:
        return f"{n / 1048576:.0f}M"
    if n >= 1024:
        return f"{n / 1024:.0f}K"
    return f"{n}B"


def alias_from_gguf(path: str) -> str:
    return Path(path).name[:-5] if path.endswith(".gguf") else Path(path).name


def allowed_import_url(url: str) -> bool:
    try:
        p = urllib.parse.urlparse(url.strip())
    except ValueError:
        return False
    if p.scheme != "https":
        return False
    host = (p.hostname or "").lower()
    if host not in ("huggingface.co", "www.huggingface.co"):
        return False
    path = p.path.lower()
    return path.endswith(".gguf") or ".gguf/" in path or path.endswith(".gguf")


def lan_ip() -> str:
    env = os.environ.get("TUIDE_ADVERTISE_HOST", "").strip()
    if env:
        return env
    if platform.system() == "Darwin":
        for iface in ("en0", "en1"):
            try:
                out = subprocess.check_output(
                    ["ipconfig", "getifaddr", iface], text=True, stderr=subprocess.DEVNULL
                ).strip()
                if out:
                    return out
            except (subprocess.CalledProcessError, FileNotFoundError):
                pass
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.connect(("8.8.8.8", 80))
        ip = sock.getsockname()[0]
        sock.close()
        return ip
    except OSError:
        return "127.0.0.1"


def cache_dir() -> Path:
    return Path(os.environ.get("TUIDE_MODELS_CACHE", str(CACHE_ROOT)))


def log_dir() -> Path:
    d = cache_dir() / "host-llama"
    d.mkdir(parents=True, exist_ok=True)
    return d


def load_catalog_file() -> Dict[str, Any]:
    if not CATALOG_PATH.is_file():
        return {"runtime": {"tag": LLAMA_TAG}, "models": []}
    return json.loads(CATALOG_PATH.read_text(encoding="utf-8"))


def model_path(entry: Dict[str, Any]) -> Path:
    raw = entry.get("path")
    if raw:
        return Path(raw)
    return cache_dir() / entry.get("dir", "custom") / entry["filename"]


def shards_present(entry: Dict[str, Any]) -> bool:
    primary = model_path(entry)
    if not primary.is_file():
        return False
    for shard in entry.get("extra_shards") or []:
        sp = primary.parent / shard["filename"]
        if not sp.is_file():
            return False
    return True


def _role_for_disk(rel: str, resolved: str) -> str:
    for saved in catalog_extra:
        if saved.get("path") == resolved or (
            saved.get("filename") and rel.endswith("custom") and saved.get("filename") == Path(resolved).name
        ):
            return str(saved.get("role") or "chat")
    if rel.endswith("intent") or rel.startswith("embed"):
        return "embed"
    return "chat"


def scan_disk_models(curated: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    known = set()
    known_names = set()
    for e in curated:
        known_names.add((e.get("dir"), e.get("filename")))
        try:
            known.add(str(model_path(e).resolve()))
        except OSError:
            continue
    extra: List[Dict[str, Any]] = []
    for rel, default_role in (("l2", "chat"), ("l1", "chat"), ("embed/intent", "embed"), ("custom", "chat")):
        folder = cache_dir() / rel
        if not folder.is_dir():
            continue
        for f in sorted(folder.glob("*.gguf")):
            name = f.name
            if name.endswith(".partial") or "-00002-of-" in name:
                continue
            try:
                resolved = str(f.resolve())
            except OSError:
                continue
            if resolved in known or (rel, name) in known_names:
                continue
            known.add(resolved)
            extra.append({
                "id": f"disk:{rel}:{name}",
                "role": _role_for_disk(rel, resolved) if rel == "custom" else default_role,
                "tier": "disk",
                "label": name,
                "filename": name,
                "dir": rel,
                "path": resolved,
                "url": "",
                "license_note": "local",
                "approx_bytes": f.stat().st_size,
                "pack": "",
            })
    return extra


def all_models() -> List[Dict[str, Any]]:
    curated = list(load_catalog_file().get("models") or [])
    seen_ids = {m.get("id") for m in curated}
    extra: List[Dict[str, Any]] = []
    for e in catalog_extra:
        if e.get("id") in seen_ids:
            continue
        extra.append(e)
        seen_ids.add(e.get("id"))
    for d in scan_disk_models(curated + extra):
        if d.get("id") in seen_ids:
            continue
        extra.append(d)
        seen_ids.add(d.get("id"))
    return curated + extra


def find_model(mid: str) -> Optional[Dict[str, Any]]:
    if not mid:
        return None
    for m in all_models():
        if m.get("id") == mid:
            return m
        p = str(model_path(m))
        if p == mid or Path(p).name == mid:
            return m
    path = Path(mid)
    if path.is_file():
        return {
            "id": f"path:{path.name}",
            "role": "chat",
            "tier": "disk",
            "label": path.name,
            "filename": path.name,
            "path": str(path.resolve()),
            "url": "",
            "approx_bytes": path.stat().st_size,
        }
    return None


def find_llama_server() -> str:
    env = os.environ.get("TUIDE_LLAMA_SERVER", "")
    if env and os.access(env, os.X_OK):
        return env
    which = shutil.which("llama-server")
    if which:
        return which
    for candidate in (
        cache_dir() / "runtime" / f"llama-{LLAMA_TAG}" / "llama-server",
        cache_dir() / "runtime" / f"llama-{LLAMA_TAG}-vulkan" / "llama-server",
        Path("/opt/homebrew/bin/llama-server"),
        Path("/usr/local/bin/llama-server"),
    ):
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return ""


def llama_archive_name() -> str:
    system = platform.system()
    machine = platform.machine().lower()
    tag = LLAMA_TAG
    if system == "Darwin":
        if machine in ("arm64", "aarch64"):
            return f"llama-{tag}-bin-macos-arm64.tar.gz"
        return f"llama-{tag}-bin-macos-x64.tar.gz"
    if machine in ("arm64", "aarch64"):
        return f"llama-{tag}-bin-ubuntu-arm64.tar.gz"
    return f"llama-{tag}-bin-ubuntu-x64.tar.gz"


def wait_http(url: str, tries: int = 90) -> bool:
    for _ in range(tries):
        try:
            urllib.request.urlopen(url, timeout=1).read()
            return True
        except Exception:
            time.sleep(0.4)
    return False


def set_download(did: str, **kwargs: Any) -> None:
    with downloads_lock:
        cur = downloads.get(did, {"id": did, "pct": 0, "error": "", "done": False})
        cur.update(kwargs)
        downloads[did] = cur


def download_url_to_file(url: str, dest: Path, expected: int, did: str) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".partial")
    curl = shutil.which("curl")
    wget = shutil.which("wget")
    if curl:
        cmd = ["curl", "-fL", "--retry", "3", "--connect-timeout", "20", "-o", str(tmp), url]
    elif wget:
        cmd = ["wget", "-O", str(tmp), url]
    else:
        set_download(did, error="hace falta curl o wget", done=True, pct=0)
        return
    set_download(did, pct=0, error="", done=False)
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    while proc.poll() is None:
        if tmp.is_file() and expected > 0:
            pct = min(99, int(tmp.stat().st_size * 100 / expected))
            set_download(did, pct=pct)
        time.sleep(0.4)
    if proc.returncode != 0 or not tmp.is_file() or tmp.stat().st_size < 64:
        tmp.unlink(missing_ok=True)
        set_download(did, error=f"download failed (rc={proc.returncode})", done=True, pct=0)
        return
    tmp.replace(dest)
    set_download(did, pct=100, done=True, error="")


def start_download(entry: Dict[str, Any]) -> str:
    did = entry["id"]
    with downloads_lock:
        cur = downloads.get(did)
        if cur and not cur.get("done") and not cur.get("error"):
            return did

    def worker() -> None:
        try:
            dest = model_path(entry)
            download_url_to_file(entry["url"], dest, int(entry.get("approx_bytes") or 0), did)
            with downloads_lock:
                if downloads.get(did, {}).get("error"):
                    return
            for shard in entry.get("extra_shards") or []:
                sp = dest.parent / shard["filename"]
                sid = did + ":" + shard["filename"]
                download_url_to_file(shard["url"], sp, int(shard.get("approx_bytes") or 0), sid)
                with downloads_lock:
                    err = downloads.get(sid, {}).get("error")
                if err:
                    set_download(did, error=err, done=True)
                    return
            set_download(did, pct=100, done=True, error="")
        except Exception as ex:
            set_download(did, error=str(ex), done=True)

    threading.Thread(target=worker, name=f"dl-{did}", daemon=True).start()
    return did


def role_snapshot(role: str) -> Dict[str, Any]:
    with state_lock:
        r = dict(roles.get(role) or {})
    alive = False
    proc: Optional[subprocess.Popen] = r.get("proc")
    if proc is not None and proc.poll() is None:
        alive = True
    return {
        "running": alive,
        "id": r.get("id") or "",
        "path": r.get("path") or "",
        "label": r.get("label") or "",
        "alias": r.get("alias") or "",
        "public_port": r.get("public_port") or 0,
        "backend_port": r.get("backend_port") or 0,
    }


def stop_role(role: str) -> None:
    with state_lock:
        r = roles.get(role) or {}
        procs = [r.get("spy"), r.get("proc")]
        logf = r.get("log")
        roles[role] = {}
    for p in procs:
        if p is None:
            continue
        try:
            p.terminate()
        except OSError:
            pass
    for p in procs:
        if p is None:
            continue
        try:
            p.wait(timeout=4)
        except Exception:
            try:
                p.kill()
            except OSError:
                pass
    if logf is not None:
        try:
            logf.close()
        except Exception:
            pass
    log(f"{role} parado")


def start_role(role: str, entry: Dict[str, Any]) -> str:
    if role not in ("chat", "embed"):
        return "rol inválido"
    path = model_path(entry)
    if not path.is_file():
        return f"GGUF ausente: {path}"
    server = find_llama_server()
    if not server:
        return "no hay llama-server (instálalo en Lanzamiento o TUIDE_LLAMA_SERVER)"
    stop_role(role)
    public_port = CFG["chat_port"] if role == "chat" else CFG["embed_port"]
    use_spy = spy_enabled()
    backend_port = public_port + 10000 if use_spy else public_port
    bind_host = "127.0.0.1" if use_spy else "0.0.0.0"
    alias = os.environ.get("TUIDE_L2_API_MODEL", "").strip() or alias_from_gguf(str(path))
    lib_dir = str(Path(server).resolve().parent)
    env = os.environ.copy()
    for key in ("DYLD_LIBRARY_PATH", "LD_LIBRARY_PATH"):
        prev = env.get(key, "")
        env[key] = lib_dir if not prev else f"{lib_dir}:{prev}"
    cmd = [
        server, "-m", str(path), "--host", bind_host, "--port", str(backend_port),
        "-ngl", str(CFG["ngl"]),
    ]
    if role == "chat":
        cmd += ["-c", str(CFG["chat_ctx"]), "--alias", alias]
    else:
        cmd += ["--embedding", "--pooling", "mean", "-c", str(CFG["embed_ctx"]),
                "-np", str(CFG["embed_np"])]
    logf = open(log_dir() / f"{role}.log", "ab")
    proc = subprocess.Popen(cmd, stdout=logf, stderr=subprocess.STDOUT, env=env)
    health = f"http://127.0.0.1:{backend_port}/health"
    if not wait_http(health):
        err = f"{role} /health no listo; ver {log_dir() / (role + '.log')}"
        try:
            proc.terminate()
        except OSError:
            pass
        return err
    spy_proc = None
    jsonl = str(log_dir() / "spy.jsonl")
    spy.jsonl_path = jsonl
    if use_spy:
        spy_cmd = [
            sys.executable, str(TOOLS_DIR / "host_llama_spy.py"),
            "--listen", f"0.0.0.0:{public_port}",
            "--backend", f"127.0.0.1:{backend_port}",
            "--tag", "chat" if role == "chat" else "embed",
            "--jsonl", jsonl,
        ]
        spy_proc = subprocess.Popen(spy_cmd, env=env)
    with state_lock:
        roles[role] = {
            "id": entry.get("id") or "",
            "path": str(path),
            "label": entry.get("label") or path.name,
            "alias": alias,
            "proc": proc,
            "spy": spy_proc,
            "log": logf,
            "public_port": public_port,
            "backend_port": backend_port,
        }
    log(f"{role} → {path.name} :{public_port}")
    return ""


def preferred_chat() -> Optional[Dict[str, Any]]:
    installed = [m for m in all_models() if m.get("role") == "chat" and shards_present(m)]
    order = ("32b", "14b", "7b", "3b", "1.5b")
    for key in order:
        for m in installed:
            name = (m.get("filename") or m.get("id") or "").lower()
            if key in name:
                return m
    return installed[0] if installed else None


def preferred_embed() -> Optional[Dict[str, Any]]:
    installed = [m for m in all_models() if m.get("role") == "embed" and shards_present(m)]
    for m in installed:
        if "nomic-embed" in (m.get("filename") or "").lower():
            return m
    return installed[0] if installed else None


def catalog_payload() -> Dict[str, Any]:
    items = []
    for m in all_models():
        mid = m.get("id") or ""
        with downloads_lock:
            dl = dict(downloads.get(mid) or {})
        running = False
        snap_chat = role_snapshot("chat")
        snap_embed = role_snapshot("embed")
        if snap_chat.get("id") == mid and snap_chat.get("running"):
            running = True
        if snap_embed.get("id") == mid and snap_embed.get("running"):
            running = True
        installed = shards_present(m)
        status = "missing"
        if running:
            status = "running"
        elif dl and not dl.get("done"):
            status = "downloading"
        elif installed:
            status = "installed"
        items.append({
            "id": mid,
            "role": m.get("role") or "chat",
            "tier": m.get("tier") or "",
            "label": m.get("label") or m.get("filename"),
            "filename": m.get("filename") or "",
            "license_note": m.get("license_note") or "",
            "approx_bytes": int(m.get("approx_bytes") or 0),
            "size": human_size(int(m.get("approx_bytes") or 0)),
            "installed": installed,
            "status": status,
            "download_pct": int(dl.get("pct") or (100 if installed else 0)),
            "download_error": dl.get("error") or "",
            "url": m.get("url") or "",
            "pack": m.get("pack") or "",
        })
    return {"models": items}


def runtime_payload() -> Dict[str, Any]:
    path = find_llama_server()
    return {
        "llama_server": path,
        "found": bool(path),
        "tag": LLAMA_TAG,
        "archive": llama_archive_name(),
        "cache": str(cache_dir()),
    }


def status_payload() -> Dict[str, Any]:
    chat = role_snapshot("chat")
    embed = role_snapshot("embed")
    adv = lan_ip()
    return {
        "ask": bool(chat.get("running")),
        "busy": spy.chat_busy(),
        "chat": chat,
        "embed": embed,
        "advertise": adv,
        "chat_port": CFG["chat_port"],
        "embed_port": CFG["embed_port"],
        "web_port": CFG["web_port"],
        "runtime": runtime_payload(),
        "vm": {
            "api_base": f"http://{adv}:{CFG['chat_port']}/v1" if chat.get("running") else "",
            "api_model": chat.get("alias") or "",
            "embed_host": adv if embed.get("running") else "",
            "embed_port": CFG["embed_port"] if embed.get("running") else 0,
        },
    }


def tail_jsonl(off: int) -> Dict[str, Any]:
    path = spy.jsonl_path or str(log_dir() / "spy.jsonl")
    if not path or not os.path.isfile(path):
        return {"off": 0, "lines": []}
    size = os.path.getsize(path)
    if off > size:
        off = 0
    with open(path, "rb") as f:
        f.seek(off)
        raw = f.read(2 * 1024 * 1024)
    if raw and not raw.endswith(b"\n"):
        last_nl = raw.rfind(b"\n")
        raw = raw[: last_nl + 1] if last_nl >= 0 else b""
    new_off = off + len(raw)
    lines = [ln for ln in raw.decode("utf-8", errors="replace").split("\n") if ln]
    return {"off": new_off, "lines": lines}


def install_runtime(did: str = "runtime") -> None:
    set_download(did, pct=0, error="", done=False)
    name = llama_archive_name()
    url = f"https://github.com/ggml-org/llama.cpp/releases/download/{LLAMA_TAG}/{name}"
    runtime = cache_dir() / "runtime"
    archive = runtime / name
    download_url_to_file(url, archive, 0, did)
    with downloads_lock:
        err = downloads.get(did, {}).get("error")
    if err:
        return
    set_download(did, pct=90)
    try:
        subprocess.check_call(["tar", "-xzf", str(archive), "-C", str(runtime)])
    except subprocess.CalledProcessError as ex:
        set_download(did, error=f"extract failed: {ex}", done=True)
        return
    set_download(did, pct=100, done=True, error="")


HUB_HTML = r"""<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<title>tuide host</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
:root {
  --bg:#101114; --panel:#181a20; --line:#2c3038; --txt:#e7e9ee; --muted:#8d939e;
  --chat:#5ec8d8; --embed:#c9a0e8; --ok:#7dcea0; --warn:#e8c547; --err:#e07a7a;
  --chrome:48px;
}
* { box-sizing:border-box; }
html,body { margin:0; height:100%; overflow:hidden; background:var(--bg); color:var(--txt);
  font:13px/1.45 ui-monospace, SFMono-Regular, Menlo, Monaco, monospace; }
#chrome { height:var(--chrome); display:flex; align-items:center; gap:10px; padding:0 14px;
  border-bottom:1px solid var(--line); background:#14161c; flex-shrink:0; }
#chrome h1 { margin:0; font-size:13px; font-weight:600; letter-spacing:.04em; }
#modes { display:flex; gap:6px; }
#chrome-status { color:var(--muted); font-size:11px; margin-left:auto; white-space:nowrap;
  overflow:hidden; text-overflow:ellipsis; max-width:46vw; }
button { background:#0c0d10; color:var(--muted); border:1px solid var(--line);
  border-radius:6px; padding:6px 10px; cursor:pointer; font:inherit; }
button.on { color:var(--txt); border-color:#4a5568; background:#22252c; }
button.ok { color:var(--ok); border-color:#355a45; }
button.danger { color:var(--err); border-color:#5a3535; }
button:disabled { opacity:.45; cursor:not-allowed; }
#views { height:calc(100% - var(--chrome)); }
.view { height:100%; display:none; }
.view.on { display:block; }
#view-inspect iframe { width:100%; height:100%; border:0; background:var(--bg); }
#view-launch { overflow:auto; padding:16px 20px 40px; }
h2 { font-size:12px; text-transform:uppercase; letter-spacing:.08em; color:var(--muted);
  margin:22px 0 10px; }
.row { display:flex; gap:10px; flex-wrap:wrap; align-items:center; }
.card { background:var(--panel); border:1px solid var(--line); border-radius:8px;
  padding:12px 14px; margin-bottom:8px; display:grid;
  grid-template-columns:minmax(0,1fr) auto; gap:8px 12px; align-items:center; }
.card .name { font-weight:600; }
.card .meta { color:var(--muted); font-size:11px; margin-top:3px; }
.badge { font-size:10px; padding:2px 6px; border-radius:4px; border:1px solid var(--line);
  color:var(--muted); margin-left:6px; }
.badge.l2 { color:var(--ok); border-color:#355a45; }
.badge.embed { color:var(--embed); }
.badge.run { color:var(--warn); border-color:#6a5a20; }
.actions { display:flex; gap:6px; flex-wrap:wrap; justify-content:flex-end; }
input, select { background:#0c0d10; color:var(--txt); border:1px solid var(--line);
  border-radius:6px; padding:6px 8px; font:inherit; }
input[type=text] { min-width:280px; flex:1; }
#hint, #import-msg, #rt-msg { color:var(--warn); font-size:12px; min-height:1.2em; }
pre.vm { background:#0c0d10; border:1px solid var(--line); border-radius:8px; padding:12px;
  overflow:auto; color:#c5c9d1; }
.bar { height:4px; background:#222; border-radius:2px; margin-top:8px; grid-column:1 / -1; }
.bar > i { display:block; height:100%; background:var(--chat); width:0; }
</style>
</head>
<body>
<div id="chrome">
  <h1>tuide host</h1>
  <div id="modes">
    <button type="button" id="m-launch" class="on">Lanzamiento</button>
    <button type="button" id="m-inspect">Inspección</button>
  </div>
  <div id="chrome-status">arrancando…</div>
</div>
<div id="views">
  <div id="view-launch" class="view on">
    <div class="row" style="margin-bottom:8px">
      <span id="rt-line">runtime…</span>
      <button type="button" id="rt-install" class="ok">Instalar llama-server</button>
    </div>
    <div id="rt-msg"></div>
    <h2>Chat</h2>
    <div id="list-chat"></div>
    <h2>Embeddings</h2>
    <div id="list-embed"></div>
    <h2>Importar GGUF</h2>
    <div class="row">
      <input id="imp-src" type="text" placeholder="https://huggingface.co/…/*.gguf  o  /ruta/local.gguf">
      <select id="imp-role"><option value="chat">chat</option><option value="embed">embed</option></select>
      <button type="button" id="imp-go" class="ok">Añadir</button>
    </div>
    <div id="import-msg"></div>
    <h2>VM Linux</h2>
    <pre class="vm" id="vm-hint">Arranca un modelo para ver los exports.</pre>
    <div id="hint"></div>
  </div>
  <div id="view-inspect" class="view">
    <iframe id="inspect-frame" src="/spy" title="Inspección"></iframe>
  </div>
</div>
<script>
let mode = "launch";
function setMode(m) {
  mode = m === "inspect" ? "inspect" : "launch";
  document.getElementById("view-launch").classList.toggle("on", mode === "launch");
  document.getElementById("view-inspect").classList.toggle("on", mode === "inspect");
  document.getElementById("m-launch").classList.toggle("on", mode === "launch");
  document.getElementById("m-inspect").classList.toggle("on", mode === "inspect");
  const hash = mode === "inspect" ? "#inspect" : "#launch";
  if (location.hash !== hash) history.replaceState(null, "", hash);
}
function esc(s) {
  return String(s || "").replace(/[&<>]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;"}[c]));
}
function tierBadge(m) {
  if (m.tier === "tide-l2") return '<span class="badge l2">TIDE L2</span>';
  if (m.tier === "tide-embed") return '<span class="badge embed">TIDE L0</span>';
  if (m.status === "running") return '<span class="badge run">en ejecución</span>';
  return m.tier ? `<span class="badge">${esc(m.tier)}</span>` : "";
}
function card(m) {
  const run = m.status === "running";
  const inst = m.installed;
  const dl = m.status === "downloading";
  const canDl = !!m.url && !inst && !dl;
  const btns = [];
  if (canDl) btns.push(`<button data-act="download" data-id="${esc(m.id)}">Descargar</button>`);
  if (inst && !run) btns.push(`<button class="ok" data-act="start" data-id="${esc(m.id)}" data-role="${esc(m.role)}">Lanzar</button>`);
  if (run) {
    btns.push(`<button data-act="restart" data-id="${esc(m.id)}" data-role="${esc(m.role)}">Reiniciar</button>`);
    btns.push(`<button class="danger" data-act="stop" data-role="${esc(m.role)}">Parar</button>`);
  }
  const pct = m.status === "downloading" ? m.download_pct : (inst ? 100 : 0);
  const err = m.download_error ? `<div class="meta" style="color:var(--err)">${esc(m.download_error)}</div>` : "";
  return `<div class="card">
    <div><div class="name">${esc(m.label)} ${tierBadge(m)}</div>
    <div class="meta">${esc(m.filename)} · ${esc(m.size)} · ${esc(m.license_note)} · ${esc(m.status)}</div>${err}
    <div class="bar"><i style="width:${pct}%"></i></div></div>
    <div class="actions">${btns.join("")}</div></div>`;
}
async function api(path, opts) {
  const res = await fetch(path, opts);
  const data = await res.json().catch(() => ({}));
  if (!res.ok && data.error) throw new Error(data.error);
  return data;
}
function bindCards(root) {
  root.querySelectorAll("button[data-act]").forEach(btn => {
    btn.onclick = async () => {
      const act = btn.dataset.act;
      const body = {id: btn.dataset.id, role: btn.dataset.role};
      try {
        if (act === "download") await api("/api/download", {method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify(body)});
        if (act === "start") await api("/api/start", {method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify(body)});
        if (act === "stop") await api("/api/stop", {method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify({role: body.role})});
        if (act === "restart") await api("/api/restart", {method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify(body)});
        await refresh();
      } catch (e) {
        document.getElementById("hint").textContent = e.message || String(e);
      }
    };
  });
}
async function refresh() {
  try {
    const [cat, st] = await Promise.all([api("/api/catalog"), api("/api/status")]);
    const chat = (cat.models || []).filter(m => m.role === "chat");
    const embed = (cat.models || []).filter(m => m.role === "embed");
    const lc = document.getElementById("list-chat");
    const le = document.getElementById("list-embed");
    lc.innerHTML = chat.map(card).join("") || '<div class="meta">sin modelos de chat</div>';
    le.innerHTML = embed.map(card).join("") || '<div class="meta">sin embeddings</div>';
    bindCards(lc); bindCards(le);
    const rt = st.runtime || {};
    document.getElementById("rt-line").textContent = rt.found
      ? ("llama-server: " + rt.llama_server)
      : "llama-server no encontrado";
    document.getElementById("rt-install").style.display = rt.found ? "none" : "inline-block";
    const ch = st.chat && st.chat.running ? (st.chat.label || "chat") : "chat off";
    const em = st.embed && st.embed.running ? (st.embed.label || "embed") : "embed off";
    document.getElementById("chrome-status").textContent = ch + " · " + em;
    const vm = st.vm || {};
    const lines = [];
    if (vm.api_base) {
      lines.push("export TUIDE_L2_API_BASE=" + vm.api_base);
      lines.push("export TUIDE_L2_API_MODEL=" + vm.api_model);
    }
    if (vm.embed_host) {
      lines.push("export TUIDE_EMBED_HOST=" + vm.embed_host);
      lines.push("export TUIDE_EMBED_PORT=" + vm.embed_port);
    }
    document.getElementById("vm-hint").textContent = lines.join("\n") || "Arranca un modelo para ver los exports.";
  } catch (e) {
    document.getElementById("chrome-status").textContent = "hub no responde";
  }
}
document.getElementById("m-launch").onclick = () => setMode("launch");
document.getElementById("m-inspect").onclick = () => setMode("inspect");
document.getElementById("rt-install").onclick = async () => {
  document.getElementById("rt-msg").textContent = "descargando runtime…";
  try {
    await api("/api/runtime/install", {method:"POST", headers:{"Content-Type":"application/json"}, body:"{}"});
    document.getElementById("rt-msg").textContent = "instalación en curso (mira la barra de un momento)";
  } catch (e) {
    document.getElementById("rt-msg").textContent = e.message || String(e);
  }
};
document.getElementById("imp-go").onclick = async () => {
  const src = document.getElementById("imp-src").value.trim();
  const role = document.getElementById("imp-role").value;
  const msg = document.getElementById("import-msg");
  if (!src) { msg.textContent = "pon una URL o ruta"; return; }
  msg.textContent = "importando…";
  try {
    const data = await api("/api/import", {method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify({src, role})});
    msg.textContent = data.message || "ok";
    await refresh();
  } catch (e) {
    msg.textContent = e.message || String(e);
  }
};
window.addEventListener("hashchange", () => {
  setMode(location.hash === "#inspect" ? "inspect" : "launch");
});
if (location.hash === "#inspect") setMode("inspect");
refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
"""


class HubHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args: Any) -> None:
        return

    def _send(self, code: int, body: bytes, ctype: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code: int, obj: dict) -> None:
        self._send(code, json.dumps(obj, ensure_ascii=False).encode("utf-8"),
                   "application/json; charset=utf-8")

    def _read_json(self) -> Dict[str, Any]:
        try:
            length = int(self.headers.get("Content-Length") or "0")
        except ValueError:
            length = 0
        if length <= 0 or length > 200000:
            return {}
        raw = self.rfile.read(length)
        try:
            obj = json.loads(raw.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return {}
        return obj if isinstance(obj, dict) else {}

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        if path in ("/", "/index.html"):
            self._send(200, HUB_HTML.encode("utf-8"), "text/html; charset=utf-8")
            return
        if path in ("/spy", "/spy/"):
            self._send(200, spy.DASHBOARD_HTML.encode("utf-8"), "text/html; charset=utf-8")
            return
        if path == "/api/catalog":
            self._json(200, catalog_payload())
            return
        if path == "/api/status":
            self._json(200, status_payload())
            return
        if path == "/api/runtime":
            self._json(200, runtime_payload())
            return
        if path == "/api/tail":
            qs = urllib.parse.parse_qs(parsed.query)
            try:
                off = max(0, int(qs.get("off", ["0"])[0]))
            except ValueError:
                off = 0
            self._json(200, tail_jsonl(off))
            return
        self._send(404, b"not found\n", "text/plain")

    def do_POST(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        obj = self._read_json()
        if path == "/api/download":
            entry = find_model(str(obj.get("id") or ""))
            if not entry or not entry.get("url"):
                self._json(400, {"ok": False, "error": "modelo desconocido o sin URL"})
                return
            start_download(entry)
            self._json(200, {"ok": True, "id": entry["id"]})
            return
        if path == "/api/import":
            src = str(obj.get("src") or obj.get("url") or obj.get("path") or "").strip()
            role = str(obj.get("role") or "chat")
            if role not in ("chat", "embed"):
                role = "chat"
            if not src:
                self._json(400, {"ok": False, "error": "src vacío"})
                return
            if os.path.isfile(src) and src.lower().endswith(".gguf"):
                resolved = str(Path(src).resolve())
                entry = {
                    "id": f"custom:{Path(resolved).name}",
                    "role": role,
                    "tier": "custom",
                    "label": Path(resolved).name,
                    "filename": Path(resolved).name,
                    "path": resolved,
                    "url": "",
                    "license_note": "local",
                    "approx_bytes": Path(resolved).stat().st_size,
                    "dir": "custom",
                }
                catalog_extra.append(entry)
                self._json(200, {"ok": True, "id": entry["id"], "message": "añadido desde disco"})
                return
            if not allowed_import_url(src):
                self._json(400, {"ok": False, "error": "solo https://huggingface.co/…/*.gguf o un GGUF local"})
                return
            name = Path(urllib.parse.urlparse(src).path).name or "model.gguf"
            if not name.endswith(".gguf"):
                name += ".gguf"
            dest_dir = "embed/intent" if role == "embed" else "custom"
            entry = {
                "id": f"custom:{name}",
                "role": role,
                "tier": "custom",
                "label": name,
                "filename": name,
                "dir": dest_dir,
                "url": src,
                "license_note": "Hugging Face (terceros)",
                "approx_bytes": 0,
            }
            catalog_extra.append(entry)
            start_download(entry)
            self._json(200, {"ok": True, "id": entry["id"], "message": "descarga iniciada"})
            return
        if path == "/api/start":
            role = str(obj.get("role") or "")
            entry = find_model(str(obj.get("id") or ""))
            if not entry:
                self._json(400, {"ok": False, "error": "modelo desconocido"})
                return
            if role not in ("chat", "embed"):
                role = str(entry.get("role") or "chat")
            err = start_role(role, entry)
            if err:
                self._json(500, {"ok": False, "error": err})
                return
            self._json(200, {"ok": True, "status": status_payload()})
            return
        if path == "/api/stop":
            role = str(obj.get("role") or "")
            if role not in ("chat", "embed"):
                self._json(400, {"ok": False, "error": "role debe ser chat o embed"})
                return
            stop_role(role)
            self._json(200, {"ok": True, "status": status_payload()})
            return
        if path == "/api/restart":
            role = str(obj.get("role") or "")
            entry = find_model(str(obj.get("id") or ""))
            if role not in ("chat", "embed"):
                if entry:
                    role = str(entry.get("role") or "chat")
                else:
                    self._json(400, {"ok": False, "error": "role inválido"})
                    return
            if entry is None:
                snap = role_snapshot(role)
                entry = find_model(str(snap.get("id") or snap.get("path") or ""))
            if not entry:
                self._json(400, {"ok": False, "error": "no hay modelo para reiniciar"})
                return
            err = start_role(role, entry)
            if err:
                self._json(500, {"ok": False, "error": err})
                return
            self._json(200, {"ok": True, "status": status_payload()})
            return
        if path == "/api/runtime/install":
            threading.Thread(target=install_runtime, daemon=True).start()
            self._json(200, {"ok": True})
            return
        if path == "/api/ask":
            chat = role_snapshot("chat")
            if not chat.get("running"):
                self._json(503, {"ok": False, "error": "no hay LLM en este visor"})
                return
            user = str(obj.get("user") or "").strip()
            system = str(obj.get("system") or "")
            if not user:
                self._json(400, {"ok": False, "error": "user vacío"})
                return
            backend_port = int(chat.get("backend_port") or (CFG["chat_port"] + 10000))
            try:
                text, rid, err = spy.run_direct_ask(user, system, "127.0.0.1", backend_port)
            except (TimeoutError, socket.timeout) as ex:
                self._json(504, {"ok": False, "error": f"timeout: {ex}"})
                return
            except OSError as ex:
                self._json(502, {"ok": False, "error": str(ex)})
                return
            if err:
                self._json(502, {"ok": False, "id": rid, "error": err, "chars": len(text)})
                return
            self._json(200, {"ok": True, "id": rid, "chars": len(text)})
            return
        self._send(404, b"not found\n", "text/plain")


class HubServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def cleanup() -> None:
    stop_role("chat")
    stop_role("embed")


def open_browser(url: str) -> None:
    try:
        if platform.system() == "Darwin":
            subprocess.Popen(["open", url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            subprocess.Popen(["xdg-open", url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError:
        log(f"abre el navegador en {url}")


def autostart(chat_token: str, embed_token: str) -> None:
    def resolve(token: str, role: str) -> Optional[Dict[str, Any]]:
        if not token or token in ("none", "off", "0"):
            return None
        if token in ("auto", "*"):
            return preferred_chat() if role == "chat" else preferred_embed()
        found = find_model(token)
        if found:
            return found
        lower = token.lower()
        for m in all_models():
            blob = f"{m.get('id','')} {m.get('filename','')} {m.get('label','')}".lower()
            if lower in blob and m.get("role") == role:
                return m
        return None

    if chat_token:
        entry = resolve(chat_token, "chat")
        if entry and shards_present(entry):
            err = start_role("chat", entry)
            if err:
                log(err)
        elif chat_token not in ("none", "off", "0"):
            log(f"chat no listo para autostart: {chat_token}")
    if embed_token:
        entry = resolve(embed_token, "embed")
        if entry and shards_present(entry):
            err = start_role("embed", entry)
            if err:
                log(err)
        elif embed_token not in ("none", "off", "0"):
            log(f"embed no listo para autostart: {embed_token}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Hub HTML tuide host-llama")
    ap.add_argument("--listen", default=f"127.0.0.1:{CFG['web_port']}")
    ap.add_argument("--mode", choices=("launch", "inspect"), default="launch")
    ap.add_argument("--chat", default="", help="id, ruta, substring o auto")
    ap.add_argument("--embed", default="", help="id, ruta, substring o auto")
    ap.add_argument("--autostart", action="store_true", help="LLM más grande + nomic si hay")
    ap.add_argument("--open-browser", action="store_true")
    args = ap.parse_args()
    spy.jsonl_path = str(log_dir() / "spy.jsonl")
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

    if args.autostart:
        if not args.chat:
            args.chat = "auto"
        if not args.embed:
            args.embed = "auto"
    if args.chat or args.embed:
        autostart(args.chat, args.embed)

    host, port_s = args.listen.rsplit(":", 1)
    CFG["web_host"] = host
    CFG["web_port"] = int(port_s)
    httpd = HubServer((host, int(port_s)), HubHandler)
    mode = args.mode
    url = f"http://{host}:{port_s}/#{mode}"
    log(f"web {url}")
    log(f"cache {cache_dir()}")
    if args.open_browser:
        threading.Timer(0.4, open_browser, args=(url,)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
        cleanup()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
