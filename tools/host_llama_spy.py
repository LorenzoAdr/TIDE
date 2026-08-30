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
from typing import Any, Dict, List, Optional, Tuple

import host_llama_chat_session as chat_mem

print_lock = threading.Lock()
_force_color: Optional[bool] = None
jsonl_path = ""
jsonl_lock = threading.Lock()
req_seq = 0
req_seq_lock = threading.Lock()
inflight_lock = threading.Lock()
inflight_chat = 0
active_backend_conns: List[http.client.HTTPConnection] = []
cancel_generation = threading.Event()
chat_turn_lock = threading.Lock()
chat_session_lock = threading.Lock()
chat_session = chat_mem.ChatSession()
_last_chat_ctx_lock = threading.Lock()
_last_chat_ctx: Dict[str, int] = {
    "n_tokens": 0,
    "prompt": 0,
    "predicted": 0,
    "cached": 0,
}

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


def try_slot_cancel(host: str, port: int) -> None:
    if not host or port <= 0:
        return
    try:
        conn = http.client.HTTPConnection(host, int(port), timeout=2)
        try:
            conn.request(
                "POST",
                "/slots/0?action=cancel",
                body=b"",
                headers={"host": f"{host}:{port}", "content-length": "0"},
            )
            conn.getresponse().read()
        finally:
            conn.close()
    except Exception:
        pass


def request_stop_generation(backend_host: str = "", backend_port: int = 0) -> dict:
    """Stop the in-flight llama-server completion (compose or VM)."""
    cancel_generation.set()
    with inflight_lock:
        conns = list(active_backend_conns)
    for conn in conns:
        try:
            conn.close()
        except Exception:
            pass
    if backend_host and backend_port:
        try_slot_cancel(backend_host, int(backend_port))
    return {"ok": True, "busy": chat_busy()}


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


def clear_history() -> dict:
    """Truncate the spy JSONL. Does not reset chat-mode memory."""
    if not jsonl_path:
        return {"ok": True, "cleared": False}
    os.makedirs(os.path.dirname(jsonl_path) or ".", exist_ok=True)
    with jsonl_lock:
        with open(jsonl_path, "w", encoding="utf-8") as f:
            fcntl.flock(f.fileno(), fcntl.LOCK_EX)
            try:
                f.write("")
                f.flush()
            finally:
                fcntl.flock(f.fileno(), fcntl.LOCK_UN)
    return {"ok": True, "cleared": True}


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


def format_messages_prompt(payload: Optional[dict]) -> str:
    if not isinstance(payload, dict):
        return ""
    msgs = payload.get("messages")
    if not isinstance(msgs, list) or not msgs:
        return ""
    parts: List[str] = []
    for msg in msgs:
        if not isinstance(msg, dict):
            continue
        role = str(msg.get("role") or "message")
        raw = content_text(msg.get("content"))
        parts.append(f"—— {role} ——\n{raw}" if raw else f"—— {role} ——\n(vacío)")
    return "\n\n".join(parts)


def _positive_int(value: Any) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return 0
    n = int(value)
    return n if n > 0 else 0


def completion_ctx(obj: Any) -> Dict[str, int]:
    """Prompt+completion occupancy from a llama-server usage/timings blob."""
    if not isinstance(obj, dict):
        return {}
    usage = obj.get("usage")
    if isinstance(usage, dict):
        prompt = _positive_int(usage.get("prompt_tokens"))
        predicted = _positive_int(usage.get("completion_tokens"))
        details = usage.get("prompt_tokens_details")
        cached = 0
        if isinstance(details, dict):
            cached = _positive_int(details.get("cached_tokens"))
        total = _positive_int(usage.get("total_tokens"))
        if total <= 0:
            total = prompt + predicted
        if total > 0:
            return {
                "n_tokens": total,
                "prompt": prompt,
                "predicted": predicted,
                "cached": cached,
            }
    timings = obj.get("timings")
    if isinstance(timings, dict):
        cached = _positive_int(timings.get("cache_n"))
        prompt_new = _positive_int(timings.get("prompt_n"))
        predicted = _positive_int(timings.get("predicted_n"))
        total = cached + prompt_new + predicted
        if total > 0:
            return {
                "n_tokens": total,
                "prompt": cached + prompt_new,
                "predicted": predicted,
                "cached": cached,
            }
    return {}


def last_chat_ctx() -> Dict[str, int]:
    with _last_chat_ctx_lock:
        return dict(_last_chat_ctx)


def record_chat_ctx(ctx: Dict[str, int]) -> None:
    n = _positive_int(ctx.get("n_tokens"))
    if n <= 0:
        return
    with _last_chat_ctx_lock:
        _last_chat_ctx["n_tokens"] = n
        _last_chat_ctx["prompt"] = _positive_int(ctx.get("prompt"))
        _last_chat_ctx["predicted"] = _positive_int(ctx.get("predicted"))
        _last_chat_ctx["cached"] = _positive_int(ctx.get("cached"))


def clear_last_chat_ctx() -> None:
    with _last_chat_ctx_lock:
        _last_chat_ctx["n_tokens"] = 0
        _last_chat_ctx["prompt"] = 0
        _last_chat_ctx["predicted"] = 0
        _last_chat_ctx["cached"] = 0


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
    log_kind: str = "",
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
    req_ev = {
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
        "prompt": format_messages_prompt(payload if isinstance(payload, dict) else None)[:32000],
        "grammar": fields["grammar"],
    }
    if log_kind:
        req_ev["log_kind"] = log_kind
    jsonl_emit(req_ev)
    tok_batch = TokBatch(rid, tag)
    stream_open(tag)
    acc: List[str] = []
    raw = b""
    sse = False
    resp: Optional[http.client.HTTPResponse] = None
    err: Optional[str] = None
    ctx_snap: Dict[str, int] = {}
    with inflight_lock:
        inflight_chat += 1
        active_backend_conns.append(conn)
    try:
        if cancel_generation.is_set():
            err = "stopped"
            raise OSError("stopped")
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
                    snap = completion_ctx(obj)
                    if snap:
                        ctx_snap = snap
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
                if cancel_generation.is_set():
                    err = "stopped"
                    break
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
                    snap = completion_ctx(obj)
                    if snap:
                        ctx_snap = snap
                    tok = extract_delta(obj)
                    if tok:
                        acc.append(tok)
                        write_tok(tok)
                        tok_batch.add(tok)
    except (TimeoutError, socket.timeout, OSError, http.client.HTTPException) as ex:
        if cancel_generation.is_set() or str(ex) == "stopped":
            err = "stopped"
        else:
            err = str(ex)
            log(f"{tag_label(tag)} {paint(RED, f'backend: {ex}')}")
    finally:
        tok_batch.flush()
        stream_close()
        with inflight_lock:
            inflight_chat -= 1
            try:
                active_backend_conns.remove(conn)
            except ValueError:
                pass
            if inflight_chat <= 0:
                cancel_generation.clear()

    text = "".join(acc)
    dt = time.monotonic() - t0
    if ctx_snap and log_kind != "summary":
        record_chat_ctx(ctx_snap)
    done_ev = {
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
    if log_kind:
        done_ev["log_kind"] = log_kind
    jsonl_emit(done_ev)
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
#head h1 { margin:0 0 8px; font-size:14px; font-weight:600; letter-spacing:.02em;
  display:flex; align-items:center; gap:10px; flex-wrap:wrap; }
