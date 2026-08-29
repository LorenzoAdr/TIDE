#!/usr/bin/env python3
"""Reverse proxy: llama-server stays on loopback; this binds the public port.

Chat completions are forced to stream on the backend so the host terminal can
print tokens as they generate. The VM still receives a single JSON body
(TIDE does not speak SSE yet). Embeddings are forwarded as-is; only a
one-line tally is printed (no vectors).

host_llama_hub.py serves DASHBOARD_HTML as Inspection mode at /spy.
"""
from __future__ import annotations

import argparse
import fcntl
import http.client
import http.server
import json
import os
import socket
import socketserver
import sys
import threading
import time
import urllib.parse
import uuid
from typing import Dict, List, Optional, Tuple

print_lock = threading.Lock()
_force_color: Optional[bool] = None
jsonl_path = ""
jsonl_lock = threading.Lock()
req_seq = 0
req_seq_lock = threading.Lock()
inflight_lock = threading.Lock()
inflight_chat = 0

RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
CYAN = "\033[36m"
GREEN = "\033[32m"
MAG = "\033[35m"
RED = "\033[31m"
YELLOW = "\033[33m"


def configure_color(force: Optional[bool]) -> None:
    global _force_color
    _force_color = force


def use_color() -> bool:
    if _force_color is False:
        return False
    if _force_color is True:
        return True
    if os.environ.get("NO_COLOR"):
        return False
    if os.environ.get("TERM", "") == "dumb":
        return False
    return sys.stdout.isatty()


def paint(style: str, text: str) -> str:
    if not text or not use_color():
        return text
    return f"{style}{text}{RESET}"


def tag_label(tag: str) -> str:
    raw = f"[{tag}]"
    if tag == "embed":
        return paint(MAG + BOLD, raw)
    if tag == "chat":
        return paint(CYAN + BOLD, raw)
    if tag == "direct":
        return paint(YELLOW + BOLD, raw)
    return paint(DIM, raw)


def log(msg: str) -> None:
    with print_lock:
        sys.stdout.write(msg)
        if not msg.endswith("\n"):
            sys.stdout.write("\n")
        sys.stdout.flush()


def write_tok(text: str) -> None:
    if not text:
        return
    with print_lock:
        sys.stdout.write(text)
        sys.stdout.flush()


def stream_open(tag: str) -> None:
    write_tok(f"{tag_label(tag)} ")
    if use_color():
        write_tok(f"{GREEN}▸ ")
    else:
        write_tok("▸ ")


def stream_close() -> None:
    if use_color():
        write_tok(RESET)
    write_tok("\n")


def next_req_id(tag: str) -> str:
    global req_seq
    with req_seq_lock:
        req_seq += 1
        n = req_seq
    return f"{tag}-{int(time.time() * 1000)}-{n}-{uuid.uuid4().hex[:6]}"


def chat_busy() -> bool:
    with inflight_lock:
        return inflight_chat > 0


def jsonl_emit(obj: dict) -> None:
    if not jsonl_path:
        return
    obj.setdefault("ts", time.time())
    line = json.dumps(obj, ensure_ascii=False, default=str) + "\n"
    os.makedirs(os.path.dirname(jsonl_path) or ".", exist_ok=True)
    with jsonl_lock:
        with open(jsonl_path, "a", encoding="utf-8") as f:
            fcntl.flock(f.fileno(), fcntl.LOCK_EX)
            try:
                f.write(line)
                f.flush()
            finally:
                fcntl.flock(f.fileno(), fcntl.LOCK_UN)


class TokBatch:
    def __init__(self, req_id: str, tag: str) -> None:
        self.req_id = req_id
        self.tag = tag
        self.buf: List[str] = []
        self.last = time.monotonic()

    def add(self, text: str) -> None:
        if not text:
            return
        self.buf.append(text)
        now = time.monotonic()
        if sum(len(x) for x in self.buf) >= 240 or now - self.last >= 0.12:
            self.flush()

    def flush(self) -> None:
        if not self.buf:
            return
        jsonl_emit({"kind": "tok", "id": self.req_id, "tag": self.tag, "text": "".join(self.buf)})
        self.buf.clear()
        self.last = time.monotonic()


def parse_request(sock: socket.socket) -> Optional[Tuple[str, str, Dict[str, str], bytes]]:
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            return None
        buf += chunk
        if len(buf) > 16 * 1024 * 1024:
            return None
    head, rest = buf.split(b"\r\n\r\n", 1)
    lines = head.split(b"\r\n")
    if not lines:
        return None
    parts = lines[0].decode("iso-8859-1", "replace").split(" ")
    if len(parts) < 2:
        return None
    method, path = parts[0], parts[1]
    headers: Dict[str, str] = {}
    for raw in lines[1:]:
        if b":" not in raw:
            continue
        k, v = raw.split(b":", 1)
        headers[k.decode("iso-8859-1").strip().lower()] = v.decode("iso-8859-1").strip()
    if headers.get("expect", "").lower() == "100-continue":
        sock.sendall(b"HTTP/1.1 100 Continue\r\n\r\n")
    length = int(headers.get("content-length", "0") or "0")
    body = rest
    while len(body) < length:
        chunk = sock.recv(min(65536, length - len(body)))
        if not chunk:
            break
        body += chunk
    return method, path, headers, body[:length]


def hop_headers() -> set:
    return {
        "connection",
        "keep-alive",
        "proxy-connection",
        "transfer-encoding",
        "te",
        "trailer",
        "upgrade",
        "host",
        "content-length",
        "expect",
    }


def fwd_headers(src: Dict[str, str], host: str) -> Dict[str, str]:
    out = {}
    for k, v in src.items():
        if k in hop_headers():
            continue
        out[k] = v
    out["host"] = host
    out["connection"] = "close"
    return out


