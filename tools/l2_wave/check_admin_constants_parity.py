#!/usr/bin/env python3
"""P7 (docs/plans/l2-admin-verify-round-reduction.md): fail loudly when the
Python admin_v1 harness's hardcoded constants drift from the C++ source of
truth (src/ai/l2_admin.hpp / l2_admin.cpp) -- instead of discovering it only
after the fact via a "matches C++ admin verify" commit, as happened with
confirmar_cerrar / forbid_falta_nada (see docs/plans/l2-admin-verify-round-reduction.md, sección 0bis).

Pure static regex over source text: no build, no import, safe to run in CI
as a cheap pre-check before any battery/canary run. Exit 0 = all matched,
exit 1 = at least one drift found (printed to stderr).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def cpp_const(name: str, text: str) -> int:
    m = re.search(rf"inline constexpr int {re.escape(name)}\s*=\s*(-?\d+)", text)
    if not m:
        raise SystemExit(f"parity: no encontré `{name}` en l2_admin.hpp")
    return int(m.group(1))


def py_kwarg_default(fn_label: str, kwarg: str, text: str) -> int:
    m = re.search(rf"{re.escape(kwarg)}\s*:\s*int\s*=\s*(-?\d+)", text)
    if not m:
        raise SystemExit(f"parity: no encontré el default de `{kwarg}` en {fn_label}")
    return int(m.group(1))


def py_call_kwarg_near(text: str, anchor: str, kwarg: str, window: int = 8000) -> int:
    idx = text.find(anchor)
    if idx == -1:
        raise SystemExit(f"parity: no encontré el ancla {anchor!r}")
    chunk = text[idx : idx + window]
    m = re.search(rf"{re.escape(kwarg)}\s*=\s*(-?\d+)", chunk)
    if not m:
        raise SystemExit(f"parity: no encontré `{kwarg}=` cerca de {anchor!r}")
    return int(m.group(1))


def py_argparse_default(text: str, flag: str) -> int:
    m = re.search(rf'add_argument\(\s*"{re.escape(flag)}"[^)]*default=(-?\d+)', text)
    if not m:
        raise SystemExit(f"parity: no encontré el default de argparse `{flag}`")
    return int(m.group(1))


def falta_nada_set(text: str, label: str) -> set[str]:
    m = re.search(r"_FALTA_NADA\s*=\s*frozenset\(\s*\{([^}]*)\}", text, re.S)
    if not m:
        raise SystemExit(f"parity: no encontré _FALTA_NADA en {label}")
    return set(re.findall(r'"([^"]*)"', m.group(1)))


def cpp_knada_set(text: str) -> set[str]:
    m = re.search(r"kNada\s*=\s*\{([^}]*)\}", text, re.S)
    if not m:
        raise SystemExit("parity: no encontré kNada en l2_admin.cpp")
    return set(re.findall(r'"([^"]*)"', m.group(1)))


def main() -> int:
    hpp = read("src/ai/l2_admin.hpp")
    cpp = read("src/ai/l2_admin.cpp")
    pilot = read("tools/l2_wave/probe_admin_pilot.py")
    battery = read("tools/l2_wave/run_admin_explore_battery.py")
    metrics = read("tools/l2_wave/admin_session_metrics.py")

    int_checks = [
        (
            "kAdminMaxVerifyPasses vs probe_admin_pilot.run_exit_verify(max_verify=)",
            cpp_const("kAdminMaxVerifyPasses", hpp),
            py_kwarg_default("run_exit_verify", "max_verify", pilot),
        ),
        (
            "kAdminVerifyMaxSteps vs run_exit_verify's run_verifier(max_steps=) call",
            cpp_const("kAdminVerifyMaxSteps", hpp),
            py_call_kwarg_near(pilot, "def run_exit_verify", "max_steps"),
        ),
        (
            "kAdminMaxExplores vs run_admin_explore_battery.py --max-explore default",
            cpp_const("kAdminMaxExplores", hpp),
            py_argparse_default(battery, "--max-explore"),
        ),
        (
            "kAdminMaxExplores vs probe_admin_pilot.py --max-explore default",
            cpp_const("kAdminMaxExplores", hpp),
            py_argparse_default(pilot, "--max-explore"),
        ),
    ]

    failed: list[tuple[str, object, object]] = []
    for label, cv, pv in int_checks:
        status = "OK" if cv == pv else "MISMATCH"
        print(f"[{status}] {label}: C++={cv} py={pv}")
        if cv != pv:
            failed.append((label, cv, pv))

    knada = cpp_knada_set(cpp)
    for label, text in (
        ("admin_session_metrics._FALTA_NADA", metrics),
        ("probe_admin_pilot._FALTA_NADA", pilot),
    ):
        s = falta_nada_set(text, label)
        status = "OK" if s == knada else "MISMATCH"
        print(f"[{status}] kNada (C++, falta_is_nada) vs {label}")
        if s != knada:
            print(f"    solo en C++: {sorted(knada - s)}")
            print(f"    solo en {label}: {sorted(s - knada)}")
            failed.append((label, knada, s))

    if failed:
        print(f"\nparity: {len(failed)} divergencia(s) — ver arriba.", file=sys.stderr)
        return 1
    print("\nparity: OK, C++ y el harness Python coinciden en los valores chequeados.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
