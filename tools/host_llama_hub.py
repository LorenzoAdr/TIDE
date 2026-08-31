#!/usr/bin/env python3
"""Host hub: modo Lanzamiento (descargar/arrancar/parar) + modo Inspección (spy)."""
from __future__ import annotations

import argparse
import errno
import http.server
import json
import os
import platform
import re
import shutil
import signal
import socket
import socketserver
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

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

# Last chat/embed argv summary (for /api/status).
last_perf: Dict[str, Any] = {"chat": {}, "embed": {}}
launch_lock = threading.Lock()
launch_opts: Dict[str, str] = {}

DRAFT_CATALOG_ID = "qwen2.5-1.5b-instruct-q4_k_m"
LAUNCH_KEYS = (
    "flash_attn", "cache_type", "threads", "np", "ngl", "chat_ctx",
    "embed_ngl", "embed_ctx", "embed_np", "draft", "draft_n_max", "draft_gguf",
    "thinking",
)

# GGUF families whose chat template emits <think> / reasoning_content.
THINKING_MARKERS = (
    "qwen3", "qwen-3",
    "qwq",
    "deepseek-r1", "deepseek_r1", "r1-distill", "r1_distill",
    "gpt-oss", "gpt_oss",
    "magistral",
    "glm-4.5", "glm4.5", "glm-4.6", "glm4.6", "glm-4.7", "glm4.7",
    "phi-4-reasoning", "phi4-reasoning",
    "hunyuan",
    "seed-oss",
    "kimi-k1", "kimi_k1",
)


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


def env_trimmed(key: str, default: str = "") -> str:
    val = os.environ.get(key)
    if val is None:
        return default
    return val.strip()


def parse_on_off_auto(value: str, default: str = "auto") -> str:
    raw = (value or default).strip().lower()
    if raw in ("0", "off", "false", "no", "none"):
        return "off"
    if raw in ("1", "on", "true", "yes"):
        return "on"
    if raw in ("auto", ""):
        return "auto"
    return raw


def detect_performance_cores() -> int:
    if platform.system() == "Darwin":
        try:
            out = subprocess.check_output(
                ["sysctl", "-n", "hw.perflevel0.logicalcpu"],
                text=True,
                stderr=subprocess.DEVNULL,
            ).strip()
            n = int(out)
            if n > 0:
                return n
        except (subprocess.CalledProcessError, FileNotFoundError, ValueError):
            pass
    return os.cpu_count() or 4


def opt_or_env(opt_key: str, env_key: str, default: str = "") -> str:
    with launch_lock:
        if opt_key in launch_opts:
            return str(launch_opts[opt_key]).strip()
    return env_trimmed(env_key, default)


def performance_core_count() -> int:
    raw = opt_or_env("threads", "TUIDE_HOST_THREADS")
    if raw.isdigit() and int(raw) > 0:
        return int(raw)
    return detect_performance_cores()


def total_ram_bytes() -> int:
    if platform.system() == "Darwin":
        try:
            out = subprocess.check_output(
                ["sysctl", "-n", "hw.memsize"],
                text=True,
                stderr=subprocess.DEVNULL,
            ).strip()
            return int(out)
        except (subprocess.CalledProcessError, FileNotFoundError, ValueError):
            pass
    try:
        pages = os.sysconf("SC_PHYS_PAGES")
        page = os.sysconf("SC_PAGE_SIZE")
        if pages > 0 and page > 0:
            return int(pages) * int(page)
    except (ValueError, OSError):
        pass
    return 0


_host_lock = threading.Lock()
_host_latest: Dict[str, Any] = {}
_host_sampler_started = False
_cpu_prev: Optional[Tuple[float, List[int]]] = None
_gguf_meta_cache: Dict[str, Tuple[int, int, Dict[str, Any]]] = {}

_GGUF_FIXED = {
    0: 1, 1: 1, 2: 2, 3: 2, 4: 4, 5: 4, 6: 4, 7: 1,
    10: 8, 11: 8, 12: 8,
}


def _run_capture(cmd: List[str], timeout: float = 0.6) -> str:
    try:
        return subprocess.check_output(
            cmd, text=True, stderr=subprocess.DEVNULL, timeout=timeout
        )
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError, OSError):
        return ""


def _u32le(buf: bytes, off: int) -> Tuple[int, int]:
    return int.from_bytes(buf[off:off + 4], "little"), off + 4


def _u64le(buf: bytes, off: int) -> Tuple[int, int]:
    return int.from_bytes(buf[off:off + 8], "little"), off + 8


def _gguf_string(buf: bytes, off: int) -> Tuple[str, int]:
    n, off = _u64le(buf, off)
    end = off + n
    if n > 1_000_000 or end > len(buf):
        raise ValueError("gguf string")
    return buf[off:end].decode("utf-8", "replace"), end


def _gguf_skip(buf: bytes, off: int, typ: int) -> int:
    if typ == 8:
        _, off = _gguf_string(buf, off)
        return off
    if typ == 9:
        subtype, off = _u32le(buf, off)
        n, off = _u64le(buf, off)
        for _ in range(min(n, 1_000_000)):
            off = _gguf_skip(buf, off, subtype)
        return off
    width = _GGUF_FIXED.get(typ)
    if width is None:
        raise ValueError("gguf type")
    return off + width


def _gguf_value(buf: bytes, off: int, typ: int) -> Tuple[Any, int]:
    if typ == 8:
        return _gguf_string(buf, off)
    if typ == 7:
        return buf[off] != 0, off + 1
    if typ == 4:
        v, off = _u32le(buf, off)
        return v, off
    if typ == 5:
        v = int.from_bytes(buf[off:off + 4], "little", signed=True)
        return v, off + 4
    if typ in (10, 11):
        v = int.from_bytes(buf[off:off + 8], "little", signed=(typ == 11))
        return v, off + 8
    if typ in (0, 1):
        return buf[off], off + 1
    if typ in (2, 3):
        v = int.from_bytes(buf[off:off + 2], "little", signed=(typ == 3))
        return v, off + 2
    off = _gguf_skip(buf, off, typ)
    return None, off


def read_gguf_meta(path: str) -> Dict[str, Any]:
    try:
        st = Path(path).stat()
    except OSError:
        return {}
    cached = _gguf_meta_cache.get(path)
    if cached and cached[0] == int(st.st_mtime) and cached[1] == st.st_size:
        return cached[2]
    meta: Dict[str, Any] = {}
    raw: Dict[str, Any] = {}
    try:
        with open(path, "rb") as fh:
            buf = fh.read(2 * 1024 * 1024)
        if buf[:4] != b"GGUF":
            return {}
        off = 4
        _ver, off = _u32le(buf, off)
        _nt, off = _u64le(buf, off)
        n_kv, off = _u64le(buf, off)
        for _ in range(min(int(n_kv), 400)):
            key, off = _gguf_string(buf, off)
            typ, off = _u32le(buf, off)
            if typ in (4, 5, 8, 10, 11):
                val, off = _gguf_value(buf, off, typ)
                raw[key] = val
            else:
                off = _gguf_skip(buf, off, typ)
    except (OSError, ValueError, IndexError):
        raw = {}
    arch = str(raw.get("general.architecture") or "")
    if arch:
        meta["arch"] = arch
        meta["n_layer"] = int(raw.get(f"{arch}.block_count") or 0)
        meta["n_embd"] = int(raw.get(f"{arch}.embedding_length") or 0)
        meta["n_head"] = int(raw.get(f"{arch}.attention.head_count") or 0)
        meta["n_head_kv"] = int(raw.get(f"{arch}.attention.head_count_kv") or meta.get("n_head") or 0)
    _gguf_meta_cache[path] = (int(st.st_mtime), st.st_size, meta)
    return meta


