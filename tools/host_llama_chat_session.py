#!/usr/bin/env python3
"""Rolling chat memory for the spy compose (SummaryBuffer).

Keeps a raw window of recent turns plus an accumulating summary. When the
window overflows, older turns fold into the summary (LLM or extractive).
Each re-summary is allowed to fade the start of the conversation.
"""
from __future__ import annotations

from typing import Dict, Iterable, List, Mapping, Sequence, Tuple

# Conservative vs n_ctx 32k (~4 chars/token) so generation still has room.
RECENT_MAX_CHARS = 12000
RECENT_MAX_TURNS = 8
SUMMARY_MAX_CHARS = 4000
TURN_EXCERPT_CHARS = 240

MEMORY_PREFIX = (
    "Memoria de la conversación (resumen acumulado; el inicio puede estar comprimido):\n"
)

SUMMARY_SYSTEM = (
    "Eres un compactador de memoria de conversación. "
    "Fusiona el resumen previo con los turnos que salen de la ventana reciente. "
    "Conserva objetivos, decisiones, nombres de archivo/símbolos y lo pendiente. "
    "El inicio de la charla puede quedar en 2–4 frases. "
    "No inventes. Sin markdown salvo paths ya citados. Español."
)

Message = Dict[str, str]


def _content(msg: Mapping[str, str]) -> str:
    return str(msg.get("content") or "")


def _role(msg: Mapping[str, str]) -> str:
    return str(msg.get("role") or "user")


def recent_chars(recent: Sequence[Mapping[str, str]]) -> int:
    return sum(len(_content(m)) for m in recent)


def needs_compact(
    recent: Sequence[Mapping[str, str]],
    max_chars: int = RECENT_MAX_CHARS,
    max_turns: int = RECENT_MAX_TURNS,
) -> bool:
    return recent_chars(recent) > max_chars or len(recent) > max_turns


def split_overflow(
    recent: Sequence[Mapping[str, str]],
    max_chars: int = RECENT_MAX_CHARS,
    max_turns: int = RECENT_MAX_TURNS,
) -> Tuple[List[Message], List[Message]]:
    """Return (dropped, kept). Dropped is the prefix that no longer fits."""
    kept: List[Message] = [dict(m) for m in recent]
    dropped: List[Message] = []
    while kept and (recent_chars(kept) > max_chars or len(kept) > max_turns):
        dropped.append(kept.pop(0))
    return dropped, kept


def clip_summary(text: str, max_chars: int = SUMMARY_MAX_CHARS) -> str:
    """Keep the tail so the beginning of the conversation fades first."""
    text = (text or "").strip()
    if max_chars <= 0 or len(text) <= max_chars:
        return text
    if max_chars <= 2:
        return text[-max_chars:]
    return "…\n" + text[-(max_chars - 2) :]


def turn_excerpt(msg: Mapping[str, str], limit: int = TURN_EXCERPT_CHARS) -> str:
    lines = [ln.strip() for ln in _content(msg).strip().splitlines() if ln.strip()][:2]
    excerpt = " ".join(lines) if lines else "(vacío)"
    if limit > 0 and len(excerpt) > limit:
        excerpt = excerpt[:limit].rstrip() + "…"
    return f"- {_role(msg)}: {excerpt}"


def extractive_fold(
    old_summary: str,
    dropped: Iterable[Mapping[str, str]],
    max_chars: int = SUMMARY_MAX_CHARS,
    excerpt_chars: int = TURN_EXCERPT_CHARS,
) -> str:
    bits: List[str] = []
    prev = (old_summary or "").strip()
    if prev:
        bits.append(prev)
    dropped_list = list(dropped)
    if dropped_list:
        bits.append("Notas (extractivo):")
        for msg in dropped_list:
            bits.append(turn_excerpt(msg, excerpt_chars))
    return clip_summary("\n".join(bits), max_chars)


def summary_prompt(
    old_summary: str,
    dropped: Sequence[Mapping[str, str]],
) -> Tuple[str, str]:
    """Return (system, user) for the same chat GGUF to compact memory."""
    parts: List[str] = []
    prev = (old_summary or "").strip()
    parts.append("Resumen previo:\n" + (prev if prev else "(vacío)"))
    parts.append("\nTurnos a compactar:")
    if not dropped:
        parts.append("(ninguno)")
    else:
        for msg in dropped:
            parts.append(f"[{_role(msg)}]\n{_content(msg)}")
    parts.append("\nNuevo resumen acumulado:")
    return SUMMARY_SYSTEM, "\n".join(parts)


def build_messages(
    user: str,
    system: str = "",
    summary: str = "",
    recent: Sequence[Mapping[str, str]] = (),
) -> List[Message]:
    messages: List[Message] = []
    sys_parts: List[str] = []
    sys = (system or "").strip()
    if sys:
        sys_parts.append(sys)
    mem = (summary or "").strip()
    if mem:
        sys_parts.append(MEMORY_PREFIX + mem)
    if sys_parts:
        messages.append({"role": "system", "content": "\n\n".join(sys_parts)})
    for msg in recent:
        role = _role(msg)
        if role not in ("user", "assistant", "system"):
            role = "user"
        messages.append({"role": role, "content": _content(msg)})
    messages.append({"role": "user", "content": user})
    return messages


class ChatSession:
    def __init__(
        self,
        recent_max_chars: int = RECENT_MAX_CHARS,
        recent_max_turns: int = RECENT_MAX_TURNS,
        summary_max_chars: int = SUMMARY_MAX_CHARS,
    ) -> None:
        self.recent_max_chars = recent_max_chars
        self.recent_max_turns = recent_max_turns
        self.summary_max_chars = summary_max_chars
        self.system = ""
        self.summary = ""
        self.recent: List[Message] = []
        self.turns = 0

    def reset(self) -> None:
        self.system = ""
        self.summary = ""
        self.recent = []
        self.turns = 0

    def needs_compact(self) -> bool:
        return needs_compact(self.recent, self.recent_max_chars, self.recent_max_turns)

    def pop_overflow(self) -> List[Message]:
        dropped, kept = split_overflow(
            self.recent, self.recent_max_chars, self.recent_max_turns
        )
        self.recent = kept
        return dropped

    def apply_summary(self, text: str) -> None:
        self.summary = clip_summary(text, self.summary_max_chars)

    def build_messages(self, user: str) -> List[Message]:
        return build_messages(user, self.system, self.summary, self.recent)

    def commit(self, user: str, assistant: str) -> None:
        self.recent.append({"role": "user", "content": user})
        self.recent.append({"role": "assistant", "content": assistant})
        self.turns += 1

    def status(self) -> dict:
        return {
            "on": self.turns > 0 or bool(self.summary) or bool(self.recent),
            "turns": self.turns,
            "summary_chars": len(self.summary),
            "recent_turns": len(self.recent),
        }