def path_kind(path: str) -> str:
    p = path.split("?", 1)[0].rstrip("/")
    if p.endswith("chat/completions") or p.endswith("/completion") or p.endswith("/completions"):
        return "chat"
    if "embed" in p.lower():
        return "embed"
    return "other"


# System packs can be tens of KB; user turns are the actual query.
SYSTEM_MAX_CHARS = 4000
USER_MAX_CHARS = 32000
PROMPT_MAX_CHARS = 16000
EMBED_PREVIEW_CHARS = 240


def content_text(content) -> str:
    if content is None:
        return ""
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        bits = []
        for part in content:
            if isinstance(part, dict) and part.get("type") == "text":
                bits.append(str(part.get("text") or ""))
            elif isinstance(part, str):
                bits.append(part)
            else:
                bits.append(str(part))
        return "\n".join(bits)
    return str(content)


def clip_text(text: str, limit: int) -> Tuple[str, bool]:
    if limit <= 0 or len(text) <= limit:
        return text, False
    return text[:limit].rstrip() + "\n…", True


def log_indented(tag: str, text: str, style: str) -> None:
    lines = text.splitlines()
    if not lines:
        log(f"{tag_label(tag)} {paint(style, '  (vacío)')}")
        return
    for line in lines:
        log(f"{tag_label(tag)} {paint(style, '  ' + line)}")


def log_incoming_prompt(tag: str, payload: Optional[dict], method: str, path: str, body_len: int) -> None:
    log("")
    log(f"{tag_label(tag)} {paint(DIM, '── petición ──')} {paint(DIM, method + ' ' + path + f'  {body_len} B')}")
    if not isinstance(payload, dict):
        log(f"{tag_label(tag)} {paint(DIM, '  (cuerpo no JSON)')}")
        return

    msgs = payload.get("messages")
    printed = False
    if isinstance(msgs, list) and msgs:
        for msg in msgs:
            if not isinstance(msg, dict):
                continue
            role = str(msg.get("role") or "message")
            raw = content_text(msg.get("content"))
            limit = SYSTEM_MAX_CHARS if role == "system" else USER_MAX_CHARS
            shown, clipped = clip_text(raw, limit)
            n = len(raw)
            extra = f", recorte {limit}" if clipped else ""
            log(f"{tag_label(tag)} {paint(BOLD, role)} {paint(DIM, f'({n} chars{extra})')}")
            style = DIM if role == "system" else YELLOW
            log_indented(tag, shown, style)
            printed = True
    prompt = payload.get("prompt")
    if not printed and prompt is not None:
        raw = content_text(prompt)
        shown, clipped = clip_text(raw, PROMPT_MAX_CHARS)
        extra = f", recorte {PROMPT_MAX_CHARS}" if clipped else ""
        log(f"{tag_label(tag)} {paint(BOLD, 'prompt')} {paint(DIM, f'({len(raw)} chars{extra})')}")
        log_indented(tag, shown, YELLOW)
        printed = True
    if payload.get("grammar"):
        g = str(payload.get("grammar") or "")
        log(f"{tag_label(tag)} {paint(DIM, f'grammar {len(g)} chars')}")
    if not printed:
        log(f"{tag_label(tag)} {paint(DIM, '  (sin messages/prompt)')}")


def prompt_fields(payload: Optional[dict]) -> dict:
    out = {"system": "", "user": "", "grammar": 0}
    if not isinstance(payload, dict):
        return out
    msgs = payload.get("messages")
    if isinstance(msgs, list):
        for msg in msgs:
            if not isinstance(msg, dict):
                continue
            role = str(msg.get("role") or "")
            raw = content_text(msg.get("content"))
            if role == "system":
                out["system"] = raw
            elif role == "user":
                out["user"] = raw
    if not out["user"] and payload.get("prompt") is not None:
        out["user"] = content_text(payload.get("prompt"))
    if payload.get("grammar"):
        out["grammar"] = len(str(payload.get("grammar") or ""))
    return out


def extract_delta(obj: dict) -> str:
    choices = obj.get("choices")
    if not isinstance(choices, list) or not choices:
        return ""
    c0 = choices[0] if isinstance(choices[0], dict) else {}
    delta = c0.get("delta") or {}
    if isinstance(delta, dict):
        content = delta.get("content")
        if isinstance(content, str):
            return content
    text = c0.get("text")
    if isinstance(text, str):
        return text
    msg = c0.get("message") or {}
    if isinstance(msg, dict) and isinstance(msg.get("content"), str):
        return msg["content"]
    return ""


def send_http(sock: socket.socket, status: int, headers: Dict[str, str], body: bytes) -> None:
    reason = {200: "OK", 502: "Bad Gateway", 504: "Gateway Timeout"}.get(status, "OK")
    lines = [f"HTTP/1.1 {status} {reason}"]
    headers = dict(headers)
    headers["content-length"] = str(len(body))
    headers["connection"] = "close"
    for k, v in headers.items():
        lines.append(f"{k}: {v}")
    sock.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("iso-8859-1") + body)


