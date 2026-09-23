#!/usr/bin/env python3
"""OpenAI-compatible proxy: harness propose → pending.json → Cursor fills response.json.

T0: the filler must answer ONLY from system+user (no repo tools).
"""

from __future__ import annotations

import argparse
import json
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--dir",
        type=Path,
        default=Path(".tuide/ai/l2_wave/v2_night/H_T0/proxy"),
    )
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=18080)
    ap.add_argument("--timeout", type=float, default=2400.0)
    args = ap.parse_args()
    root: Path = args.dir
    root.mkdir(parents=True, exist_ok=True)
    pending = root / "pending.json"
    response = root / "response.json"
    logf = root / "proxy.log"
    lock = threading.Lock()

    def log(msg: str) -> None:
        line = f"{time.strftime('%Y-%m-%dT%H:%M:%S')} {msg}\n"
        with lock:
            with logf.open("a", encoding="utf-8") as f:
                f.write(line)
        print(line, end="", flush=True)

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt: str, *a) -> None:  # noqa: A003
            log("http " + (fmt % a))

        def _json(self, code: int, obj: dict) -> None:
            raw = json.dumps(obj, ensure_ascii=False).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def do_GET(self) -> None:  # noqa: N802
            if self.path.rstrip("/").endswith("/models") or self.path.endswith("/v1/models"):
                self._json(
                    200,
                    {
                        "object": "list",
                        "data": [
                            {
                                "id": "cursor-t0",
                                "object": "model",
                                "owned_by": "tide-t0-proxy",
                            }
                        ],
                    },
                )
                return
            self._json(404, {"error": "not found"})

        def do_POST(self) -> None:  # noqa: N802
            if "/chat/completions" not in self.path:
                self._json(404, {"error": "not found"})
                return
            n = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(n)
            try:
                req = json.loads(body.decode("utf-8"))
            except json.JSONDecodeError as e:
                self._json(400, {"error": str(e)})
                return
            messages = req.get("messages") or []
            system = ""
            user = ""
            for m in messages:
                role = m.get("role") or ""
                content = m.get("content") or ""
                if isinstance(content, list):
                    content = "".join(
                        (p.get("text") or "") if isinstance(p, dict) else str(p) for p in content
                    )
                if role == "system":
                    system = str(content)
                elif role == "user":
                    user = str(content)
            rid = str(uuid.uuid4())
            payload = {
                "id": rid,
                "created": time.time(),
                "model": req.get("model") or "cursor-t0",
                "phase_hint": "control_v1 if PILOTO; ola_v1 if EXPLORADOR",
                "system": system,
                "user": user,
            }
            with lock:
                if response.exists():
                    response.unlink()
                pending.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
                (root / "PENDING").write_text(rid + "\n", encoding="utf-8")
            log(f"WAIT id={rid} system_chars={len(system)} user_chars={len(user)}")
            deadline = time.time() + args.timeout
            content = None
            while time.time() < deadline:
                if response.is_file():
                    try:
                        data = json.loads(response.read_text(encoding="utf-8"))
                    except (json.JSONDecodeError, OSError):
                        time.sleep(0.4)
                        continue
                    if str(data.get("id") or "") not in ("", rid):
                        # stale
                        time.sleep(0.2)
                        continue
                    content = data.get("content") or data.get("text") or ""
                    if content:
                        break
                time.sleep(0.4)
            with lock:
                if pending.exists():
                    try:
                        pending.unlink()
                    except OSError:
                        pass
                if (root / "PENDING").exists():
                    try:
                        (root / "PENDING").unlink()
                    except OSError:
                        pass
                if response.exists():
                    try:
                        response.unlink()
                    except OSError:
                        pass
            if not content:
                log(f"TIMEOUT id={rid}")
                self._json(504, {"error": {"message": "t0 proxy timeout waiting for Cursor"}})
                return
            log(f"OK id={rid} content_chars={len(content)}")
            self._json(
                200,
                {
                    "id": rid,
                    "object": "chat.completion",
                    "model": "cursor-t0",
                    "choices": [
                        {
                            "index": 0,
                            "message": {"role": "assistant", "content": content},
                            "finish_reason": "stop",
                        }
                    ],
                },
            )

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    log(f"LISTEN http://{args.host}:{args.port}/v1 dir={root}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