def kv_elem_bytes(cache_type: str) -> float:
    raw = (cache_type or "f16").lower()
    if raw in ("", "off", "none", "f16", "fp16"):
        return 2.0
    if raw in ("bf16",):
        return 2.0
    if raw.startswith("q8"):
        return 1.0
    if raw.startswith("q6"):
        return 0.75
    if raw.startswith("q5"):
        return 0.625
    if raw.startswith("q4"):
        return 0.5
    return 2.0


def estimate_kv_bytes(
    n_layer: int,
    n_embd: int,
    n_head: int,
    n_head_kv: int,
    n_ctx: int,
    n_parallel: int,
    cache_type: str,
) -> int:
    if n_layer <= 0 or n_embd <= 0 or n_ctx <= 0:
        return 0
    heads = n_head if n_head > 0 else 1
    kv_heads = n_head_kv if n_head_kv > 0 else heads
    np_n = n_parallel if n_parallel > 0 else 1
    n_embd_gqa = n_embd * kv_heads / heads
    return int(2 * n_layer * n_embd_gqa * n_ctx * kv_elem_bytes(cache_type) * np_n)


def parse_prom_metrics(text: str) -> Dict[str, float]:
    out: Dict[str, float] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, _, rest = line.partition(" ")
        if not rest:
            continue
        name = name.split("{", 1)[0]
        try:
            out[name] = float(rest.split()[0])
        except ValueError:
            continue
    return out


def prom_get(metrics: Dict[str, float], *names: str) -> Optional[float]:
    for name in names:
        if name in metrics:
            return metrics[name]
        alt = name.replace(":", "_")
        if alt in metrics:
            return metrics[alt]
    return None


def parse_ioreg_gpu(text: str) -> Dict[str, Any]:
    util = re.search(r'"Device Utilization %"\s*=\s*(\d+)', text)
    alloc = re.search(r'"Alloc system memory"\s*=\s*(\d+)', text)
    in_use = re.search(r'"In use system memory"\s*=\s*(\d+)', text)
    out: Dict[str, Any] = {
        "gpu_pct": int(util.group(1)) if util else None,
        "gpu_alloc": int(alloc.group(1)) if alloc else 0,
        "gpu_in_use": int(in_use.group(1)) if in_use else 0,
    }
    return out


def parse_vm_stat(text: str, page_size: int) -> int:
    fields: Dict[str, int] = {}
    for line in text.splitlines():
        if ":" not in line:
            continue
        key, _, raw = line.partition(":")
        digits = re.sub(r"[^0-9]", "", raw)
        if digits:
            fields[key.strip().lower()] = int(digits)
    if page_size <= 0:
        page_size = 4096
    used_pages = (
        fields.get("pages active", 0)
        + fields.get("pages wired down", 0)
        + fields.get("pages occupied by compressor", 0)
    )
    return used_pages * page_size


def parse_pmset_therm(text: str) -> str:
    low = text.lower()
    if "cpu_speed_limit" in low:
        m = re.search(r"cpu_speed_limit\s*=\s*(\d+)", low)
        if m and int(m.group(1)) < 100:
            return "throttle"
    if "thermal warning" in low and "no thermal warning" not in low:
        return "throttle"
    if "no thermal warning" in low or "no performance warning" in low:
        return "ok"
    if text.strip():
        return "ok"
    return "unknown"


def _http_get(url: str, timeout: float = 0.3) -> str:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return resp.read().decode("utf-8", "replace")
    except (urllib.error.URLError, TimeoutError, OSError, ValueError):
        return ""


def _http_json_any(url: str, timeout: float = 0.3) -> Any:
    raw = _http_get(url, timeout)
    if not raw:
        return None
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return None


def _http_json(url: str, timeout: float = 0.3) -> Dict[str, Any]:
    obj = _http_json_any(url, timeout)
    return obj if isinstance(obj, dict) else {}


def _cpu_pct_from_times(parts: List[int], idle_index: int, extra_idle: int = 0) -> int:
    global _cpu_prev
    now = time.time()
    if _cpu_prev is None:
        _cpu_prev = (now, list(parts))
        return 0
    prev = _cpu_prev[1]
    _cpu_prev = (now, list(parts))
    n = max(len(parts), len(prev))
    parts = list(parts) + [0] * (n - len(parts))
    prev = list(prev) + [0] * (n - len(prev))
    deltas = [max(0, a - b) for a, b in zip(parts, prev)]
    total = sum(deltas)
    if total <= 0:
        return 0
    idle = deltas[idle_index] if idle_index < len(deltas) else 0
    if extra_idle and extra_idle < len(deltas):
        idle += deltas[extra_idle]
    return max(0, min(100, int(round(100.0 * (total - idle) / total))))


def _sample_host_cpu() -> int:
    if platform.system() == "Darwin":
        raw = _run_capture(["sysctl", "-n", "kern.cp_time"], timeout=0.3)
        parts = [int(x) for x in raw.split() if x.isdigit()]
        if len(parts) >= 4:
            return _cpu_pct_from_times(parts, 3)
        return 0
    try:
        with open("/proc/stat", encoding="utf-8") as fh:
            line = fh.readline()
        parts = [int(x) for x in line.split()[1:] if x.isdigit()]
        if len(parts) >= 5:
            return _cpu_pct_from_times(parts, 3, 4)
    except (OSError, ValueError):
        pass
    return 0


def _sample_ram_used(total: int) -> int:
    if platform.system() == "Darwin":
        page = 16384
        raw_page = _run_capture(["sysctl", "-n", "hw.pagesize"], timeout=0.2)
        if raw_page.strip().isdigit():
            page = int(raw_page.strip())
        used = parse_vm_stat(_run_capture(["vm_stat"], timeout=0.4), page)
        if used > 0:
            return min(used, total) if total else used
    try:
        with open("/proc/meminfo", encoding="utf-8") as fh:
            info = {}
            for line in fh:
                key, _, rest = line.partition(":")
                digits = re.sub(r"[^0-9]", "", rest)
                if digits:
                    info[key] = int(digits) * 1024
        total_i = info.get("MemTotal") or total
        avail = info.get("MemAvailable")
        if total_i and avail is not None:
            return max(0, total_i - avail)
    except OSError:
        pass
    return 0