def send_raw_response(sock: socket.socket, resp: http.client.HTTPResponse, extra_prefix: bytes = b"") -> None:
    status = resp.status
    reason = resp.reason or "OK"
    hdrs = []
    skip = hop_headers() | {"content-length"}
    length = resp.getheader("Content-Length")
    chunks: List[bytes] = [extra_prefix] if extra_prefix else []
    if length is not None:
        need = int(length) - len(extra_prefix)
        while need > 0:
            chunk = resp.read(min(65536, need))
            if not chunk:
                break
            chunks.append(chunk)
            need -= len(chunk)
        body = b"".join(chunks)
        hdrs.append(f"HTTP/1.1 {status} {reason}")
        for k, v in resp.getheaders():
            if k.lower() in skip:
                continue
            hdrs.append(f"{k}: {v}")
        hdrs.append(f"Content-Length: {len(body)}")
        hdrs.append("Connection: close")
        sock.sendall(("\r\n".join(hdrs) + "\r\n\r\n").encode("iso-8859-1") + body)
        return
    hdrs.append(f"HTTP/1.1 {status} {reason}")
    for k, v in resp.getheaders():
        if k.lower() in skip:
            continue
        hdrs.append(f"{k}: {v}")
    hdrs.append("Connection: close")
    sock.sendall(("\r\n".join(hdrs) + "\r\n\r\n").encode("iso-8859-1"))
    if extra_prefix:
        sock.sendall(extra_prefix)
    while True:
        chunk = resp.read(65536)
        if not chunk:
            break
        sock.sendall(chunk)


def chat_body_from_sse(content: str, model: str) -> bytes:
    payload = {
        "model": model,
        "choices": [
            {
                "index": 0,
                "message": {"role": "assistant", "content": content},
                "finish_reason": "stop",
            }
        ],
    }
    return json.dumps(payload, ensure_ascii=False).encode("utf-8")


def handle_chat(
    sock: Optional[socket.socket],
    conn: http.client.HTTPConnection,
    method: str,
    path: str,
    headers: Dict[str, str],
    body: bytes,
    tag: str,
    src: str = "vm",
    reply: bool = True,
) -> Tuple[str, str, str]:
    global inflight_chat
    client_stream = False
    payload = None
    model = ""
    try:
        payload = json.loads(body.decode("utf-8"))
        if isinstance(payload, dict):
            client_stream = bool(payload.get("stream"))
            model = str(payload.get("model") or "")
            payload = dict(payload)
            payload["stream"] = True
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            headers = dict(headers)
            headers["content-type"] = "application/json"
    except (json.JSONDecodeError, UnicodeDecodeError):
        payload = None

    t0 = time.monotonic()
    log_incoming_prompt(tag, payload if isinstance(payload, dict) else None, method, path, len(body))
    log(f"{tag_label(tag)} {paint(DIM, '── respuesta ──')}")
    rid = next_req_id(tag)
    fields = prompt_fields(payload if isinstance(payload, dict) else None)
    jsonl_emit(
        {
            "kind": "req",
            "id": rid,
            "tag": tag,
            "src": src,
            "method": method,
            "path": path,
            "bytes": len(body),
            "model": model,
            "system": fields["system"][:12000],
            "user": fields["user"][:32000],
            "grammar": fields["grammar"],
        }
    )
    tok_batch = TokBatch(rid, tag)
    stream_open(tag)
    acc: List[str] = []
    raw = b""
    sse = False
    resp: Optional[http.client.HTTPResponse] = None
    err: Optional[str] = None
    with inflight_lock:
        inflight_chat += 1
    try:
        headers["content-length"] = str(len(body))
        conn.request(method, path, body=body, headers=headers)
        resp = conn.getresponse()
        ctype = (resp.getheader("Content-Type") or "").lower()
        if "text/event-stream" not in ctype and not client_stream:
            raw = resp.read()
            try:
                obj = json.loads(raw.decode("utf-8"))
                text = ""
                if isinstance(obj, dict):
                    text = extract_delta(obj)
                    if not text:
                        choices = obj.get("choices") or []
                        if choices and isinstance(choices[0], dict):
                            msg = choices[0].get("message") or {}
                            if isinstance(msg, dict):
                                text = str(msg.get("content") or "")
                if text:
                    acc.append(text)
                    write_tok(text)
                    tok_batch.add(text)
            except (json.JSONDecodeError, UnicodeDecodeError):
                pass
            sse = False
        else:
            sse = True
            leftover = b""
            done = False
            while not done:
                chunk = resp.read(4096)
                if not chunk:
                    break
                leftover += chunk
                while b"\n" in leftover:
                    line, leftover = leftover.split(b"\n", 1)
                    line = line.strip()
                    if not line.startswith(b"data:"):
                        continue
                    data = line[5:].strip()
                    if data == b"[DONE]":
                        done = True
                        break
                    try:
                        obj = json.loads(data.decode("utf-8"))
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        continue
                    if not isinstance(obj, dict):
                        continue
                    tok = extract_delta(obj)
                    if tok:
                        acc.append(tok)
                        write_tok(tok)
                        tok_batch.add(tok)
    except (TimeoutError, socket.timeout, OSError, http.client.HTTPException) as ex:
        err = str(ex)
        log(f"{tag_label(tag)} {paint(RED, f'backend: {ex}')}")
    finally:
        tok_batch.flush()
        stream_close()
        with inflight_lock:
            inflight_chat -= 1

    text = "".join(acc)
    dt = time.monotonic() - t0
    jsonl_emit(
        {
            "kind": "done",
            "id": rid,
            "tag": tag,
            "src": src,
            "seconds": round(dt, 3),
            "chars": len(text),
            "sse": sse,
            "error": err or "",
            "response": text[:200000],
        }
    )
    if err:
        log(f"{tag_label(tag)} {paint(DIM, f'done {dt:.1f}s  error')}")
        if reply and sock is not None:
            send_http(
                sock,
                502,
                {"Content-Type": "text/plain"},
                f"backend: {err}\n".encode(),
            )
        return text, rid, err or ""
    if not sse:
        log(f"{tag_label(tag)} {paint(DIM, f'done {dt:.1f}s  (sin SSE)')}")
        if reply and sock is not None and resp is not None:
            send_http(
                sock,
                resp.status,
                {"Content-Type": resp.getheader("Content-Type") or "application/json"},
                raw,
            )
        return text, rid, ""
    log(f"{tag_label(tag)} {paint(DIM, f'done {dt:.1f}s  {len(text)} chars')}")
    if not reply or sock is None:
        return text, rid, ""
    if client_stream:
        # Rebuild a minimal SSE so a streaming client still works.
        events = []
        if text:
            events.append(
                "data: "
                + json.dumps({"choices": [{"delta": {"content": text}}]}, ensure_ascii=False)
            )
        events.append("data: [DONE]")
        body_out = ("\n\n".join(events) + "\n\n").encode("utf-8")
        send_http(sock, 200, {"Content-Type": "text/event-stream"}, body_out)
        return text, rid, ""
    send_http(
        sock,
        200,
        {"Content-Type": "application/json; charset=utf-8"},
        chat_body_from_sse(text, model),
    )
    return text, rid, ""


