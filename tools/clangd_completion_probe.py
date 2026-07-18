#!/usr/bin/env python3
import json
import os
import select
import subprocess
import sys
import time
from pathlib import Path

WORKSPACE = Path("/home/lorenzo/workspace/tgdb")
FILE = WORKSPACE / "src/app/application.cpp"
COMPILE_DIR = WORKSPACE / ".tuide"


def read_message(stream) -> dict:
    headers = {}
    while True:
        line = stream.readline()
        if not line:
            raise EOFError("clangd stdout closed")
        line = line.decode("utf-8").strip()
        if line == "":
            break
        key, value = line.split(":", 1)
        headers[key.strip()] = value.strip()
    length = int(headers.get("Content-Length", "0"))
    body = stream.read(length)
    return json.loads(body)


def write_message(proc, payload: dict) -> None:
    body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    proc.stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode("utf-8"))
    proc.stdin.write(body)
    proc.stdin.flush()


def wait_response(proc, req_id: int, timeout_s: float = 30.0) -> dict:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], max(0.1, deadline - time.time()))
        if not ready:
            continue
        msg = read_message(proc.stdout)
        if msg.get("id") == req_id:
            return msg
    raise TimeoutError(f"no response for id={req_id}")


def line_col_for_token(text: str, line: int, token: str) -> tuple[int, int]:
    rows = text.splitlines()
    if line >= len(rows):
        return line, 0
    idx = rows[line].find(token)
    if idx < 0:
        return line, 0
    return line, idx + len(token)


def utf16_col(line_text: str, byte_col: int) -> int:
    utf16 = 0
    byte = 0
    while byte < byte_col and byte < len(line_text):
        c = ord(line_text[byte])
        if c & 0xF8 == 0xF0:
            byte += 4
            utf16 += 2
        elif c & 0xF0 == 0xE0:
            byte += 3
            utf16 += 1
        elif c & 0xE0 == 0xC0:
            byte += 2
            utf16 += 1
        else:
            byte += 1
            utf16 += 1
    return utf16


def completion_count(proc, req_id: int, uri: str, text: str, line: int, byte_col: int,
                     label: str) -> int:
    rows = text.splitlines()
    line_text = rows[line] if line < len(rows) else ""
    params = {
        "textDocument": {"uri": uri},
        "position": {"line": line, "character": utf16_col(line_text, byte_col)},
        "context": {"triggerKind": 1},
    }
    started = time.time()
    write_message(proc, {"jsonrpc": "2.0", "id": req_id, "method": "textDocument/completion",
                         "params": params})
    resp = wait_response(proc, req_id, timeout_s=60.0)
    elapsed_ms = int((time.time() - started) * 1000)
    result = resp.get("result")
    if isinstance(result, list):
        items = result
    elif isinstance(result, dict) and isinstance(result.get("items"), list):
        items = result["items"]
    else:
        items = []
    print(f"{label}: line={line} col={byte_col} items={len(items)} elapsed={elapsed_ms}ms",
          flush=True)
    return len(items)


def main() -> int:
    text = FILE.read_text(encoding="utf-8")
    uri = FILE.resolve().as_uri()

    proc = subprocess.Popen(
        [
            "clangd",
            f"--compile-commands-dir={COMPILE_DIR}",
            "--background-index=false",
            "--log=error",
        ],
        cwd=WORKSPACE,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )

    try:
        write_message(proc, {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "processId": os.getpid(),
                "rootUri": WORKSPACE.resolve().as_uri(),
                "capabilities": {},
            },
        })
        wait_response(proc, 1, timeout_s=30.0)
        write_message(proc, {"jsonrpc": "2.0", "method": "initialized", "params": {}})

        write_message(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": uri,
                    "languageId": "cpp",
                    "version": 1,
                    "text": text,
                }
            },
        })

        idle_deadline = time.time() + 45.0
        while time.time() < idle_deadline:
            ready, _, _ = select.select([proc.stdout], [], [], 0.5)
            if not ready:
                continue
            msg = read_message(proc.stdout)
            if msg.get("method") == "$/clangd/fileStatus":
                state = msg.get("params", {}).get("state")
                if state == "idle":
                    break

        early_line, early_col = line_col_for_token(text, 100, "void")
        late_line, late_col = line_col_for_token(text, 2470, "screen")
        early = completion_count(proc, 3, uri, text, early_line, early_col, "early")
        late = completion_count(proc, 4, uri, text, late_line, late_col, "late")
        return 0 if late > 0 else (1 if early > 0 else 2)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