def _sample_gpu() -> Dict[str, Any]:
    empty = {"gpu_pct": None, "gpu_alloc": 0, "gpu_in_use": 0}
    if platform.system() != "Darwin":
        return empty
    try:
        proc = subprocess.Popen(
            ["ioreg", "-r", "-d", "1", "-c", "IOAccelerator", "-w", "0"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except OSError:
        return empty
    buf = b""
    try:
        assert proc.stdout is not None
        deadline = time.time() + 0.45
        while time.time() < deadline:
            chunk = proc.stdout.read(65536)
            if not chunk:
                break
            buf += chunk
            if b"PerformanceStatistics" in buf and b"Device Utilization" in buf:
                break
    except Exception:
        pass
    finally:
        try:
            if proc.poll() is None:
                proc.kill()
        except OSError:
            pass
        try:
            if proc.stdout:
                proc.stdout.close()
        except Exception:
            pass
        try:
            proc.wait(timeout=0.4)
        except Exception:
            pass
    return parse_ioreg_gpu(buf.decode("utf-8", "replace"))


def _ps_rss_cpu(pids: Dict[str, int]) -> Dict[int, Tuple[int, float]]:
    if not pids:
        return {}
    uniq = sorted({p for p in pids.values() if p > 0})
    if not uniq:
        return {}
    raw = _run_capture(
        ["ps", "-p", ",".join(str(p) for p in uniq), "-o", "pid=,rss=,pcpu="],
        timeout=0.4,
    )
    out: Dict[int, Tuple[int, float]] = {}
    for line in raw.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        try:
            pid = int(parts[0])
            rss = int(float(parts[1])) * 1024
            cpu = float(parts[2])
        except ValueError:
            continue
        out[pid] = (rss, cpu)
    return out


def _role_proc_info() -> Dict[str, Dict[str, Any]]:
    info: Dict[str, Dict[str, Any]] = {}
    with state_lock:
        for role, r in roles.items():
            proc = r.get("proc")
            alive = proc is not None and proc.poll() is None
            info[role] = {
                "pid": proc.pid if alive else 0,
                "path": r.get("path") or "",
                "backend_port": r.get("backend_port") or 0,
                "label": r.get("label") or "",
            }
    return info


def _draft_path_from_cmd(cmd: List[str]) -> str:
    if "-md" not in cmd:
        return ""
    try:
        return cmd[cmd.index("-md") + 1]
    except (ValueError, IndexError):
        return ""


def _n_ctx_from_props(props: Dict[str, Any], fallback: int) -> int:
    gen = props.get("default_generation_settings")
    if isinstance(gen, dict):
        if isinstance(gen.get("n_ctx"), int) and gen["n_ctx"] > 0:
            return int(gen["n_ctx"])
        params = gen.get("params")
        if isinstance(params, dict) and isinstance(params.get("n_ctx"), int) and params["n_ctx"] > 0:
            return int(params["n_ctx"])
    return fallback


def _positive_int(value: Any) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return 0
    n = int(value)
    return n if n > 0 else 0


def tokens_from_slot(slot: Any) -> int:
    if not isinstance(slot, dict):
        return 0
    past = _positive_int(slot.get("n_past"))
    if past:
        return past
    named = _positive_int(slot.get("n_prompt_tokens"))
    cache = _positive_int(slot.get("n_prompt_tokens_cache"))
    processed = _positive_int(slot.get("n_prompt_tokens_processed"))
    if named:
        prompt = named
    elif cache or processed:
        prompt = cache + processed
    else:
        prompt = 0
    nxt = slot.get("next_token") if isinstance(slot.get("next_token"), dict) else {}
    decoded = _positive_int(nxt.get("n_decoded")) or _positive_int(slot.get("n_decoded"))
    if prompt <= 0:
        return 0
    return prompt + decoded


def tokens_from_slots(slots: Any) -> int:
    if not isinstance(slots, list):
        return 0
    return max((tokens_from_slot(s) for s in slots), default=0)


def resolve_ctx_tokens(
    n_ctx: int,
    metrics: Dict[str, float],
    slots: Any = None,
    last_tokens: int = 0,
) -> Tuple[int, float, str]:
    """Live occupancy, else last turn, else process watermark. ratio in [0, 1]."""
    ctx = n_ctx if n_ctx > 0 else 0

    def ratio_for(n: int, given: Optional[float] = None) -> float:
        if given is not None:
            return max(0.0, min(1.0, float(given)))
        if ctx <= 0:
            return 0.0
        return max(0.0, min(1.0, float(n) / float(ctx)))

    kv_tok = prom_get(metrics, "llamacpp:kv_cache_tokens")
    kv_ratio = prom_get(metrics, "llamacpp:kv_cache_usage_ratio")
    if kv_tok is not None and float(kv_tok) > 0:
        n = int(kv_tok)
        return n, ratio_for(n, kv_ratio), "kv"
    if kv_ratio is not None and float(kv_ratio) > 0 and ctx:
        n = int(round(float(kv_ratio) * ctx))
        return n, ratio_for(n, kv_ratio), "kv"

    live = tokens_from_slots(slots)
    if live > 0:
        return live, ratio_for(live), "slot"

    last = _positive_int(last_tokens)
    if last > 0:
        return last, ratio_for(last), "last"

    watermark = prom_get(metrics, "llamacpp:n_tokens_max", "llamacpp:n_past_max")
    if watermark is not None and float(watermark) > 0:
        n = int(watermark)
        return n, ratio_for(n), "peak"
    return 0, 0.0, ""


def llm_view(
    path: str,
    n_ctx: int,
    n_parallel: int,
    cache_type: str,
    draft_path: str,
    metrics: Dict[str, float],
    rss: int,
    cpu_pct: float,
    slots: Any = None,
    last_tokens: int = 0,
) -> Dict[str, Any]:
    weights = _gguf_size(path)
    draft_sz = _gguf_size(draft_path) if draft_path else 0
    meta = read_gguf_meta(path) if path else {}
    kv_res = estimate_kv_bytes(
        int(meta.get("n_layer") or 0),
        int(meta.get("n_embd") or 0),
        int(meta.get("n_head") or 0),
        int(meta.get("n_head_kv") or 0),
        n_ctx,
        n_parallel,
        cache_type,
    )
    if kv_res <= 0 and weights > 0 and n_ctx > 0:
        kv_res = int(weights * 0.08 * (n_ctx / 32768.0) * (kv_elem_bytes(cache_type) / 2.0))
    n_tokens, ratio, ctx_src = resolve_ctx_tokens(n_ctx, metrics, slots, last_tokens)
    kv_used = int(kv_res * ratio) if kv_res else 0
    unified = weights + draft_sz + (kv_used if kv_used else kv_res)
    return {
        "weights": weights,
        "draft": draft_sz,
        "kv_reserved": kv_res,
        "kv_used": kv_used,
        "kv_ratio": ratio,
        "n_ctx": n_ctx,
        "n_tokens": n_tokens,
        "ctx_src": ctx_src,
        "rss": rss,
        "cpu_pct": cpu_pct,
        "unified": unified,
    }


def _empty_host() -> Dict[str, Any]:
    return {
        "ram_total": 0,
        "ram_used": 0,
        "ram_pct": 0,
        "cpu_pct": 0,
        "gpu_pct": None,
        "gpu_alloc": 0,
        "gpu_in_use": 0,
        "therm": "unknown",
        "llm": {},
        "embed_rss": 0,
    }


def collect_host_stats() -> Dict[str, Any]:
    host = _empty_host()
    total = total_ram_bytes()
    host["ram_total"] = total
    used = _sample_ram_used(total)
    host["ram_used"] = used
    host["ram_pct"] = int(round(100.0 * used / total)) if total > 0 else 0
    host["cpu_pct"] = _sample_host_cpu()
    host.update(_sample_gpu())
    if platform.system() == "Darwin":
        host["therm"] = parse_pmset_therm(_run_capture(["pmset", "-g", "therm"], timeout=0.4))
    roles_info = _role_proc_info()
    pids = {k: int(v["pid"]) for k, v in roles_info.items() if v.get("pid")}
    psmap = _ps_rss_cpu(pids)
    chat = roles_info.get("chat") or {}
    if chat.get("path"):
        pid = int(chat.get("pid") or 0)
        rss, cpu = psmap.get(pid, (0, 0.0))
        port = int(chat.get("backend_port") or 0)
        metrics: Dict[str, float] = {}
        props: Dict[str, Any] = {}
        slots: Any = []
        if port:
            metrics = parse_prom_metrics(_http_get(f"http://127.0.0.1:{port}/metrics"))
            props = _http_json(f"http://127.0.0.1:{port}/props")
            raw_slots = _http_json_any(f"http://127.0.0.1:{port}/slots")
            slots = raw_slots if isinstance(raw_slots, list) else []
        ctx_raw = opt_or_env("chat_ctx", "TUIDE_HOST_CHAT_CTX", str(CFG["chat_ctx"])) or "32768"
        ctx_n = int(ctx_raw) if str(ctx_raw).isdigit() else 32768
        n_ctx = _n_ctx_from_props(props, ctx_n)
        np_raw = chat_slot_count()
        np_n = int(np_raw) if np_raw.isdigit() else 1
        cmd = (last_perf.get("chat") or {}).get("cmd") or []
        draft = _draft_path_from_cmd(cmd) if isinstance(cmd, list) else ""
        last_tokens = int((spy.last_chat_ctx() or {}).get("n_tokens") or 0)
        host["llm"] = llm_view(
            str(chat["path"]),
            n_ctx,
            np_n,
            cache_type_k_v() or "f16",
            draft,
            metrics,
            rss,
            cpu,
            slots=slots,
            last_tokens=last_tokens,
        )
        host["llm"]["label"] = chat.get("label") or ""
    embed = roles_info.get("embed") or {}
    epid = int(embed.get("pid") or 0)
    if epid:
        host["embed_rss"] = psmap.get(epid, (0, 0.0))[0]
    return host


def _host_sampler_loop() -> None:
    while True:
        try:
            snap = collect_host_stats()
        except Exception:
            snap = _empty_host()
        with _host_lock:
            _host_latest.clear()
            _host_latest.update(snap)
        time.sleep(1.0)


def ensure_host_sampler() -> None:
    global _host_sampler_started
    with _host_lock:
        if _host_sampler_started:
            return
        _host_sampler_started = True
        if not _host_latest:
            _host_latest.update(_empty_host())
    threading.Thread(target=_host_sampler_loop, name="host-metrics", daemon=True).start()


def host_payload() -> Dict[str, Any]:
    ensure_host_sampler()
    with _host_lock:
        return dict(_host_latest) if _host_latest else _empty_host()


def _gguf_size(path: str) -> int:
    try:
        return Path(path).stat().st_size
    except OSError:
        return 0


def _name_is_small_chat(path: str) -> bool:
    name = Path(path).name.lower()
    return any(tag in name for tag in ("0.5b", "0_5b", "1.5b", "1_5b"))


def find_draft_gguf() -> str:
    explicit = opt_or_env("draft_gguf", "TUIDE_HOST_DRAFT_GGUF")
    if explicit:
        if Path(explicit).is_file():
            return explicit
        found = find_model(explicit)
        if found and shards_present(found):
            return str(model_path(found))
        return ""
    for entry in load_catalog_file().get("models") or []:
        if entry.get("id") == DRAFT_CATALOG_ID:
            path = model_path(entry)
            if path.is_file():
                return str(path)
    l1 = cache_dir() / "l1"
    if l1.is_dir():
        found = sorted(
            f for f in l1.glob("*.gguf")
            if "1.5b" in f.name.lower() or "1_5b" in f.name.lower()
        )
        if found:
            return str(found[0])
    return ""


def estimate_chat_plus_draft_bytes(chat_path: str, draft_path: str, ctx: int) -> int:
    chat_sz = _gguf_size(chat_path)
    draft_sz = _gguf_size(draft_path)
    ctx_n = ctx if ctx > 0 else 32768
    kv = int(chat_sz * 0.25 * (ctx_n / 32768.0))
    return chat_sz + draft_sz + kv + (3 * 1024 * 1024 * 1024)


def select_draft_path(chat_path: str, ctx: Optional[int] = None) -> str:
    mode = parse_on_off_auto(opt_or_env("draft", "TUIDE_HOST_DRAFT", "auto"), "auto")
    if mode == "off":
        return ""
    draft = find_draft_gguf()
    if not draft:
        return ""
    try:
        if Path(draft).resolve() == Path(chat_path).resolve():
            return ""
    except OSError:
        if draft == chat_path:
            return ""
    if _name_is_small_chat(chat_path):
        return ""
    if _gguf_size(chat_path) and _gguf_size(draft) and _gguf_size(chat_path) <= int(_gguf_size(draft) * 1.2):
        return ""
    if mode == "on":
        return draft
    ctx_n = int(ctx if ctx is not None else (opt_or_env("chat_ctx", "TUIDE_HOST_CHAT_CTX", CFG["chat_ctx"]) or "32768"))
    needed = estimate_chat_plus_draft_bytes(chat_path, draft, ctx_n)
    ram = total_ram_bytes()
    if ram > 0 and needed > int(ram * 0.75):
        return ""
    return draft


def flash_attn_mode() -> str:
    raw = opt_or_env("flash_attn", "TUIDE_HOST_FLASH_ATTN", "on")
    parsed = parse_on_off_auto(raw, "on")
    if parsed in ("on", "off", "auto"):
        return parsed
    return "on"


def cache_type_k_v() -> str:
    raw = opt_or_env("cache_type", "TUIDE_HOST_CACHE_TYPE", "q8_0").lower()
    if raw in ("", "0", "off", "false", "no", "none", "f16", "fp16"):
        return ""
    return raw


def chat_slot_count() -> str:
    raw = opt_or_env("np", "TUIDE_HOST_NP", "1")
    return raw if raw else "1"


def draft_n_max() -> str:
    raw = opt_or_env("draft_n_max", "TUIDE_HOST_DRAFT_N_MAX", "16")
    return raw if raw else "16"


def embed_ngl() -> str:
    return opt_or_env("embed_ngl", "TUIDE_HOST_EMBED_NGL", "0") or "0"


def model_supports_thinking(path: str) -> bool:
    name = Path(path or "").name.lower()
    return any(marker in name for marker in THINKING_MARKERS)


def thinking_wanted() -> bool:
    return parse_on_off_auto(opt_or_env("thinking", "TUIDE_HOST_THINKING", "on"), "on") != "off"


_llama_help: Dict[str, str] = {}


def llama_server_help(server: str) -> str:
    if server in _llama_help:
        return _llama_help[server]
    text = ""
    if server and Path(server).is_file():
        try:
            text = subprocess.check_output(
                [server, "--help"],
                text=True,
                stderr=subprocess.STDOUT,
                timeout=8,
            )
        except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
            text = ""
    _llama_help[server] = text
    return text


def thinking_llama_argv(server: str, model: str) -> List[str]:
    """Jinja + reasoning split for CoT models; no-op for Llama / Qwen2.5 / etc."""
    if not model_supports_thinking(model):
        return []
    on = thinking_wanted()
    help_text = llama_server_help(server)

    def has(flag: str) -> bool:
        return True if not help_text else flag in help_text

    flags: List[str] = []
    if has("--jinja"):
        flags.append("--jinja")
    if has("--reasoning-format"):
        flags += ["--reasoning-format", "deepseek"]
    if has("--chat-template-kwargs"):
        flags += [
            "--chat-template-kwargs",
            json.dumps({"enable_thinking": on}, separators=(",", ":")),
        ]
    if (not on) and has("--reasoning-budget"):
        flags += ["--reasoning-budget", "0"]
    return flags


def chat_llama_argv(server: str, model: str, host: str, port: int, alias: str) -> List[str]:
    threads = str(performance_core_count())
    ngl = opt_or_env("ngl", "TUIDE_HOST_NGL", str(CFG["ngl"])) or str(CFG["ngl"])
    ctx = opt_or_env("chat_ctx", "TUIDE_HOST_CHAT_CTX", str(CFG["chat_ctx"])) or str(CFG["chat_ctx"])
    cmd = [
        server, "-m", model, "--host", host, "--port", str(port),
        "-ngl", ngl,
        "-c", ctx,
        "--alias", alias,
        "-np", chat_slot_count(),
        "-t", threads,
        "-tb", threads,
        "-fa", flash_attn_mode(),
        "--metrics",
    ]
    cmd += thinking_llama_argv(server, model)
    ctk = cache_type_k_v()
    if ctk:
        cmd += ["-ctk", ctk, "-ctv", ctk]
    draft = select_draft_path(model, int(ctx) if str(ctx).isdigit() else None)
    if draft:
        cmd += [
            "-md", draft,
            "-ngld", ngl,
            "--spec-draft-n-max", draft_n_max(),
        ]
        if ctk:
            cmd += ["-ctkd", ctk, "-ctvd", ctk]
    return cmd


def embed_llama_argv(server: str, model: str, host: str, port: int) -> List[str]:
    ctx = opt_or_env("embed_ctx", "TUIDE_HOST_EMBED_CTX", str(CFG["embed_ctx"])) or str(CFG["embed_ctx"])
    np_slots = opt_or_env("embed_np", "TUIDE_HOST_EMBED_NP", str(CFG["embed_np"])) or str(CFG["embed_np"])
    return [
        server, "-m", model, "--host", host, "--port", str(port),
        "-ngl", embed_ngl(),
        "--embedding", "--pooling", "mean",
        "-c", ctx,
        "-np", np_slots,
        "--metrics",
    ]


def perf_summary(role: str = "chat") -> str:
    snap = last_perf.get(role) or {}
    if snap.get("summary"):
        return str(snap["summary"])
    bits = [
        f"fa={flash_attn_mode()}",
        f"kv={cache_type_k_v() or 'f16'}",
        f"t={performance_core_count()}",
        f"np={chat_slot_count()}",
    ]
    draft = find_draft_gguf()
    bits.append("draft=" + (Path(draft).name if draft else "off"))
    bits.append("think=" + ("on" if thinking_wanted() else "off"))
    if role == "embed":
        return f"ngl={embed_ngl()}"
    return " ".join(bits)


def _record_perf(role: str, cmd: List[str]) -> None:
    if role == "chat":
        draft = ""
        if "-md" in cmd:
            try:
                draft = Path(cmd[cmd.index("-md") + 1]).name
            except (ValueError, IndexError):
                draft = "on"
        last_perf[role] = {
            "cmd": cmd,
            "summary": (
                f"fa={flash_attn_mode()} kv={cache_type_k_v() or 'f16'} "
                f"t={performance_core_count()} np={chat_slot_count()} "
                f"draft={draft or 'off'} think={'on' if thinking_wanted() else 'off'}"
            ),
        }
        return
    last_perf[role] = {"cmd": cmd, "summary": f"ngl={embed_ngl()}"}


def default_launch_opts() -> Dict[str, str]:
    threads_env = env_trimmed("TUIDE_HOST_THREADS")
    threads = threads_env if threads_env.isdigit() and int(threads_env) > 0 else str(detect_performance_cores())
    return {
        "flash_attn": env_trimmed("TUIDE_HOST_FLASH_ATTN", "on") or "on",
        "cache_type": env_trimmed("TUIDE_HOST_CACHE_TYPE", "q8_0") or "q8_0",
        "threads": threads,
        "np": env_trimmed("TUIDE_HOST_NP", "1") or "1",
        "ngl": env_trimmed("TUIDE_HOST_NGL", str(CFG["ngl"])) or str(CFG["ngl"]),
        "chat_ctx": env_trimmed("TUIDE_HOST_CHAT_CTX", str(CFG["chat_ctx"])) or str(CFG["chat_ctx"]),
        "embed_ngl": env_trimmed("TUIDE_HOST_EMBED_NGL", "0") or "0",
        "embed_ctx": env_trimmed("TUIDE_HOST_EMBED_CTX", str(CFG["embed_ctx"])) or str(CFG["embed_ctx"]),
        "embed_np": env_trimmed("TUIDE_HOST_EMBED_NP", str(CFG["embed_np"])) or str(CFG["embed_np"]),
        "draft": env_trimmed("TUIDE_HOST_DRAFT", "auto") or "auto",
        "draft_n_max": env_trimmed("TUIDE_HOST_DRAFT_N_MAX", "16") or "16",
        "draft_gguf": env_trimmed("TUIDE_HOST_DRAFT_GGUF"),
        "thinking": env_trimmed("TUIDE_HOST_THINKING", "on") or "on",
    }


def effective_launch_opts() -> Dict[str, str]:
    out = default_launch_opts()
    with launch_lock:
        for key, val in launch_opts.items():
            out[key] = str(val)
    return out


def _parse_int_opt(raw: str, lo: int, hi: int, name: str) -> "tuple[int, str]":
    try:
        n = int(str(raw).strip())
    except (TypeError, ValueError):
        return 0, f"{name} inválido"
    if n < lo or n > hi:
        return 0, f"{name} fuera de rango ({lo}–{hi})"
    return n, ""


def apply_launch_opts(obj: Optional[Dict[str, Any]]) -> str:
    if not obj:
        return ""
    updates: Dict[str, str] = {}
    if "flash_attn" in obj:
        fa = parse_on_off_auto(str(obj.get("flash_attn") or ""), "on")
        if fa not in ("on", "off", "auto"):
            return "flash_attn debe ser on, off o auto"
        updates["flash_attn"] = fa
    if "cache_type" in obj:
        ctk = str(obj.get("cache_type") or "").strip().lower()
        allowed = ("q8_0", "f16", "fp16", "q4_0", "q4_1", "q5_0", "q5_1", "bf16", "iq4_nl", "off", "0")
        if ctk not in allowed:
            return "cache_type no reconocido"
        updates["cache_type"] = ctk
    ranges = (
        ("threads", 1, 256),
        ("np", 1, 32),
        ("ngl", 0, 999),
        ("chat_ctx", 512, 131072),
        ("embed_ngl", 0, 999),
        ("embed_ctx", 256, 8192),
        ("embed_np", 1, 64),
        ("draft_n_max", 1, 32),
    )
    for key, lo, hi in ranges:
        if key not in obj:
            continue
        n, err = _parse_int_opt(str(obj.get(key) or ""), lo, hi, key)
        if err:
            return err
        updates[key] = str(n)
    if "draft" in obj:
        mode = parse_on_off_auto(str(obj.get("draft") or ""), "auto")
        if mode not in ("on", "off", "auto"):
            return "draft debe ser auto, on u off"
        updates["draft"] = mode
    if "draft_gguf" in obj:
        raw = str(obj.get("draft_gguf") or "").strip()
        if raw:
            if Path(raw).is_file():
                updates["draft_gguf"] = raw
            else:
                found = find_model(raw)
                if found and shards_present(found):
                    updates["draft_gguf"] = str(model_path(found))
                else:
                    return "draft GGUF no encontrado"
        else:
            updates["draft_gguf"] = ""
    if "thinking" in obj:
        mode = parse_on_off_auto(str(obj.get("thinking") or ""), "on")
        if mode not in ("on", "off"):
            return "thinking debe ser on u off"
        updates["thinking"] = mode
    if not updates:
        return ""
    with launch_lock:
        launch_opts.update(updates)
    return ""


def draft_choices() -> List[Dict[str, str]]:
    items: List[Dict[str, str]] = []
    seen = set()
    for m in all_models():
        if (m.get("role") or "chat") != "chat":
            continue
        if not shards_present(m):
            continue
        path = str(model_path(m))
        if path in seen:
            continue
        seen.add(path)
        items.append({
            "id": str(m.get("id") or ""),
            "label": str(m.get("label") or Path(path).name),
            "path": path,
        })
    return items


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
        cmd = [
            "curl", "-fL", "-C", "-",
            "-A", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36",
            "--retry", "3", "--connect-timeout", "20", "-o", str(tmp), url,
        ]
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
    if role == "chat":
        cmd = chat_llama_argv(server, str(path), bind_host, backend_port, alias)
    else:
        cmd = embed_llama_argv(server, str(path), bind_host, backend_port)
    _record_perf(role, cmd)
    log(f"{role} llama-server {' '.join(cmd[1:])}")
    if role == "chat" and "-md" not in cmd:
        mode = parse_on_off_auto(opt_or_env("draft", "TUIDE_HOST_DRAFT", "auto"), "auto")
        if mode != "off":
            if not find_draft_gguf():
                log("chat sin draft: descarga Qwen2.5 Instruct 1.5B (L1) para speculative decoding")
            else:
                log("chat sin draft: modelo pequeño, mismo GGUF o RAM justa (TUIDE_HOST_DRAFT=1 fuerza)")
    if role == "chat" and model_supports_thinking(str(path)):
        log(
            "chat thinking "
            + ("on (CoT en Inspección)" if thinking_wanted() else "off")
        )
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
        if role == "chat" and model_supports_thinking(str(path)):
            spy_cmd += ["--thinking", "on" if thinking_wanted() else "off"]
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
    order = ("70b", "32b", "14b", "7b", "3b", "1.5b")
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
        "thread": spy.chat_thread_status(),
        "embed": embed,
        "advertise": adv,
        "chat_port": CFG["chat_port"],
        "embed_port": CFG["embed_port"],
        "web_port": CFG["web_port"],
        "runtime": runtime_payload(),
        "perf": {
            "chat": (last_perf.get("chat") or {}).get("summary") or perf_summary("chat"),
            "embed": (last_perf.get("embed") or {}).get("summary") or f"ngl={embed_ngl()}",
        },
        "launch": effective_launch_opts(),
        "launch_defaults": default_launch_opts(),
        "drafts": draft_choices(),
        "host": host_payload(),
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
body.webapp #chrome { padding-left:86px; -webkit-app-region:drag; }
body.webapp #chrome button, body.webapp .chip { -webkit-app-region:no-drag; }
#chrome h1 { margin:0; font-size:13px; font-weight:600; letter-spacing:.04em; }
#modes { display:flex; gap:6px; }
#chrome-metrics { display:flex; gap:5px; margin-left:auto; align-items:center; min-width:0;
  overflow:hidden; }
.chip { font-size:11px; font-variant-numeric:tabular-nums; padding:2px 7px; border:1px solid var(--line);
  border-radius:4px; color:var(--muted); white-space:nowrap; }
.chip.warn { color:var(--warn); border-color:#6a5a20; }
.chip.hot { color:var(--err); border-color:#5a3535; }
#chrome-status { color:var(--muted); font-size:11px; white-space:nowrap;
  overflow:hidden; text-overflow:ellipsis; max-width:22vw; flex-shrink:0; }
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
#perf-grid { display:grid; grid-template-columns:repeat(auto-fill, minmax(168px, 1fr));
  gap:10px 14px; margin:8px 0 4px; }
#perf-grid label { display:flex; flex-direction:column; gap:4px; color:var(--muted); font-size:11px; }
#perf-grid label > span { letter-spacing:.04em; text-transform:uppercase; }
#perf-grid input, #perf-grid select { width:100%; min-width:0; }
#perf-help { color:var(--muted); font-size:11px; margin:0 0 8px; }
#perf-help code { color:var(--txt); }
.check-row { display:flex; align-items:flex-start; gap:10px; margin:12px 0 8px;
  color:var(--muted); font-size:12px; max-width:52rem; }
.check-row input[type=checkbox] { width:auto; margin-top:3px; flex-shrink:0; }
.check-row .check-copy { display:flex; flex-direction:column; gap:3px; }
.check-row .check-copy strong { color:var(--txt); font-weight:600; letter-spacing:.04em;
  text-transform:uppercase; font-size:11px; }
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
  <div id="chrome-metrics"></div>
  <div id="chrome-status">arrancando…</div>
</div>
<div id="views">
  <div id="view-launch" class="view on">
    <div class="row" style="margin-bottom:8px">
      <span id="rt-line">runtime…</span>
      <button type="button" id="rt-install" class="ok">Instalar llama-server</button>
    </div>
    <div id="rt-msg"></div>
    <h2>Rendimiento</h2>
    <p id="perf-help">Se aplican al <b>Lanzar</b> o <b>Reiniciar</b>. Por defecto: flash-attn, KV q8_0, 1 slot, embeddings en CPU, draft 1.5B automático, pensamiento en vivo en Qwen3/R1.</p>
    <div id="perf-grid">
      <label><span>Flash attention</span>
        <select id="opt-fa">
          <option value="on" selected>on</option>
          <option value="auto">auto</option>
          <option value="off">off</option>
        </select></label>
      <label><span>KV cache</span>
        <select id="opt-ctk">
          <option value="q8_0" selected>q8_0</option>
          <option value="f16">f16</option>
          <option value="q4_0">q4_0</option>
          <option value="q5_0">q5_0</option>
          <option value="bf16">bf16</option>
          <option value="off">off (f16)</option>
        </select></label>
      <label><span>Hilos (P-cores)</span>
        <input id="opt-threads" type="number" min="1" max="256" value="8"></label>
      <label><span>Slots (-np)</span>
        <input id="opt-np" type="number" min="1" max="32" value="1"></label>
      <label><span>GPU layers chat</span>
        <input id="opt-ngl" type="number" min="0" max="999" value="99"></label>
      <label><span>Contexto chat</span>
        <input id="opt-ctx" type="number" min="512" max="131072" step="512" value="32768"></label>
      <label><span>GPU layers embed</span>
        <input id="opt-embed-ngl" type="number" min="0" max="999" value="0"></label>
      <label><span>Draft</span>
        <select id="opt-draft">
          <option value="auto" selected>auto</option>
          <option value="on">on</option>
          <option value="off">off</option>
        </select></label>
      <label><span>Draft tokens</span>
        <input id="opt-draft-n" type="number" min="1" max="32" value="16"></label>
      <label><span>Draft GGUF</span>
        <select id="opt-draft-gguf">
          <option value="">automático (1.5B L1)</option>
        </select></label>
    </div>
    <label class="check-row" for="opt-thinking">
      <input id="opt-thinking" type="checkbox" checked>
      <span class="check-copy">
        <strong>Pensamiento en vivo</strong>
        Muestra la cadena de pensamiento en Inspección (Qwen3, DeepSeek-R1, gpt-oss, Magistral, GLM-4.5…). En Llama 70B / Qwen2.5 no aplica. Desactívalo para responder antes, sin CoT.
      </span>
    </label>
    <div class="row" style="margin-bottom:12px">
      <button type="button" id="opt-reset">Restaurar defaults</button>
    </div>
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
let optsReady = false;
let launchDefaults = null;
let lastDrafts = [];
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
  return String(s || "").replace(/[&<>"]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
}
function fmtG(n) {
  n = Number(n) || 0;
  if (n >= 1073741824) return (n / 1073741824).toFixed(1) + "G";
  if (n >= 1048576) return Math.round(n / 1048576) + "M";
  if (n >= 1024) return Math.round(n / 1024) + "K";
  return n + "B";
}
function fmtTok(n) {
  n = Number(n) || 0;
  if (n >= 10000) return Math.round(n / 1000) + "k";
  if (n >= 1000) return (n / 1000).toFixed(1) + "k";
  return String(n);
}
function chip(text, title, cls) {
  return `<span class="chip${cls ? " " + cls : ""}" title="${esc(title)}">${esc(text)}</span>`;
}
function renderMetrics(h) {
  const el = document.getElementById("chrome-metrics");
  if (!el) return;
  if (!h) { el.innerHTML = ""; return; }
  const chips = [];
  if (h.ram_total) {
    const cls = (h.ram_pct || 0) >= 85 ? "warn" : "";
    chips.push(chip(
      "RAM " + fmtG(h.ram_used) + "/" + fmtG(h.ram_total),
      (h.ram_pct || 0) + "% · unificada (pesos+KV+macOS)",
      cls
    ));
  }
  const llm = h.llm || {};
  if (llm.weights) {
    const committed = (llm.weights || 0) + (llm.draft || 0) + (llm.kv_reserved || 0);
    const ratio = llm.kv_ratio || 0;
    const cls = ratio >= 0.8 ? "warn" : "";
    let title = "pesos " + fmtG(llm.weights);
    if (llm.draft) title += " · draft " + fmtG(llm.draft);
    title += " · KV " + fmtG(llm.kv_used) + "/" + fmtG(llm.kv_reserved);
    if (llm.rss) title += " · RSS " + fmtG(llm.rss);
    if (h.embed_rss) title += " · embed " + fmtG(h.embed_rss);
    chips.push(chip("LLM " + fmtG(committed), title, cls));
    if (llm.n_ctx) {
      const srcMap = {kv: "KV live", slot: "slot en curso", last: "último turno", peak: "máx. desde arranque"};
      const src = srcMap[llm.ctx_src] || "";
      chips.push(chip(
        "ctx " + fmtTok(llm.n_tokens) + "/" + fmtTok(llm.n_ctx),
        (src ? src + " · " : "") + Math.round(ratio * 100) + "% · " + (llm.n_tokens || 0) + "/" + llm.n_ctx + " tokens",
        cls
      ));
    }
  }
  if (h.gpu_pct != null) {
    const gTitle = "util " + h.gpu_pct + "%"
      + (h.gpu_alloc ? " · GPU alloc " + fmtG(h.gpu_alloc) : "")
      + (h.gpu_in_use ? " · in use " + fmtG(h.gpu_in_use) : "");
    chips.push(chip("GPU " + h.gpu_pct + "%", gTitle, h.gpu_pct >= 90 ? "warn" : ""));
  }
  chips.push(chip("CPU " + (h.cpu_pct || 0) + "%", "carga del host (todos los núcleos)", (h.cpu_pct || 0) >= 90 ? "warn" : ""));
  const therm = h.therm || "unknown";
  chips.push(chip(
    therm === "throttle" ? "therm throttle" : (therm === "ok" ? "therm ok" : "therm —"),
    "presión térmica (sin °C; hace falta sudo powermetrics)",
    therm === "throttle" ? "hot" : ""
  ));
  el.innerHTML = chips.join("");
}
function readLaunch() {
  return {
    flash_attn: document.getElementById("opt-fa").value,
    cache_type: document.getElementById("opt-ctk").value,
    threads: document.getElementById("opt-threads").value,
    np: document.getElementById("opt-np").value,
    ngl: document.getElementById("opt-ngl").value,
    chat_ctx: document.getElementById("opt-ctx").value,
    embed_ngl: document.getElementById("opt-embed-ngl").value,
    draft: document.getElementById("opt-draft").value,
    draft_n_max: document.getElementById("opt-draft-n").value,
    draft_gguf: document.getElementById("opt-draft-gguf").value,
    thinking: document.getElementById("opt-thinking").checked ? "on" : "off",
  };
}
function fillSelect(id, value, allowed) {
  const el = document.getElementById(id);
  if (!el) return;
  if (value && allowed && !allowed.includes(value)) {
    const opt = document.createElement("option");
    opt.value = value;
    opt.textContent = value;
    el.appendChild(opt);
  }
  if (value) el.value = value;
}
function syncDraftSelect(drafts, keep) {
  const sel = document.getElementById("opt-draft-gguf");
  const cur = keep !== undefined ? keep : sel.value;
  const rows = [["", "automático (1.5B L1)"]].concat(
    (drafts || []).map(d => [d.path, (d.label || d.path) + (d.path ? "  ·  " + d.path.split("/").pop() : "")])
  );
  const same = sel.options.length === rows.length &&
    [...sel.options].every((o, i) => o.value === rows[i][0]);
  if (!same) {
    sel.innerHTML = rows.map(([v, l]) => `<option value="${esc(v)}">${esc(l)}</option>`).join("");
  }
  if ([...sel.options].some(o => o.value === cur)) sel.value = cur;
}
function applyLaunchForm(o, drafts) {
  if (!o) return;
  fillSelect("opt-fa", o.flash_attn, ["on", "off", "auto"]);
  fillSelect("opt-ctk", o.cache_type, ["q8_0", "f16", "q4_0", "q5_0", "bf16", "off"]);
  document.getElementById("opt-threads").value = o.threads || "";
  document.getElementById("opt-np").value = o.np || "1";
  document.getElementById("opt-ngl").value = o.ngl || "99";
  document.getElementById("opt-ctx").value = o.chat_ctx || "32768";
  document.getElementById("opt-embed-ngl").value = o.embed_ngl || "0";
  fillSelect("opt-draft", o.draft, ["auto", "on", "off"]);
  document.getElementById("opt-draft-n").value = o.draft_n_max || "16";
  syncDraftSelect(drafts == null ? lastDrafts : drafts, o.draft_gguf || "");
  document.getElementById("opt-thinking").checked = (o.thinking || "on") !== "off";
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
      const body = {id: btn.dataset.id, role: btn.dataset.role, launch: readLaunch()};
      try {
        if (act === "download") await api("/api/download", {method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify({id: body.id, role: body.role})});
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
    const perf = st.perf || {};
    if (st.launch_defaults) launchDefaults = st.launch_defaults;
    if (st.drafts) lastDrafts = st.drafts;
    if (!optsReady && st.launch) {
      applyLaunchForm(st.launch, lastDrafts);
      optsReady = true;
    } else {
      syncDraftSelect(lastDrafts);
    }
    document.getElementById("rt-line").textContent = rt.found
      ? ("llama-server: " + rt.llama_server + (perf.chat ? " · " + perf.chat : ""))
      : "llama-server no encontrado";
    document.getElementById("rt-install").style.display = rt.found ? "none" : "inline-block";
    const ch = st.chat && st.chat.running ? (st.chat.label || "chat") : "chat off";
    const em = st.embed && st.embed.running ? (st.embed.label || "embed") : "embed off";
    document.getElementById("chrome-status").textContent = ch + " · " + em;
    renderMetrics(st.host);
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
document.getElementById("opt-reset").onclick = () => {
  const d = launchDefaults || {
    flash_attn: "on", cache_type: "q8_0", threads: "8", np: "1", ngl: "99",
    chat_ctx: "32768", embed_ngl: "0", draft: "auto", draft_n_max: "16", draft_gguf: "",
    thinking: "on"
  };
  applyLaunchForm(d, lastDrafts);
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
if (/tuide-host-webapp/.test(navigator.userAgent || "")) document.body.classList.add("webapp");
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
            err = apply_launch_opts(obj.get("launch") if isinstance(obj.get("launch"), dict) else None)
            if err:
                self._json(400, {"ok": False, "error": err})
                return
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
            err = apply_launch_opts(obj.get("launch") if isinstance(obj.get("launch"), dict) else None)
            if err:
                self._json(400, {"ok": False, "error": err})
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
        if path == "/api/history/clear":
            self._json(200, spy.clear_history())
            return
        if path == "/api/ask/stop":
            chat = role_snapshot("chat")
            backend_port = int(chat.get("backend_port") or (CFG["chat_port"] + 10000))
            self._json(200, spy.request_stop_generation("127.0.0.1", backend_port))
            return
        if path == "/api/ask":
            chat = role_snapshot("chat")
            if not chat.get("running"):
                self._json(503, {"ok": False, "error": "no hay LLM en este visor"})
                return
            backend_port = int(chat.get("backend_port") or (CFG["chat_port"] + 10000))
            try:
                code, payload = spy.handle_ask_post(obj, "127.0.0.1", backend_port)
            except (TimeoutError, socket.timeout) as ex:
                self._json(504, {"ok": False, "error": f"timeout: {ex}"})
                return
            except OSError as ex:
                self._json(502, {"ok": False, "error": str(ex)})
                return
            self._json(code, payload)
            return
        self._send(404, b"not found\n", "text/plain")


class HubServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def cleanup() -> None:
    stop_role("chat")
    stop_role("embed")


WEBAPP_SRC = TOOLS_DIR / "host_llama_webapp.swift"


def host_webapp_bin() -> Path:
    return cache_dir() / "runtime" / "tuide-host-webapp"


def chrome_macos_bin() -> str:
    for path in (
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        str(Path.home() / "Applications/Google Chrome.app/Contents/MacOS/Google Chrome"),
    ):
        if Path(path).is_file():
            return path
    return ""


def ensure_host_webapp() -> str:
    if platform.system() != "Darwin":
        return ""
    src = WEBAPP_SRC
    if not src.is_file():
        return ""
    dest = host_webapp_bin()
    try:
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.is_file() and dest.stat().st_mtime >= src.stat().st_mtime:
            return str(dest)
        swiftc = shutil.which("swiftc") or "/usr/bin/swiftc"
        if not Path(swiftc).is_file():
            return ""
        log("compilando ventana WebKit (sin chrome de Safari)…")
        subprocess.check_call(
            [swiftc, "-O", "-o", str(dest), str(src)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=90,
        )
        return str(dest) if dest.is_file() else ""
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return ""


def browser_launch_argv(url: str) -> List[str]:
    mode = os.environ.get("TUIDE_HOST_BROWSER", "app").strip().lower() or "app"
    if platform.system() != "Darwin":
        return ["xdg-open", url]
    if mode in ("safari",):
        return ["open", "-a", "Safari", url]
    if mode in ("system", "default"):
        return ["open", url]
    if mode in ("app", "webapp", "webkit", "chrome"):
        if mode != "chrome":
            webapp = ensure_host_webapp()
            if webapp:
                return [webapp, url]
        chrome = chrome_macos_bin()
        if chrome:
            return [chrome, f"--app={url}"]
        if mode == "chrome":
            return ["open", "-na", "Google Chrome", "--args", f"--app={url}"]
    return ["open", url]


def open_browser(url: str) -> None:
    cmd = browser_launch_argv(url)
    try:
        subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
    ensure_host_sampler()

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
    mode = args.mode
    url = f"http://{host}:{port_s}/#{mode}"
    try:
        httpd = HubServer((host, int(port_s)), HubHandler)
    except OSError as ex:
        if getattr(ex, "errno", None) not in (errno.EADDRINUSE, 48):
            raise
        log(f"puerto {port_s} ocupado: el hub ya está en {url}")
        log("para recargar: ./tools/run_host_llama.sh --stop")
        log(f"o cierra esa instancia (lsof -nP -iTCP:{port_s} -sTCP:LISTEN)")
        if args.open_browser:
            open_browser(url)
        return 0
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