def handle_embed(
    sock: socket.socket,
    conn: http.client.HTTPConnection,
    method: str,
    path: str,
    headers: Dict[str, str],
    body: bytes,
    tag: str,
) -> None:
    n = 0
    samples: List[str] = []
    try:
        payload = json.loads(body.decode("utf-8"))
        inp = payload.get("input", payload.get("content"))
        if isinstance(inp, list):
            n = len(inp)
            samples = [content_text(x).replace("\n", " ")[:EMBED_PREVIEW_CHARS] for x in inp[:8]]
        elif inp is not None:
            n = 1
            samples = [content_text(inp).replace("\n", " ")[:EMBED_PREVIEW_CHARS]]
    except (json.JSONDecodeError, UnicodeDecodeError):
        n = 0
    t0 = time.monotonic()
    conn.request(method, path, body=body, headers=headers)
    resp = conn.getresponse()
    raw = resp.read()
    dt = time.monotonic() - t0
    batch = str(n) if n else "?"
    st = paint(RED, str(resp.status)) if resp.status >= 400 else paint(GREEN, str(resp.status))
    log(
        f"{tag_label(tag)} {paint(DIM, 'batch=' + batch)}  HTTP {st}  "
        f"{paint(DIM, f'{dt:.2f}s  {len(raw)} B')}"
    )
    for i, sample in enumerate(samples):
        more = "…" if len(sample) >= EMBED_PREVIEW_CHARS else ""
        log(f"{tag_label(tag)} {paint(DIM, f'  [{i}]')} {paint(YELLOW, sample + more)}")
    jsonl_emit(
        {
            "kind": "embed",
            "id": next_req_id(tag),
            "tag": tag,
            "src": "vm",
            "path": path,
            "batch": n,
            "status": resp.status,
            "seconds": round(dt, 3),
            "bytes": len(raw),
            "samples": samples,
        }
    )
    send_http(
        sock,
        resp.status,
        {"Content-Type": resp.getheader("Content-Type") or "application/json"},
        raw,
    )