#head p { margin:0; color:var(--muted); font-size:11px; }
#llm-lamp { display:inline-flex; align-items:center; gap:7px; font-size:11px; font-weight:500;
  letter-spacing:0; color:var(--muted); margin-left:auto; }
#llm-lamp i { width:10px; height:10px; border-radius:50%; background:#2a2d33;
  box-shadow:inset 0 0 0 1px #3a3e46; flex-shrink:0; }
#llm-lamp.on { color:var(--ok); }
#llm-lamp.on i { background:var(--ok); box-shadow:0 0 0 1px #355a45, 0 0 8px var(--ok); }
#tools { display:flex; gap:6px; padding:10px 12px; border-bottom:1px solid var(--line); flex-wrap:wrap; flex-shrink:0; }
#tools input { flex:1; min-width:120px; background:#0c0d10; color:var(--txt);
  border:1px solid var(--line); border-radius:6px; padding:6px 8px; }
button { background:#0c0d10; color:var(--muted); border:1px solid var(--line);
  border-radius:6px; padding:6px 10px; cursor:pointer; font:inherit; }
button.on { color:var(--txt); border-color:#4a5568; background:#22252c; }
button:disabled { opacity:.45; cursor:not-allowed; }
#send { color:var(--ok); border-color:#355a45; }
#stop { color:var(--err); border-color:#5a3535; display:none; }
#f-clear { color:var(--err); border-color:#5a3535; }
#list { overflow-x:hidden; overflow-y:auto; flex:1; min-height:0; }
.item { padding:10px 12px; border-bottom:1px solid var(--line); cursor:pointer; }
.item:hover { background:#1f222a; }
.item.sel { background:#252a33; }
.item .meta { color:var(--muted); font-size:11px; display:flex; gap:8px; margin-bottom:4px; }
.tag { font-weight:600; }
.tag.vm { color:var(--chat); }
.tag.yo { color:var(--yo); }
.tag.embed { color:var(--embed); }
.tag.memoria { color:var(--muted); }
.item .sum { white-space:nowrap; overflow:hidden; text-overflow:ellipsis; color:#c5c9d1; }
#main { display:flex; flex-direction:column; min-width:0; min-height:0; height:100%; overflow:hidden; }
#dhead { padding:8px 12px 8px 16px; border-bottom:1px solid var(--line); color:var(--muted);
  font-size:12px; flex-shrink:0; display:flex; align-items:center; gap:10px; flex-wrap:wrap; }
#dhead-meta { flex:1; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.js-follow { flex-shrink:0; font-weight:600; padding:7px 12px; }
.js-follow.on { color:#101114; background:var(--warn); border-color:var(--warn); }
#dbody { display:grid; grid-template-rows:minmax(72px, var(--prompt-h, 50%)) 6px minmax(72px, 1fr);
  min-height:0; flex:1; overflow:hidden; }
#dbody.hide-prompt { grid-template-rows:minmax(0, 1fr); }
#dbody.hide-prompt #pane-prompt,
#dbody.hide-prompt #split-io { display:none; }
.pane { min-height:0; overflow:hidden; display:flex; flex-direction:column; }
.pane > h2 { margin:0; padding:10px 16px 6px; font-size:11px; color:var(--muted);
  text-transform:uppercase; letter-spacing:.08em; flex-shrink:0;
  display:flex; align-items:center; gap:8px; }
.pane > h2 button { text-transform:none; letter-spacing:0; font-size:11px; padding:3px 8px; }
.pane-body { flex:1; min-height:0; overflow-x:hidden; overflow-y:auto; padding:0 16px 14px; }
pre { margin:0; white-space:pre-wrap; word-break:break-word; }
#pane-prompt pre { color:var(--txt); }
#pane-prompt .md { color:var(--txt); }
#pane-prompt .md td { color:var(--txt); }
#pane-resp pre, #pane-resp .md { color:var(--ok); }
#pane-prompt pre.muted, #pane-resp pre.muted, #pane-resp .md.muted { color:var(--muted); }
.md { white-space:normal; word-break:break-word; }
.md p { margin:0.45em 0; }
.md p:first-child { margin-top:0; }
.md strong { color:var(--txt); font-weight:700; }
.md em { font-style:italic; }
.md h1,.md h2,.md h3,.md h4 { color:var(--txt); text-transform:none; letter-spacing:0;
  font-size:13px; margin:0.85em 0 0.35em; }
.md h1 { font-size:16px; }
.md h2 { font-size:14px; }
.md ul,.md ol { margin:0.4em 0; padding-left:1.4em; }
.md li { margin:0.15em 0; }
.md code { background:#0c0d10; border:1px solid var(--line); border-radius:4px;
  padding:0 5px; color:var(--warn); font:inherit; }
.md pre.md-code { background:#0c0d10; border:1px solid var(--line); border-radius:6px;
  padding:10px 12px; overflow-x:auto; color:#c5c9d1; margin:0.5em 0; }
#pane-resp pre.md-code { color:#c5c9d1; }
.md pre.md-code code { background:none; border:0; padding:0; color:inherit; }
.md-fence { margin:0.5em 0; background:#0c0d10; border:1px solid var(--line); border-radius:6px;
  overflow:hidden; }
.md-fence-bar { display:flex; align-items:center; justify-content:space-between; gap:8px;
  padding:4px 6px 0 12px; min-height:26px; }
.md-fence-lang { color:var(--muted); font-size:10px; text-transform:uppercase;
  letter-spacing:.08em; }
.md-copy { font-size:10px; padding:2px 8px; line-height:1.3; flex-shrink:0; }
.md-copy.on { color:var(--ok); border-color:#355a45; }
.md-fence pre.md-code { margin:0; border:0; background:none; padding:6px 12px 10px; }
.md-tok-kw { color:#c9a0e8; }
.md-tok-str { color:#7dcea0; }
.md-tok-cmt { color:#8d939e; font-style:italic; }
.md-tok-num { color:#e8b86d; }
.md-tok-type { color:#5ec8d8; }
.md-tok-fn { color:#6cb6ff; }
.md-tok-pp { color:#c9a0e8; }
.md-tok-key { color:#5ec8d8; }
.md blockquote { margin:0.5em 0; padding:0 10px; border-left:3px solid var(--line); color:var(--muted); }
.md a { color:var(--chat); }
.md hr { border:0; border-top:1px solid var(--line); margin:0.8em 0; }
.md table { border-collapse:collapse; margin:0.6em 0; width:100%; font-size:12px; }
.md th, .md td { border:1px solid var(--line); padding:6px 8px; text-align:left; vertical-align:top; }
.md th { background:#14161c; color:var(--txt); font-weight:600; }
.md td { color:var(--ok); }
pre.md-mermaid::before { content:"mermaid"; display:block; color:var(--muted);
  font-size:10px; text-transform:uppercase; letter-spacing:.08em; margin-bottom:6px; }
.md-mermaid-fail { color:var(--warn); font-size:11px; margin:0.4em 0 0.2em; }
.md-mermaid-out { margin:0.6em 0; overflow-x:auto; background:#0c0d10;
  border:1px solid var(--line); border-radius:8px; padding:12px; }
.md-mermaid-out svg { max-width:100%; height:auto; }
.live { color:var(--warn); }
#compose { display:none; flex-direction:column; padding:10px 12px 12px;
  flex-shrink:0; height:var(--compose-h, 180px); min-height:110px; overflow:auto; }
#compose textarea { width:100%; background:#0c0d10; color:var(--txt); border:1px solid var(--line);
  border-radius:6px; padding:8px; font:inherit; resize:vertical; }
#sys { min-height:36px; max-height:80px; margin-bottom:6px; color:#c5c9d1; }
#user { min-height:64px; max-height:140px; }
#compose-row { display:flex; gap:8px; align-items:center; margin-top:8px; flex-wrap:wrap; }
#hint { color:var(--warn); font-size:11px; flex:1; min-height:1.2em; min-width:8em; }
#chat-meta { color:var(--muted); font-size:11px; white-space:nowrap; display:none; }
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
      <h1>tuide spy
        <span id="llm-lamp" title="Ningún LLM a la escucha"><i></i><span id="llm-lamp-label">LLM off</span></span>
      </h1>
      <p>Historial local · VM y este Mac</p>
    </div>
    <div id="tools">
      <input id="q" placeholder="buscar en prompt / respuesta" autocomplete="off">
      <button id="f-all" class="on">todos</button>
      <button id="f-vm">vm</button>
      <button id="f-yo">yo</button>
      <button id="f-embed">embed</button>
      <button id="f-clear" type="button" title="Vacía la lista de turnos (vm, yo, embed). No borra la memoria del modo chat.">resetear historial</button>
    </div>
    <div id="list"></div>
    <div id="split-compose" class="split split-h" title="Arrastra para redimensionar"></div>
    <div id="compose">
      <textarea id="sys" placeholder="system (opcional)"></textarea>
      <textarea id="user" placeholder="Escribe un prompt…  Option+Enter envía"></textarea>
      <div id="compose-row">
        <span id="hint"></span>
        <span id="chat-meta"></span>
        <button id="m-oneshot" class="on" type="button" title="Un prompt suelto, sin historial">prompt</button>
        <button id="m-chat" type="button" title="Conversación con resumen acumulativo">chat</button>
        <button id="chat-reset" type="button" style="display:none" title="Borra la memoria de esta conversación">nueva conversación</button>
        <button id="send" type="button">enviar</button>
        <button id="stop" type="button" title="Detiene la generación en curso (tú o la VM). Esc.">stop</button>
      </div>
    </div>
  </div>
  <div id="split-side" class="split split-v" title="Arrastra para redimensionar"></div>
  <div id="main">
    <div id="dhead">
      <div id="dhead-meta">ningún turno seleccionado</div>
      <button id="f-prompt" class="on" type="button" title="Ocultar el prompt enviado y dejar solo la salida">prompt</button>
      <button id="f-follow" class="js-follow on" type="button" title="Mantener el foco en el último turno">Anclado al último</button>
    </div>
    <div id="dbody">
      <div class="pane" id="pane-prompt">
        <h2>prompt <button id="f-prompt-pane" type="button" title="Ocultar este panel">ocultar</button></h2>
        <div class="pane-body"><div id="prompt" class="md muted">Selecciona un turno o escribe a la izquierda.</div></div>
      </div>
      <div id="split-io" class="split split-h" title="Arrastra para redimensionar"></div>
      <div class="pane" id="pane-resp">
        <h2>salida <button id="f-md" class="on" type="button" title="Renderizar markdown del prompt y de la salida">md</button></h2>
        <div class="pane-body"><div id="response" class="md muted">—</div></div>
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
let askAbort = null;
let askEnabled = false;
let serverBusy = false;
let follow = true;
let mdView = true;
let promptView = true;
let askMode = "oneshot";
let thread = {on: false, turns: 0, summary_chars: 0, recent_turns: 0};

function esc(s) {
  return String(s || "").replace(/[&<>]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;"}[c]));
}
function kwSet(words) {
  const o = Object.create(null);
  String(words).split(/\s+/).forEach(w => { if (w) o[w] = 1; });
  return o;
}
const LANG_KW = {
  cpp: kwSet("alignas alignof and asm auto bool break case catch char class const consteval constexpr continue decltype default delete do double else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept nullptr operator private protected public return short signed sizeof static static_assert static_cast struct switch template this throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while override final concept requires co_await co_return co_yield"),
  python: kwSet("and as assert async await break class continue def del elif else except False finally for from global if import in is lambda None nonlocal not or pass raise return True try while with yield match case"),
  js: kwSet("async await break case catch class const continue debugger default delete do else export extends false finally for function if import in instanceof let new null return static super switch this throw true try typeof undefined var void while with yield of from as"),
  rust: kwSet("as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type unsafe use where while"),
  go: kwSet("break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var true false nil iota"),
  bash: kwSet("if then else elif fi for while do done case esac in function return break continue select until time coproc true false"),
  cmake: kwSet("if else elseif endif foreach endforeach while endwhile function endfunction macro endmacro set list string option project include message return break continue"),
  json: kwSet("true false null")
};
LANG_KW.ts = Object.assign(kwSet("interface type enum implements private public protected readonly abstract declare namespace module any never unknown"), LANG_KW.js);
function normalizeLang(lang) {
  const x = String(lang || "").toLowerCase().trim();
  if (!x) return "";
  if (/^(c|cc|cxx|c\+\+|cpp|h|hh|hpp|hxx)$/.test(x)) return "cpp";
  if (/^(py|python)$/.test(x)) return "python";
  if (/^(js|javascript|mjs|cjs)$/.test(x)) return "js";
  if (/^(ts|typescript|tsx)$/.test(x)) return "ts";
  if (/^(rs|rust)$/.test(x)) return "rust";
  if (x === "go" || x === "golang") return "go";
  if (/^(sh|bash|zsh|shell)$/.test(x)) return "bash";
  if (x === "json") return "json";
  if (x === "cmake") return "cmake";
  if (/^(html|xml|svg)$/.test(x)) return "html";
  return x;
}
function guessLang(code) {
  const t = String(code || "").trim();
  if (!t) return "";
  if ((t.startsWith("{") || t.startsWith("[")) && /"[^"]+"\s*:/.test(t)) return "json";
  if (/^\s*#include\b|std::|int\s+main\s*\(/.test(t)) return "cpp";
  if (/^#!/.test(t) || /\bthen\b[\s\S]*\bfi\b|\besac\b/.test(t)) return "bash";
  if (/\bdef\s+\w+\s*\(|^\s*import\s+\w+/m.test(t)) return "python";
  if (/\b(fn\s+\w+|let\s+mut\b|impl\s+)/.test(t)) return "rust";
  if (/\bfunc\s+\w+|package\s+\w+/.test(t)) return "go";
  if (/\b(function|const|let|=>)\b/.test(t)) return "js";
  return "";
}
function tok(cls, text) {
  return cls ? '<span class="md-tok-' + cls + '">' + esc(text) + "</span>" : esc(text);
}
function highlightHtml(src) {
  const out = [];
  let i = 0;
  const n = src.length;
  while (i < n) {
    if (src.startsWith("<!--", i)) {
      const end = src.indexOf("-->", i + 4);
      const j = end < 0 ? n : end + 3;
      out.push(tok("cmt", src.slice(i, j)));
      i = j;
      continue;
    }
    if (src[i] === "<") {
      const end = src.indexOf(">", i + 1);
      const j = end < 0 ? n : end + 1;
      out.push(highlightHtmlTag(src.slice(i, j)));
      i = j;
      continue;
    }
    out.push(esc(src[i]));
    i++;
  }
  return out.join("");
}
function highlightHtmlTag(tag) {
  const out = [];
  let i = 0;
  while (i < tag.length) {
    const c = tag[i];
    if (c === '"' || c === "'") {
      let j = i + 1;
      while (j < tag.length && tag[j] !== c) j++;
      j = Math.min(tag.length, j + 1);
      out.push(tok("str", tag.slice(i, j)));
      i = j;
      continue;
    }
    if (/[A-Za-z]/.test(c)) {
      let j = i;
      while (/[A-Za-z0-9:-]/.test(tag[j] || "")) j++;
      const name = tag.slice(i, j);
      const isTag = i <= 2;
      out.push(tok(isTag ? "kw" : "key", name));
      i = j;
      continue;
    }
    out.push(esc(c));
    i++;
  }
  return out.join("");
}
function cheapHighlight(src, lang) {
  lang = normalizeLang(lang);
  if (lang === "html") return highlightHtml(src);
  const kw = LANG_KW[lang] || {};
  const hashCmt = lang === "python" || lang === "bash" || lang === "cmake";
  const cCmt = !hashCmt;
  const out = [];
  let i = 0;
  const n = src.length;
  while (i < n) {
    const c = src[i];
    if (lang === "python" && src[i] === src[i + 1] && src[i] === src[i + 2] && (src[i] === '"' || src[i] === "'")) {
      const q = src.slice(i, i + 3);
      const k = src.indexOf(q, i + 3);
      const j = k < 0 ? n : k + 3;
      out.push(tok("str", src.slice(i, j)));
      i = j;
      continue;
    }
    if (lang === "cpp" && c === "#" && (i === 0 || src[i - 1] === "\n")) {
      let j = i + 1;
      while (j < n && src[j] !== "\n") {
        if (src[j] === "\\" && src[j + 1] === "\n") { j += 2; continue; }
        j++;
      }
      out.push(tok("pp", src.slice(i, j)));
      i = j;
      continue;
    }
    if (cCmt && c === "/" && src[i + 1] === "/") {
      let j = i + 2;
      while (j < n && src[j] !== "\n") j++;
      out.push(tok("cmt", src.slice(i, j)));
      i = j;
      continue;
    }
    if (cCmt && c === "/" && src[i + 1] === "*") {
      let j = i + 2;
      while (j + 1 < n && !(src[j] === "*" && src[j + 1] === "/")) j++;
      j = Math.min(n, j + 2);
      out.push(tok("cmt", src.slice(i, j)));
      i = j;
      continue;
    }
    if (hashCmt && c === "#") {
      let j = i + 1;
      while (j < n && src[j] !== "\n") j++;
      out.push(tok("cmt", src.slice(i, j)));
      i = j;
      continue;
    }
    if (c === '"' || c === "'" || (c === "`" && (lang === "js" || lang === "ts" || lang === "bash"))) {
      let j = i + 1;
      while (j < n) {
        if (src[j] === "\\" && lang !== "bash") { j += 2; continue; }
        if (src[j] === c) { j++; break; }
        if (c !== "`" && src[j] === "\n") break;
        j++;
      }
      const chunk = src.slice(i, j);
      if (lang === "json") {
        let k = j;
        while (k < n && /[ \t\n\r]/.test(src[k])) k++;
        if (src[k] === ":") {
          out.push(tok("key", chunk));
          i = j;
          continue;
        }
      }
      out.push(tok("str", chunk));
      i = j;
      continue;
    }
    if (/[0-9]/.test(c) || (c === "." && /[0-9]/.test(src[i + 1] || ""))) {
      let j = i;
      if (src[j] === "0" && (src[j + 1] === "x" || src[j + 1] === "X")) {
        j += 2;
        while (/[0-9a-fA-F]/.test(src[j] || "")) j++;
      } else {
        while (/[0-9]/.test(src[j] || "")) j++;
        if (src[j] === ".") { j++; while (/[0-9]/.test(src[j] || "")) j++; }
        if (src[j] === "e" || src[j] === "E") {
          j++;
          if (src[j] === "+" || src[j] === "-") j++;
          while (/[0-9]/.test(src[j] || "")) j++;
        }
        if (lang === "cpp" && /[fFuUlL]/.test(src[j] || "")) j++;
      }
      out.push(tok("num", src.slice(i, j)));
      i = j;
      continue;
    }
    if (/[A-Za-z_$]/.test(c)) {
      let j = i;
      while (/[A-Za-z0-9_$]/.test(src[j] || "")) j++;
      const id = src.slice(i, j);
      if (kw[id]) {
        out.push(tok("kw", id));
      } else {
        let k = j;
        while (k < n && /[ \t]/.test(src[k])) k++;
        if (src[k] === "(") out.push(tok("fn", id));
        else if (/^[A-Z]/.test(id) && lang !== "bash" && lang !== "json") out.push(tok("type", id));
        else out.push(esc(id));
      }
      i = j;
      continue;
    }
    out.push(esc(c));
    i++;
  }
  return out.join("");
}
function fencePre(code, lang) {
  const raw = String(code).replace(/\n$/, "");
  const id = normalizeLang(lang) || guessLang(raw);
  const html = cheapHighlight(raw, id);
  const attr = id ? ' data-lang="' + esc(id) + '"' : "";
  const cls = id ? ("language-" + id) : "";
  const label = id ? '<span class="md-fence-lang">' + esc(id) + "</span>" : "<span></span>";
  return '<div class="md-fence"><div class="md-fence-bar">' + label +
    '<button type="button" class="md-copy" title="Copiar fragmento">copiar</button></div>' +
    '<pre class="md-code"' + attr + '><code class="' + cls + '">' + html + "</code></pre></div>";
}
function looksLikeMermaid(code) {
  const t = String(code || "").trim();
  return /^(flowchart(?:\s+\w+)?|graph\s+[A-Za-z]+|sequenceDiagram|classDiagram|stateDiagram(?:-v2)?|erDiagram|gantt|pie|gitGraph|mindmap|timeline|journey|quadrantChart|sankey-beta|xychart-beta|block-beta|C4Context|C4Container)\b/.test(t);
}
function mermaidPre(code) {
  return '<pre class="md-mermaid"><code>' + esc(String(code).replace(/\n$/, "")) + "</code></pre>";
}
function extractBareMermaid(s, fences) {
  const lines = String(s).split("\n");
  const out = [];
  for (let i = 0; i < lines.length; i++) {
    if (looksLikeMermaid(lines[i])) {
      const block = [lines[i]];
      let j = i + 1;
      while (j < lines.length && lines[j].trim() !== "" && lines[j].indexOf("```") < 0) {
        block.push(lines[j]);
        j++;
      }
      if (block.length >= 2) {
        fences.push(mermaidPre(block.join("\n")));
        out.push("%%FENCE" + (fences.length - 1) + "%%");
        i = j - 1;
        continue;
      }
    }
    out.push(lines[i]);
  }
  return out.join("\n");
}
function fenceOpen(line) {
  return String(line).match(/^ {0,3}(`{3,})([^`]*)$/);
}
function fenceClose(line, ticks) {
  const m = String(line).match(/^ {0,3}(`{3,})[ \t]*$/);
  return !!(m && m[1].length >= ticks);
}
function consumeFences(src, fences) {
  fences = fences || [];
  const lines = String(src || "").replace(/\r\n/g, "\n").split("\n");
  const out = [];
  const stack = [];
  let i = 0;
  function isWrap(lang) {
    const x = String(lang || "").toLowerCase();
    return x === "markdown" || x === "md";
  }
  function inCode() {
    return stack.length > 0 && stack[stack.length - 1].body;
  }
  function emitCode(lang, code) {
    const langN = String(lang || "").toLowerCase();
    const mermaid = langN === "mermaid" || langN === "mmd" || looksLikeMermaid(code);
    fences.push(mermaid ? mermaidPre(code) : fencePre(code, lang));
    out.push("%%FENCE" + (fences.length - 1) + "%%");
  }
  while (i < lines.length) {
    let line = lines[i];
    if (!inCode()) {
      const mid = line.match(/^(.*?\S)[ \t]*(`{3,})[\t ]*([A-Za-z][\w.+-]*)[ \t]*$/);
      if (mid && !fenceOpen(line)) {
        out.push(mid[1]);
        line = mid[2] + mid[3];
      }
      if (stack.length && fenceClose(line, stack[stack.length - 1].ticks)) {
        stack.pop();
        i++;
        continue;
      }
      const open = fenceOpen(line);
      if (open) {
        const ticks = open[1].length;
        const info = (open[2] || "").trim();
        const lang = (info.split(/[\t ]+/)[0] || "");
        stack.push({ ticks: ticks, lang: lang, body: isWrap(lang) ? null : [] });
        i++;
        continue;
      }
      out.push(line);
      i++;
      continue;
    }
    const top = stack[stack.length - 1];
    if (fenceClose(line, top.ticks)) {
      emitCode(top.lang, top.body.join("\n"));
      stack.pop();
      i++;
      continue;
    }
    top.body.push(line);
    i++;
  }
  while (stack.length) {
    const top = stack.pop();
    if (top.body) emitCode(top.lang, top.body.join("\n"));
  }
  return out.join("\n");
}
function renderMd(src) {
  const fences = [];
  const codes = [];
  let s = consumeFences(src, fences);
  s = extractBareMermaid(s, fences);
  s = s.replace(/`([^`\n]+)`/g, (_m, code) => {
    codes.push("<code>" + esc(code) + "</code>");
    return "%%CODE" + (codes.length - 1) + "%%";
  });
  s = esc(s);
  s = s.replace(/^###### (.+)$/gm, "<h6>$1</h6>");
  s = s.replace(/^##### (.+)$/gm, "<h5>$1</h5>");
  s = s.replace(/^#### (.+)$/gm, "<h4>$1</h4>");
  s = s.replace(/^### (.+)$/gm, "<h3>$1</h3>");
  s = s.replace(/^## (.+)$/gm, "<h2>$1</h2>");
  s = s.replace(/^# (.+)$/gm, "<h1>$1</h1>");
  s = s.replace(/^&gt; (.+)$/gm, "<blockquote>$1</blockquote>");
  s = s.replace(/^(?:---|\*\*\*|___)$/gm, "<hr>");
  s = s.replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>");
  s = s.replace(/__([^_]+)__/g, "<strong>$1</strong>");
  s = s.replace(/\*([^*\n]+)\*/g, "<em>$1</em>");
  s = s.replace(/\[([^\]]+)\]\((https?:[^)\s]+)\)/g, '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>');
  s = mdTables(s);
  s = s.replace(/(^|\n)((?:[\-\*] .+(?:\n|$))+)/g, (m, p, block) => {
    const items = block.trim().split("\n").map(ln => "<li>" + ln.replace(/^[\-\*] /, "") + "</li>");
    return p + "<ul>" + items.join("") + "</ul>";
  });
  s = s.replace(/(^|\n)((?:\d+\. .+(?:\n|$))+)/g, (m, p, block) => {
    const items = block.trim().split("\n").map(ln => "<li>" + ln.replace(/^\d+\. /, "") + "</li>");
    return p + "<ol>" + items.join("") + "</ol>";
  });
  const lines = s.split("\n");
  const out = [];
  let para = [];
  const flush = () => {
    if (para.length) {
      out.push("<p>" + para.join("<br>") + "</p>");
      para = [];
    }
  };
  for (const line of lines) {
    const t = line.trim();
    if (/^%%FENCE\d+%%$/.test(t) || /^<(h[1-6]|ul|ol|blockquote|hr|table)\b/.test(t)) {
      flush();
      out.push(t);
    } else if (t === "") {
      flush();
    } else {
      para.push(line);
    }
  }
  flush();
  s = out.join("\n");
  s = s.replace(/%%CODE(\d+)%%/g, (_m, i) => codes[Number(i)] || "");
  s = s.replace(/%%FENCE(\d+)%%/g, (_m, i) => fences[Number(i)] || "");
  return s;
}
function mdTableRow(line) {
  return /^\s*\|.+\|\s*$/.test(line);
}
function mdTableSep(line) {
  return /^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)+\|?\s*$/.test(line);
}
function mdTableCells(line) {
  let t = line.trim();
  if (t.startsWith("|")) t = t.slice(1);
  if (t.endsWith("|")) t = t.slice(0, -1);
  return t.split("|").map(c => c.trim());
}
function mdTables(s) {
  const lines = s.split("\n");
  const out = [];
  for (let i = 0; i < lines.length; ) {
    if (mdTableRow(lines[i]) && i + 1 < lines.length && mdTableSep(lines[i + 1])) {
      const header = mdTableCells(lines[i]);
      const aligns = mdTableCells(lines[i + 1]).map(c => {
        const left = c.startsWith(":");
        const right = c.endsWith(":");
        if (left && right) return "center";
        if (right) return "right";
        return "left";
      });
      i += 2;
      const body = [];
      while (i < lines.length && mdTableRow(lines[i])) {
        body.push(mdTableCells(lines[i]));
        i++;
      }
      let html = "<table><thead><tr>";
      header.forEach((c, j) => {
        html += '<th style="text-align:' + (aligns[j] || "left") + '">' + c + "</th>";
      });
      html += "</tr></thead><tbody>";
      body.forEach(row => {
        html += "<tr>";
        header.forEach((_, j) => {
          html += '<td style="text-align:' + (aligns[j] || "left") + '">' + (row[j] || "") + "</td>";
        });
        html += "</tr>";
      });
      html += "</tbody></table>";
      out.push(html);
      continue;
    }
    out.push(lines[i]);
    i++;
  }
  return out.join("\n");
}
let mermaidPromise = null;
let mermaidSeq = 0;
function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement("script");
    s.src = src;
    s.onload = () => resolve();
    s.onerror = () => reject(new Error(src));
    document.head.appendChild(s);
  });
}
function loadMermaid() {
  if (window.mermaid && window.mermaid.render) {
    return Promise.resolve(window.mermaid);
  }
  if (!mermaidPromise) {
    const cdns = [
      "https://cdn.jsdelivr.net/npm/mermaid@11.4.1/dist/mermaid.min.js",
      "https://unpkg.com/mermaid@11.4.1/dist/mermaid.min.js",
    ];
    mermaidPromise = (async () => {
      for (const src of cdns) {
        try {
          await loadScript(src);
          if (window.mermaid && window.mermaid.initialize) {
            window.mermaid.initialize({
              startOnLoad: false,
              securityLevel: "strict",
              theme: "dark",
              fontFamily: "ui-monospace, SFMono-Regular, Menlo, Monaco, monospace",
            });
            return window.mermaid;
          }
        } catch (e) {}
      }
      return null;
    })();
  }
  return mermaidPromise;
}
function markMermaidFail(el, msg) {
  el.classList.add("md-code");
  if (el.querySelector(".md-mermaid-fail")) return;
  const note = document.createElement("div");
  note.className = "md-mermaid-fail";
  note.textContent = msg;
  el.insertBefore(note, el.firstChild);
}
function hydrateMermaid(root) {
  const nodes = root.querySelectorAll("pre.md-mermaid");
  if (!nodes.length) return;
  loadMermaid().then(mermaid => {
    if (!root.isConnected) return;
    if (!mermaid) {
      nodes.forEach(el => markMermaidFail(el, "No se pudo cargar Mermaid (CDN / red)."));
      return;
    }
    nodes.forEach(el => {
      if (!el.isConnected) return;
      const src = (el.textContent || "").trim();
      if (!src) return;
      const id = "spy-mmd-" + (++mermaidSeq);
      const run = mermaid.render(id, src);
      Promise.resolve(run).then(out => {
        if (!el.isConnected) return;
        const svg = typeof out === "string" ? out : (out && out.svg);
        if (!svg) throw new Error("sin svg");
        const wrap = document.createElement("div");
        wrap.className = "md-mermaid-out";
        wrap.innerHTML = svg;
        el.replaceWith(wrap);
      }).catch(() => {
        markMermaidFail(el, "Mermaid no pudo dibujar este diagrama.");
      });
    });
  });
}
function srcOf(r) {
  if (r.src) return r.src;
  if (r.tag === "direct") return "direct";
  return "vm";
}
function badge(r) {
  if (r.tag === "embed") return "embed";
  if (r.log_kind === "summary" || r.kind === "summary") return "memoria";
  return srcOf(r) === "direct" ? "yo" : "vm";
}
function apply(ev) {
  if (!ev || !ev.kind) return;
  if (ev.kind === "req") {
    reqs.set(ev.id, {
      id: ev.id, tag: ev.tag || "chat", src: ev.src || (ev.tag === "direct" ? "direct" : "vm"),
      ts: ev.ts, user: ev.user || "", system: ev.system || "", prompt: ev.prompt || "",
      log_kind: ev.log_kind || "", response: "",
      streaming: true, seconds: null, model: ev.model || "",
      kind: ev.log_kind === "summary" ? "summary" : "chat"
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
      if (ev.log_kind) r.log_kind = ev.log_kind;
    }
  } else if (ev.kind === "embed") {
    reqs.set(ev.id, {
      id: ev.id, tag: "embed", src: "vm", ts: ev.ts, kind: "embed",
      user: (ev.samples || []).join("\n"), system: "", prompt: "", log_kind: "",
      response: "HTTP " + ev.status + "  batch=" + ev.batch + "  " + (ev.seconds || 0) + "s",
      streaming: false, seconds: ev.seconds, status: ev.status
    });
    order.push(ev.id);
  }
}
function match(r) {
  const b = badge(r);
  if (filter === "vm" && b !== "vm") return false;
  if (filter === "yo" && b !== "yo" && b !== "memoria") return false;
  if (filter === "embed" && b !== "embed") return false;
  if (!query) return true;
  const q = query.toLowerCase();
  return (r.user + "\n" + r.system + "\n" + (r.prompt || "") + "\n" + r.response).toLowerCase().includes(q);
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
function syncMdBtn() {
  const btn = document.getElementById("f-md");
  if (!btn) return;
  btn.classList.toggle("on", mdView);
  btn.textContent = mdView ? "md" : "raw";
  btn.title = mdView ? "Mostrar texto crudo (prompt y salida)" : "Renderizar markdown (prompt y salida)";
}
function setMdView(on) {
  mdView = !!on;
  try { localStorage.setItem("tuide-spy-md", mdView ? "1" : "0"); } catch (e) {}
  syncMdBtn();
}
function syncPromptBtn() {
  const btn = document.getElementById("f-prompt");
  const dbody = document.getElementById("dbody");
  if (dbody) dbody.classList.toggle("hide-prompt", !promptView);
  if (btn) {
    btn.classList.toggle("on", promptView);
    btn.textContent = promptView ? "prompt" : "mostrar prompt";
    btn.title = promptView ? "Ocultar el prompt enviado y dejar solo la salida" : "Mostrar el prompt enviado";
  }
}
function setPromptView(on) {
  promptView = !!on;
  try { localStorage.setItem("tuide-spy-prompt", promptView ? "1" : "0"); } catch (e) {}
  syncPromptBtn();
}
function anyChatLive() {
  for (const r of reqs.values()) {
    if (r.streaming && r.tag !== "embed") return true;
  }
  return false;
}
function fmtChars(n) {
  n = Number(n) || 0;
  if (n >= 1000) return (n / 1000).toFixed(n >= 10000 ? 0 : 1).replace(/\.0$/, "") + "k";
  return String(n);
}
function applyThread(t) {
  if (!t || typeof t !== "object") return;
  thread = {
    on: !!t.on,
    turns: t.turns || 0,
    summary_chars: t.summary_chars || 0,
    recent_turns: t.recent_turns || 0
  };
}
function setAskMode(m) {
  askMode = m === "chat" ? "chat" : "oneshot";
  document.getElementById("m-oneshot").classList.toggle("on", askMode === "oneshot");
  document.getElementById("m-chat").classList.toggle("on", askMode === "chat");
  try { localStorage.setItem("tuide-spy-ask-mode", askMode); } catch (e) {}
  syncComposer();
}
function syncComposer() {
  const busy = sending || serverBusy || anyChatLive();
  const user = document.getElementById("user");
  const btn = document.getElementById("send");
  const stop = document.getElementById("stop");
  const hint = document.getElementById("hint");
  const meta = document.getElementById("chat-meta");
  const reset = document.getElementById("chat-reset");
  if (!user || !btn || !hint) return;
  btn.disabled = !askEnabled || busy || !user.value.trim();
  if (stop) {
    stop.style.display = busy ? "inline-block" : "none";
    stop.disabled = !busy;
  }
  if (reset) {
    reset.style.display = askMode === "chat" ? "inline-block" : "none";
    reset.disabled = !askEnabled || busy;
  }
  if (meta) {
    const chatOn = askMode === "chat";
    meta.style.display = chatOn ? "inline" : "none";
    if (chatOn) {
      meta.textContent = thread.turns + " turnos · resumen " + fmtChars(thread.summary_chars);
    }
  }
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
    promptEl.className = "md muted";
    respEl.className = "md muted";
    promptEl.textContent = "Selecciona un turno o escribe a la izquierda.";
    respEl.textContent = "—";
    return;
  }
  const t = r.ts ? new Date(r.ts * 1000).toLocaleString() : "";
  const extra = r.error ? " · error" : (r.streaming ? " · generando…" : "");
  document.getElementById("dhead-meta").textContent =
    badge(r) + " · " + t + (r.model ? " · " + r.model : "") + extra;
  const prompt = r.prompt || ((r.system ? "—— system ——\n" + r.system + "\n\n" : "") +
                 (r.user ? "—— user ——\n" + r.user : "(sin user)"));
  if (mdView) {
    promptEl.className = "md";
    promptEl.innerHTML = renderMd(prompt);
    if (promptView) hydrateMermaid(promptEl);
  } else {
    promptEl.className = "md";
    promptEl.textContent = prompt;
  }
  const body = r.response || (r.streaming ? "…" : (r.error || "(vacío)"));
  const mute = (!r.response && (r.error || !r.streaming));
  if (mdView && r.response) {
    respEl.className = "md" + (r.error ? " muted" : "");
    respEl.innerHTML = renderMd(r.response);
    if (!r.streaming) hydrateMermaid(respEl);
  } else {
    respEl.className = mute ? "md muted" : "md";
    respEl.textContent = body;
  }
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
    applyThread(st.thread);
    document.getElementById("compose").style.display = askEnabled ? "flex" : "none";
    document.getElementById("split-compose").style.display = askEnabled ? "block" : "none";
    const lamp = document.getElementById("llm-lamp");
    const lab = document.getElementById("llm-lamp-label");
    if (lamp && lab) {
      lamp.classList.toggle("on", askEnabled);
      const chat = st.chat || {};
      const name = chat.label || chat.alias || "";
      lab.textContent = askEnabled ? (name ? "LLM · " + name : "LLM a la escucha") : "LLM off";
      lamp.title = askEnabled
        ? (name ? "LLM a la escucha: " + name : "LLM a la escucha")
        : "Ningún LLM a la escucha";
    }
    syncComposer();
  } catch (e) {}
}
async function sendAsk() {
  const userEl = document.getElementById("user");
  const user = userEl.value.trim();
  const system = document.getElementById("sys").value;
  if (!user || sending || !askEnabled) return;
  sending = true;
  askAbort = typeof AbortController !== "undefined" ? new AbortController() : null;
  syncComposer();
  try {
    const res = await fetch("/api/ask", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({user, system, mode: askMode}),
      signal: askAbort ? askAbort.signal : undefined,
    });
    const data = await res.json();
    if (data.thread) applyThread(data.thread);
    if (data.id) sel = data.id;
    if (data.stopped) {
      document.getElementById("hint").textContent = "generación detenida";
      userEl.value = "";
    } else if (!data.ok) {
      document.getElementById("hint").textContent = data.error || "error";
    } else {
      userEl.value = "";
    }
  } catch (e) {
    if (!e || e.name !== "AbortError") {
      document.getElementById("hint").textContent = "no se pudo enviar";
    }
  }
  askAbort = null;
  sending = false;
  await poll();
  syncComposer();
}
async function stopGenerate() {
  try {
    await fetch("/api/ask/stop", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: "{}",
    });
  } catch (e) {}
  try { if (askAbort) askAbort.abort(); } catch (e) {}
}
async function resetChat() {
  if (sending || !askEnabled) return;
  sending = true;
  syncComposer();
  try {
    const res = await fetch("/api/ask", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({mode: "chat", reset: true}),
    });
    const data = await res.json();
    if (data.thread) applyThread(data.thread);
    else applyThread({on: false, turns: 0, summary_chars: 0, recent_turns: 0});
    if (!data.ok) {
      document.getElementById("hint").textContent = data.error || "error";
    }
  } catch (e) {
    document.getElementById("hint").textContent = "no se pudo resetear";
  }
  sending = false;
  syncComposer();
}
async function clearHistory() {
  if (!confirm("¿Vaciar el historial de turnos (vm, yo, embed)?")) return;
  try {
    const res = await fetch("/api/history/clear", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: "{}",
    });
    const data = await res.json().catch(() => ({}));
    if (!data.ok) {
      document.getElementById("hint").textContent = data.error || "no se pudo vaciar";
      return;
    }
    reqs.clear();
    order = [];
    sel = null;
    off = 0;
    render();
  } catch (e) {
    document.getElementById("hint").textContent = "no se pudo vaciar el historial";
  }
}
function copyText(text) {
  if (navigator.clipboard && navigator.clipboard.writeText) {
    return navigator.clipboard.writeText(text);
  }
  return new Promise((resolve, reject) => {
    const ta = document.createElement("textarea");
    ta.value = text;
    ta.setAttribute("readonly", "");
    ta.style.position = "fixed";
    ta.style.top = "0";
    ta.style.left = "0";
    ta.style.opacity = "0";
    document.body.appendChild(ta);
    ta.focus();
    ta.select();
    try {
      if (document.execCommand("copy")) resolve();
      else reject(new Error("copy"));
    } catch (err) { reject(err); }
    ta.remove();
  });
}
document.addEventListener("click", (e) => {
  const btn = e.target.closest && e.target.closest(".md-copy");
  if (!btn) return;
  e.preventDefault();
  const fence = btn.closest(".md-fence");
  const code = fence && fence.querySelector("pre.md-code code");
  const text = code ? code.textContent : "";
  copyText(text).then(() => {
    btn.textContent = "copiado";
    btn.classList.add("on");
    setTimeout(() => {
      if (!btn.isConnected) return;
      btn.textContent = "copiar";
      btn.classList.remove("on");
    }, 1200);
  }).catch(() => {
    btn.textContent = "error";
    setTimeout(() => { if (btn.isConnected) btn.textContent = "copiar"; }, 1200);
  });
});
document.getElementById("q").oninput = (e) => { query = e.target.value.trim(); renderList(); };
document.getElementById("user").oninput = syncComposer;
document.getElementById("send").onclick = sendAsk;
document.getElementById("stop").onclick = stopGenerate;
document.getElementById("m-oneshot").onclick = () => setAskMode("oneshot");
document.getElementById("m-chat").onclick = () => setAskMode("chat");
document.getElementById("chat-reset").onclick = resetChat;
document.getElementById("f-clear").onclick = clearHistory;
function bindSendKeys(el) {
  if (!el) return;
  el.addEventListener("keydown", (e) => {
    if (e.key !== "Enter") return;
    if (!(e.altKey || e.ctrlKey || e.metaKey)) return;
    e.preventDefault();
    sendAsk();
  });
}
bindSendKeys(document.getElementById("user"));
bindSendKeys(document.getElementById("sys"));
document.addEventListener("keydown", (e) => {
  if (e.key !== "Escape") return;
  if (!(sending || serverBusy || anyChatLive())) return;
  e.preventDefault();
  stopGenerate();
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
document.getElementById("f-md").onclick = () => {
  setMdView(!mdView);
  render();
};
document.getElementById("f-prompt").onclick = () => {
  setPromptView(!promptView);
};
document.getElementById("f-prompt-pane").onclick = () => {
  setPromptView(false);
};
try {
  const savedFollow = localStorage.getItem("tuide-spy-follow");
  if (savedFollow === "0") follow = false;
} catch (e) {}
try {
  const savedMd = localStorage.getItem("tuide-spy-md");
  if (savedMd === "0") mdView = false;
} catch (e) {}
try {
  const savedPrompt = localStorage.getItem("tuide-spy-prompt");
  if (savedPrompt === "0") promptView = false;
} catch (e) {}
try {
  const savedMode = localStorage.getItem("tuide-spy-ask-mode");
  if (savedMode === "chat" || savedMode === "oneshot") askMode = savedMode;
} catch (e) {}
syncFollowBtn();
syncMdBtn();
syncPromptBtn();
setAskMode(askMode);
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


def run_direct_ask(
    user: str,
    system: str,
    backend_host: str,
    backend_port: int,
    messages: Optional[List[dict]] = None,
    log_kind: str = "",
) -> Tuple[str, str, str]:
    """Returns (text, req_id, error)."""
    if messages is None:
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
            log_kind=log_kind,
        )
        return text, rid, err
    finally:
        conn.close()


def chat_thread_status() -> dict:
    with chat_session_lock:
        return chat_session.status()


def reset_chat_session() -> dict:
    with chat_turn_lock:
        with chat_session_lock:
            chat_session.reset()
            clear_last_chat_ctx()
            return chat_session.status()


def _compact_overflow(backend_host: str, backend_port: int) -> None:
    """Fold overflow recent turns into the rolling summary (same GGUF).

    Caller holds chat_turn_lock. Session lock is not held during the LLM call.
    """
    with chat_session_lock:
        if not chat_session.needs_compact():
            if len(chat_session.summary) > chat_session.summary_max_chars:
                chat_session.apply_summary(chat_session.summary)
            return
        dropped = chat_session.pop_overflow()
        old_summary = chat_session.summary
    if not dropped:
        return
    sys_p, user_p = chat_mem.summary_prompt(old_summary, dropped)
    text, _rid, err = run_direct_ask(
        user_p,
        sys_p,
        backend_host,
        backend_port,
        log_kind="summary",
    )
    with chat_session_lock:
        if err or not (text or "").strip():
            folded = chat_mem.extractive_fold(
                old_summary, dropped, chat_session.summary_max_chars
            )
            chat_session.apply_summary(folded)
        else:
            chat_session.apply_summary(text)


def _ask_result(text: str, rid: str, err: str) -> Tuple[int, dict]:
    stopped = err == "stopped"
    payload: dict = {
        "ok": not err or stopped,
        "id": rid,
        "chars": len(text),
        "thread": chat_thread_status(),
    }
    if stopped:
        payload["stopped"] = True
        return 200, payload
    if err:
        payload["error"] = err
        return 502, payload
    return 200, payload


def handle_ask_post(obj: dict, backend_host: str, backend_port: int) -> Tuple[int, dict]:
    """Shared /api/ask body handler for hub and standalone spy web."""
    user = str(obj.get("user") or "").strip()
    system = str(obj.get("system") or "")
    mode = str(obj.get("mode") or "oneshot").strip().lower()
    if mode not in ("oneshot", "chat"):
        mode = "oneshot"
    reset = bool(obj.get("reset"))

    if mode != "chat":
        if reset:
            reset_chat_session()
        if not user:
            return 400, {"ok": False, "error": "user vacío"}
        text, rid, err = run_direct_ask(user, system, backend_host, backend_port)
        return _ask_result(text, rid, err)

    with chat_turn_lock:
        with chat_session_lock:
            if reset:
                chat_session.reset()
                clear_last_chat_ctx()
            if not user:
                if reset:
                    return 200, {"ok": True, "thread": chat_session.status()}
                return 400, {"ok": False, "error": "user vacío"}
            if system.strip():
                chat_session.system = system
        _compact_overflow(backend_host, backend_port)
        with chat_session_lock:
            messages = chat_session.build_messages(user[:ASK_USER_MAX])
            sticky_system = chat_session.system
        text, rid, err = run_direct_ask(
            user,
            sticky_system,
            backend_host,
            backend_port,
            messages=messages,
            log_kind="chat",
        )
        with chat_session_lock:
            if not err or (err == "stopped" and (text or "").strip()):
                chat_session.commit(user[:ASK_USER_MAX], text)
        return _ask_result(text, rid, err)


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
            self._json(
                200,
                {"ask": self.ask_enabled, "busy": chat_busy(), "thread": chat_thread_status()},
            )
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
        if parsed.path == "/api/history/clear":
            self._json(200, clear_history())
            return
        if parsed.path == "/api/ask/stop":
            self._json(200, request_stop_generation(self.backend_host, self.backend_port))
            return
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
        try:
            code, payload = handle_ask_post(obj, self.backend_host, self.backend_port)
        except (TimeoutError, socket.timeout) as ex:
            self._json(504, {"ok": False, "error": f"timeout: {ex}"})
            return
        except OSError as ex:
            self._json(502, {"ok": False, "error": str(ex)})
            return
        self._json(code, payload)


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