DASHBOARD_HTML = r"""<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<title>tuide spy</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
:root {
  --bg:#101114; --panel:#181a20; --line:#2c3038; --txt:#e7e9ee; --muted:#8d939e;
  --chat:#5ec8d8; --embed:#c9a0e8; --ok:#7dcea0; --warn:#e8c547; --err:#e07a7a; --yo:#e8c547;
}
* { box-sizing:border-box; }
html,body { margin:0; height:100%; overflow:hidden; background:var(--bg); color:var(--txt);
  font:13px/1.45 ui-monospace, SFMono-Regular, Menlo, Monaco, monospace; }
#app { display:grid; grid-template-columns:var(--side-w, 360px) 6px minmax(0,1fr);
  height:100%; overflow:hidden; }
#side { display:flex; flex-direction:column;
  min-width:0; min-height:0; height:100%; overflow:hidden; }
#head { padding:12px 14px; border-bottom:1px solid var(--line); flex-shrink:0; }
#head h1 { margin:0 0 8px; font-size:14px; font-weight:600; letter-spacing:.02em; }
#head p { margin:0; color:var(--muted); font-size:11px; }
#tools { display:flex; gap:6px; padding:10px 12px; border-bottom:1px solid var(--line); flex-wrap:wrap; flex-shrink:0; }
#tools input { flex:1; min-width:120px; background:#0c0d10; color:var(--txt);
  border:1px solid var(--line); border-radius:6px; padding:6px 8px; }
button { background:#0c0d10; color:var(--muted); border:1px solid var(--line);
  border-radius:6px; padding:6px 10px; cursor:pointer; font:inherit; }
button.on { color:var(--txt); border-color:#4a5568; background:#22252c; }
button:disabled { opacity:.45; cursor:not-allowed; }
#send { color:var(--ok); border-color:#355a45; }
#list { overflow-x:hidden; overflow-y:auto; flex:1; min-height:0; }
.item { padding:10px 12px; border-bottom:1px solid var(--line); cursor:pointer; }
.item:hover { background:#1f222a; }
.item.sel { background:#252a33; }
.item .meta { color:var(--muted); font-size:11px; display:flex; gap:8px; margin-bottom:4px; }
.tag { font-weight:600; }
.tag.vm { color:var(--chat); }
.tag.yo { color:var(--yo); }
.tag.embed { color:var(--embed); }
.item .sum { white-space:nowrap; overflow:hidden; text-overflow:ellipsis; color:#c5c9d1; }
#main { display:flex; flex-direction:column; min-width:0; min-height:0; height:100%; overflow:hidden; }
#dhead { padding:8px 12px 8px 16px; border-bottom:1px solid var(--line); color:var(--muted);
  font-size:12px; flex-shrink:0; display:flex; align-items:center; gap:10px; flex-wrap:wrap; }
#dhead-meta { flex:1; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.js-follow { flex-shrink:0; font-weight:600; padding:7px 12px; }
.js-follow.on { color:#101114; background:var(--warn); border-color:var(--warn); }
#dbody { display:grid; grid-template-rows:minmax(72px, var(--prompt-h, 50%)) 6px minmax(72px, 1fr);
  min-height:0; flex:1; overflow:hidden; }
.pane { min-height:0; overflow:hidden; display:flex; flex-direction:column; }
.pane h2 { margin:0; padding:10px 16px 6px; font-size:11px; color:var(--muted);
  text-transform:uppercase; letter-spacing:.08em; flex-shrink:0; }
.pane-body { flex:1; min-height:0; overflow-x:hidden; overflow-y:auto; padding:0 16px 14px; }
pre { margin:0; white-space:pre-wrap; word-break:break-word; }
#pane-prompt pre { color:var(--txt); }
#pane-resp pre { color:var(--ok); }
#pane-prompt pre.muted, #pane-resp pre.muted { color:var(--muted); }
.live { color:var(--warn); }
#compose { display:none; flex-direction:column; padding:10px 12px 12px;
  flex-shrink:0; height:var(--compose-h, 180px); min-height:110px; overflow:auto; }
#compose textarea { width:100%; background:#0c0d10; color:var(--txt); border:1px solid var(--line);
  border-radius:6px; padding:8px; font:inherit; resize:vertical; }
#sys { min-height:36px; max-height:80px; margin-bottom:6px; color:#c5c9d1; }
#user { min-height:64px; max-height:140px; }
#compose-row { display:flex; gap:8px; align-items:center; margin-top:8px; }
#hint { color:var(--warn); font-size:11px; flex:1; min-height:1.2em; }
.split { background:var(--line); flex-shrink:0; }
.split:hover, .split.drag { background:#6a7382; }
.split-v { cursor:col-resize; }
.split-h { cursor:row-resize; }
body.resizing { user-select:none; cursor:col-resize; }
body.resizing-h { user-select:none; cursor:row-resize; }
#split-compose { display:none; height:6px; }
</style>
</head>
<body>
<div id="app">
  <div id="side">
    <div id="head">
      <h1>tuide spy</h1>
      <p>Historial local · VM y este Mac</p>
    </div>
    <div id="tools">
      <input id="q" placeholder="buscar en prompt / respuesta" autocomplete="off">
      <button id="f-all" class="on">todos</button>
      <button id="f-vm">vm</button>
      <button id="f-yo">yo</button>
      <button id="f-embed">embed</button>
    </div>
    <div id="list"></div>
    <div id="split-compose" class="split split-h" title="Arrastra para redimensionar"></div>
    <div id="compose">
      <textarea id="sys" placeholder="system (opcional)"></textarea>
      <textarea id="user" placeholder="Escribe un prompt…  Ctrl+Enter envía"></textarea>
      <div id="compose-row">
        <span id="hint"></span>
        <button id="send" type="button">enviar</button>
      </div>
    </div>
  </div>
  <div id="split-side" class="split split-v" title="Arrastra para redimensionar"></div>
  <div id="main">
    <div id="dhead">
      <div id="dhead-meta">ningún turno seleccionado</div>
      <button id="f-follow" class="js-follow on" type="button" title="Mantener el foco en el último turno">Anclado al último</button>
    </div>
    <div id="dbody">
      <div class="pane" id="pane-prompt">
        <h2>prompt</h2>
        <div class="pane-body"><pre id="prompt" class="muted">Selecciona un turno o escribe a la izquierda.</pre></div>
      </div>
      <div id="split-io" class="split split-h" title="Arrastra para redimensionar"></div>
      <div class="pane" id="pane-resp">
        <h2>salida</h2>
        <div class="pane-body"><pre id="response" class="muted">—</pre></div>
      </div>
    </div>
  </div>
</div>
<script>
const reqs = new Map();
let order = [];
let filter = "all";
let query = "";
let sel = null;
let off = 0;
let sending = false;
let askEnabled = false;
let serverBusy = false;
let follow = true;

function esc(s) {
  return String(s || "").replace(/[&<>]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;"}[c]));
}
function srcOf(r) {
  if (r.src) return r.src;
  if (r.tag === "direct") return "direct";
  return "vm";
}
function badge(r) {
  if (r.tag === "embed") return "embed";
  return srcOf(r) === "direct" ? "yo" : "vm";
}
function apply(ev) {
  if (!ev || !ev.kind) return;
  if (ev.kind === "req") {
    reqs.set(ev.id, {
      id: ev.id, tag: ev.tag || "chat", src: ev.src || (ev.tag === "direct" ? "direct" : "vm"),
      ts: ev.ts, user: ev.user || "", system: ev.system || "", response: "",
      streaming: true, seconds: null, model: ev.model || "", kind: "chat"
    });
    order.push(ev.id);
  } else if (ev.kind === "tok") {
    const r = reqs.get(ev.id);
    if (r) r.response += ev.text || "";
  } else if (ev.kind === "done") {
    const r = reqs.get(ev.id);
    if (r) {
      if (ev.response) r.response = ev.response;
      r.streaming = false;
      r.seconds = ev.seconds;
      r.chars = ev.chars;
      r.error = ev.error || "";
    }
  } else if (ev.kind === "embed") {
    reqs.set(ev.id, {
      id: ev.id, tag: "embed", src: "vm", ts: ev.ts, kind: "embed",
      user: (ev.samples || []).join("\n"), system: "",
      response: "HTTP " + ev.status + "  batch=" + ev.batch + "  " + (ev.seconds || 0) + "s",
      streaming: false, seconds: ev.seconds, status: ev.status
    });
    order.push(ev.id);
  }
}
function match(r) {
  const b = badge(r);
  if (filter === "vm" && b !== "vm") return false;
  if (filter === "yo" && b !== "yo") return false;
  if (filter === "embed" && b !== "embed") return false;
  if (!query) return true;
  const q = query.toLowerCase();
  return (r.user + "\n" + r.system + "\n" + r.response).toLowerCase().includes(q);
}
function latestMatchId() {
  for (let i = order.length - 1; i >= 0; i--) {
    const r = reqs.get(order[i]);
    if (r && match(r)) return r.id;
  }
  return null;
}
function syncFollowBtn() {
  const btn = document.getElementById("f-follow");
  if (!btn) return;
  btn.classList.toggle("on", follow);
  btn.textContent = follow ? "Anclado al último" : "Anclar al último";
}
function setFollow(on) {
  follow = !!on;
  try { localStorage.setItem("tuide-spy-follow", follow ? "1" : "0"); } catch (e) {}
  if (follow) {
    const id = latestMatchId();
    if (id) sel = id;
  }
  syncFollowBtn();
}
function anyChatLive() {
  for (const r of reqs.values()) {
    if (r.streaming && r.tag !== "embed") return true;
  }
  return false;
}
function syncComposer() {
  const busy = sending || serverBusy || anyChatLive();
  const user = document.getElementById("user");
  const btn = document.getElementById("send");
  const hint = document.getElementById("hint");
  if (!user || !btn || !hint) return;
  btn.disabled = !askEnabled || busy || !user.value.trim();
  hint.textContent = (!askEnabled || !busy) ? "" : "el modelo está ocupado (VM o tú)";
}
function renderList() {
  if (follow) {
    const id = latestMatchId();
    if (id) sel = id;
  }
  const list = document.getElementById("list");
  const y = list.scrollTop;
  const html = [];
  for (let i = order.length - 1; i >= 0; i--) {
    const r = reqs.get(order[i]);
    if (!r || !match(r)) continue;
    const t = r.ts ? new Date(r.ts * 1000).toLocaleTimeString() : "";
    const b = badge(r);
    const sum = (r.user || r.response || "(sin texto)").replace(/\s+/g, " ").slice(0, 140);
    html.push(`<div class="item${sel===r.id?" sel":""}" data-id="${esc(r.id)}">
      <div class="meta"><span class="tag ${esc(b)}">${esc(b)}</span><span>${esc(t)}</span>
      ${r.streaming ? '<span class="live">live</span>' : (r.seconds!=null ? "<span>"+r.seconds+"s</span>" : "")}</div>
      <div class="sum">${esc(sum)}</div></div>`);
  }
  list.innerHTML = html.join("") || '<div class="item"><span class="meta">sin coincidencias</span></div>';
  list.querySelectorAll(".item[data-id]").forEach(el => {
    el.onclick = () => {
      if (follow) setFollow(false);
      sel = el.dataset.id;
      render();
    };
  });
  list.scrollTop = follow ? 0 : y;
}
function renderDetail() {
  const r = sel ? reqs.get(sel) : null;
  const promptEl = document.getElementById("prompt");
  const respEl = document.getElementById("response");
  const paneResp = document.getElementById("pane-resp").querySelector(".pane-body");
  const nearBottom = paneResp.scrollHeight - paneResp.scrollTop - paneResp.clientHeight < 48;
  if (!r) {
    document.getElementById("dhead-meta").textContent = "ningún turno seleccionado";
    promptEl.className = "muted";
    respEl.className = "muted";
    promptEl.textContent = "Selecciona un turno o escribe a la izquierda.";
    respEl.textContent = "—";
    return;
  }
  const t = r.ts ? new Date(r.ts * 1000).toLocaleString() : "";
  const extra = r.error ? " · error" : (r.streaming ? " · generando…" : "");
  document.getElementById("dhead-meta").textContent =
    badge(r) + " · " + t + (r.model ? " · " + r.model : "") + extra;
  const prompt = (r.system ? "—— system ——\n" + r.system + "\n\n" : "") +
                 (r.user ? "—— user ——\n" + r.user : "(sin user)");
  promptEl.className = "";
  promptEl.textContent = prompt;
  respEl.className = r.error && !r.response ? "muted" : "";
  respEl.textContent = r.response || (r.streaming ? "…" : (r.error || "(vacío)"));
  if (follow || (r.streaming && nearBottom)) paneResp.scrollTop = paneResp.scrollHeight;
}
function render() { renderList(); renderDetail(); syncComposer(); }

async function poll() {
  try {
    const res = await fetch("/api/tail?off=" + off);
    const data = await res.json();
    off = data.off;
    (data.lines || []).forEach(line => {
      try { apply(JSON.parse(line)); } catch (e) {}
    });
    if ((data.lines || []).length) render();
    else syncComposer();
  } catch (e) {}
}
async function refreshStatus() {
  try {
    const res = await fetch("/api/status");
    const st = await res.json();
    askEnabled = !!st.ask;
    serverBusy = !!st.busy;
    document.getElementById("compose").style.display = askEnabled ? "flex" : "none";
    document.getElementById("split-compose").style.display = askEnabled ? "block" : "none";
    syncComposer();
  } catch (e) {}
}
async function sendAsk() {
  const userEl = document.getElementById("user");
  const user = userEl.value.trim();
  const system = document.getElementById("sys").value;
  if (!user || sending || !askEnabled) return;
  sending = true;
  syncComposer();
  try {
    const res = await fetch("/api/ask", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({user, system}),
    });
    const data = await res.json();
    if (data.id) sel = data.id;
    if (!data.ok) {
      document.getElementById("hint").textContent = data.error || "error";
    } else {
      userEl.value = "";
    }
  } catch (e) {
    document.getElementById("hint").textContent = "no se pudo enviar";
  }
  sending = false;
  await poll();
  syncComposer();
}
document.getElementById("q").oninput = (e) => { query = e.target.value.trim(); renderList(); };
document.getElementById("user").oninput = syncComposer;
document.getElementById("send").onclick = sendAsk;
document.getElementById("user").addEventListener("keydown", (e) => {
  if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
    e.preventDefault();
    sendAsk();
  }
});
["all","vm","yo","embed"].forEach(name => {
  document.getElementById("f-" + name).onclick = () => {
    filter = name;
    ["all","vm","yo","embed"].forEach(n => document.getElementById("f-"+n).classList.toggle("on", n===name));
    render();
  };
});
document.getElementById("f-follow").onclick = () => {
  setFollow(!follow);
  render();
};
try {
  const savedFollow = localStorage.getItem("tuide-spy-follow");
  if (savedFollow === "0") follow = false;
} catch (e) {}
syncFollowBtn();
refreshStatus();
poll();
setInterval(poll, 250);
setInterval(refreshStatus, 1000);

(function layoutSplits() {
  const app = document.getElementById("app");
  const dbody = document.getElementById("dbody");
  const compose = document.getElementById("compose");
  const KEY = "tuide-spy-layout";
  try {
    const saved = JSON.parse(localStorage.getItem(KEY) || "null");
    if (saved) {
      if (saved.side) app.style.setProperty("--side-w", saved.side);
      if (saved.prompt) dbody.style.setProperty("--prompt-h", saved.prompt);
      if (saved.compose) compose.style.setProperty("--compose-h", saved.compose);
    }
  } catch (e) {}
  function save() {
    try {
      localStorage.setItem(KEY, JSON.stringify({
        side: getComputedStyle(app).getPropertyValue("--side-w").trim() || "360px",
        prompt: getComputedStyle(dbody).getPropertyValue("--prompt-h").trim() || "50%",
        compose: getComputedStyle(compose).getPropertyValue("--compose-h").trim() || "180px",
      }));
    } catch (e) {}
  }
  function drag(el, horiz, onMove) {
    el.addEventListener("mousedown", (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      el.classList.add("drag");
      document.body.classList.add(horiz ? "resizing-h" : "resizing");
      const move = (ev) => onMove(ev);
      const up = () => {
        el.classList.remove("drag");
        document.body.classList.remove("resizing", "resizing-h");
        window.removeEventListener("mousemove", move);
        window.removeEventListener("mouseup", up);
        save();
      };
      window.addEventListener("mousemove", move);
      window.addEventListener("mouseup", up);
    });
  }
  drag(document.getElementById("split-side"), false, (ev) => {
    const max = Math.floor(window.innerWidth * 0.72);
    const w = Math.max(220, Math.min(max, ev.clientX));
    app.style.setProperty("--side-w", w + "px");
  });
  drag(document.getElementById("split-io"), true, (ev) => {
    const box = dbody.getBoundingClientRect();
    if (box.height < 8) return;
    const pct = Math.max(18, Math.min(82, ((ev.clientY - box.top) / box.height) * 100));
    dbody.style.setProperty("--prompt-h", pct + "%");
  });
  drag(document.getElementById("split-compose"), true, (ev) => {
    const box = document.getElementById("side").getBoundingClientRect();
    const h = Math.max(110, Math.min(box.height * 0.7, box.bottom - ev.clientY));
    compose.style.setProperty("--compose-h", Math.round(h) + "px");
  });
})();
</script>
</body>
</html>
"""


ASK_USER_MAX = 32000
ASK_SYSTEM_MAX = 12000
ASK_BODY_MAX = 200000


def run_direct_ask(user: str, system: str, backend_host: str, backend_port: int) -> Tuple[str, str, str]:
    """Returns (text, req_id, error)."""
    messages = []
    if system.strip():
        messages.append({"role": "system", "content": system[:ASK_SYSTEM_MAX]})
    messages.append({"role": "user", "content": user[:ASK_USER_MAX]})
    payload = {"messages": messages, "stream": False}
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    headers = {
        "content-type": "application/json",
        "host": f"{backend_host}:{backend_port}",
        "connection": "close",
        "content-length": str(len(body)),
    }
    conn = http.client.HTTPConnection(backend_host, backend_port, timeout=600)
    try:
        text, rid, err = handle_chat(
            None,
            conn,
            "POST",
            "/v1/chat/completions",
            headers,
            body,
            "direct",
            src="direct",
            reply=False,
        )
        return text, rid, err
    finally:
        conn.close()


class _WebHandler(http.server.BaseHTTPRequestHandler):
    jsonl = ""
    backend_host = "127.0.0.1"
    backend_port = 18080
    ask_enabled = False

    def log_message(self, fmt: str, *args) -> None:
        return

    def _send(self, code: int, body: bytes, ctype: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code: int, obj: dict) -> None:
        self._send(
            code,
            json.dumps(obj, ensure_ascii=False).encode("utf-8"),
            "application/json; charset=utf-8",
        )

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path in ("/", "/index.html"):
            self._send(200, DASHBOARD_HTML.encode("utf-8"), "text/html; charset=utf-8")
            return
        if parsed.path == "/api/status":
            self._json(200, {"ask": self.ask_enabled, "busy": chat_busy()})
            return
        if parsed.path == "/api/tail":
            qs = urllib.parse.parse_qs(parsed.query)
            try:
                off = max(0, int(qs.get("off", ["0"])[0]))
            except ValueError:
                off = 0
            path = self.jsonl
            if not path or not os.path.isfile(path):
                self._json(200, {"off": 0, "lines": []})
                return
            size = os.path.getsize(path)
            if off > size:
                off = 0
            with open(path, "rb") as f:
                f.seek(off)
                raw = f.read(2 * 1024 * 1024)
            if raw and not raw.endswith(b"\n"):
                last_nl = raw.rfind(b"\n")
                if last_nl >= 0:
                    raw = raw[: last_nl + 1]
                else:
                    raw = b""
            new_off = off + len(raw)
            lines = [ln for ln in raw.decode("utf-8", errors="replace").split("\n") if ln]
            self._json(200, {"off": new_off, "lines": lines})
            return
        self._send(404, b"not found\n", "text/plain")

    def do_POST(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path != "/api/ask":
            self._send(404, b"not found\n", "text/plain")
            return
        if not self.ask_enabled:
            self._json(503, {"ok": False, "error": "no hay LLM en este visor (solo embeddings)"})
            return
        try:
            length = int(self.headers.get("Content-Length") or "0")
        except ValueError:
            length = 0
        if length <= 0 or length > ASK_BODY_MAX:
            self._json(413, {"ok": False, "error": "cuerpo inválido o demasiado grande"})
            return
        raw = self.rfile.read(length)
        try:
            obj = json.loads(raw.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            self._json(400, {"ok": False, "error": "JSON inválido"})
            return
        if not isinstance(obj, dict):
            self._json(400, {"ok": False, "error": "JSON inválido"})
            return
        user = str(obj.get("user") or "").strip()
        system = str(obj.get("system") or "")
        if not user:
            self._json(400, {"ok": False, "error": "user vacío"})
            return
        try:
            text, rid, err = run_direct_ask(user, system, self.backend_host, self.backend_port)
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


def start_web(
    listen: str,
    jsonl: str,
    backend_host: str,
    backend_port: int,
    ask_enabled: bool,
) -> None:
    host, port_s = listen.rsplit(":", 1)

    class _WebServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
        allow_reuse_address = True
        daemon_threads = True

    _WebHandler.jsonl = jsonl
    _WebHandler.backend_host = backend_host
    _WebHandler.backend_port = backend_port
    _WebHandler.ask_enabled = ask_enabled
    httpd = _WebServer((host, int(port_s)), _WebHandler)
    t = threading.Thread(target=httpd.serve_forever, name="spy-web", daemon=True)
    t.start()
    extra = "  ask=on" if ask_enabled else "  ask=off"
    log(paint(DIM, f"web http://{host}:{port_s}  jsonl={jsonl}{extra}"))


class Handler(socketserver.StreamRequestHandler):
    backend_host = "127.0.0.1"
    backend_port = 18080
    tag = "chat"

    def handle(self) -> None:
        parsed = parse_request(self.connection)
        if parsed is None:
            return
        method, path, headers, body = parsed
        kind = path_kind(path)
        fwd = fwd_headers(headers, f"{self.backend_host}:{self.backend_port}")
        if body:
            fwd["content-length"] = str(len(body))
        conn = http.client.HTTPConnection(self.backend_host, self.backend_port, timeout=600)
        try:
            if kind == "chat" and method.upper() == "POST":
                handle_chat(self.connection, conn, method, path, fwd, body, self.tag, src="vm")
            elif kind == "embed" and method.upper() == "POST":
                handle_embed(self.connection, conn, method, path, fwd, body, self.tag)
            else:
                conn.request(method, path, body=body if body else None, headers=fwd)
                resp = conn.getresponse()
                send_raw_response(self.connection, resp)
        except (TimeoutError, socket.timeout) as ex:
            log(f"{tag_label(self.tag)} {paint(RED, f'timeout: {ex}')}")
            send_http(self.connection, 504, {"Content-Type": "text/plain"}, b"backend timeout\n")
        except OSError as ex:
            log(f"{tag_label(self.tag)} {paint(RED, f'backend: {ex}')}")
            send_http(
                self.connection,
                502,
                {"Content-Type": "text/plain"},
                f"backend: {ex}\n".encode(),
            )
        finally:
            conn.close()


class ThreadingTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main() -> int:
    ap = argparse.ArgumentParser(description="Host llama-server spy proxy")
    ap.add_argument("--listen", required=True, help="host:port public (0.0.0.0:8080)")
    ap.add_argument("--backend", required=True, help="host:port llama-server (127.0.0.1:18080)")
    ap.add_argument("--tag", default="chat", help="log prefix (chat|embed)")
    ap.add_argument("--jsonl", default="", help="historial JSONL (chat y embed pueden compartir archivo)")
    ap.add_argument(
        "--web",
        default="",
        metavar="HOST:PORT",
        help="dashboard HTML en loopback (p.ej. 127.0.0.1:18767); requiere --jsonl",
    )
    color = ap.add_mutually_exclusive_group()
    color.add_argument("--color", action="store_true", help="fuerza ANSI aunque stdout no sea TTY")
    color.add_argument("--no-color", action="store_true", help="sin ANSI (o env NO_COLOR)")
    args = ap.parse_args()
    if args.no_color:
        configure_color(False)
    elif args.color:
        configure_color(True)

    global jsonl_path
    jsonl_path = args.jsonl or ""
    lh, lp = args.listen.rsplit(":", 1)
    bh, bp = args.backend.rsplit(":", 1)
    Handler.backend_host = bh
    Handler.backend_port = int(bp)
    Handler.tag = args.tag
    if args.web:
        if not jsonl_path:
            log("error: --web requiere --jsonl")
            return 2
        start_web(
            args.web,
            jsonl_path,
            bh,
            int(bp),
            ask_enabled=(args.tag == "chat"),
        )

    server = ThreadingTCPServer((lh, int(lp)), Handler)
    log(f"{tag_label(args.tag)} {paint(DIM, f'spy {lh}:{lp} → {bh}:{bp}')}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
