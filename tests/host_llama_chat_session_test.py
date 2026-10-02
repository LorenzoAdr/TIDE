#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import host_llama_chat_session as mem  # noqa: E402


class BuildMessagesTest(unittest.TestCase):
    def test_user_only(self) -> None:
        msgs = mem.build_messages("hola")
        self.assertEqual(msgs, [{"role": "user", "content": "hola"}])

    def test_system_summary_recent_and_user(self) -> None:
        recent = [
            {"role": "user", "content": "antes"},
            {"role": "assistant", "content": "ok"},
        ]
        msgs = mem.build_messages(
            "ahora",
            system="sé breve",
            summary="hablamos de Foo.cpp",
            recent=recent,
        )
        self.assertEqual(msgs[0]["role"], "system")
        self.assertIn("sé breve", msgs[0]["content"])
        self.assertIn(mem.MEMORY_PREFIX.strip(), msgs[0]["content"])
        self.assertIn("Foo.cpp", msgs[0]["content"])
        self.assertEqual(msgs[1], recent[0])
        self.assertEqual(msgs[2], recent[1])
        self.assertEqual(msgs[-1], {"role": "user", "content": "ahora"})

    def test_summary_without_system(self) -> None:
        msgs = mem.build_messages("sigue", summary="contexto viejo")
        self.assertEqual(msgs[0]["role"], "system")
        self.assertTrue(msgs[0]["content"].startswith(mem.MEMORY_PREFIX))
        self.assertEqual(msgs[-1]["content"], "sigue")


class OverflowTest(unittest.TestCase):
    def test_needs_compact_by_turns(self) -> None:
        recent = [{"role": "user", "content": "x"}] * 9
        self.assertTrue(mem.needs_compact(recent, max_chars=99999, max_turns=8))
        self.assertFalse(mem.needs_compact(recent[:8], max_chars=99999, max_turns=8))

    def test_needs_compact_by_chars(self) -> None:
        recent = [{"role": "user", "content": "abcd"}, {"role": "assistant", "content": "efgh"}]
        self.assertTrue(mem.needs_compact(recent, max_chars=6, max_turns=99))
        self.assertFalse(mem.needs_compact(recent, max_chars=80, max_turns=99))

    def test_split_overflow_keeps_tail(self) -> None:
        recent = [{"role": "user", "content": f"t{i}"} for i in range(6)]
        dropped, kept = mem.split_overflow(recent, max_chars=99999, max_turns=4)
        self.assertEqual([m["content"] for m in dropped], ["t0", "t1"])
        self.assertEqual([m["content"] for m in kept], ["t2", "t3", "t4", "t5"])

    def test_split_overflow_by_chars(self) -> None:
        recent = [
            {"role": "user", "content": "aaaa"},
            {"role": "assistant", "content": "bbbb"},
            {"role": "user", "content": "cc"},
        ]
        dropped, kept = mem.split_overflow(recent, max_chars=2, max_turns=99)
        self.assertEqual(len(dropped), 2)
        self.assertEqual(kept, [{"role": "user", "content": "cc"}])


class SummaryFoldTest(unittest.TestCase):
    def test_clip_summary_fades_start(self) -> None:
        text = "ABCDEFGHIJKLMNOPQRST"
        out = mem.clip_summary(text, max_chars=8)
        self.assertEqual(out, "…\nOPQRST")
        self.assertLessEqual(len(out), 8)

    def test_clip_summary_noop_when_short(self) -> None:
        self.assertEqual(mem.clip_summary("hola", 40), "hola")

    def test_extractive_fold_appends_excerpts(self) -> None:
        dropped = [
            {"role": "user", "content": "abre Foo.cpp y mira bar()\nsegunda linea"},
            {"role": "assistant", "content": "listo"},
        ]
        folded = mem.extractive_fold("previo", dropped, max_chars=4000)
        self.assertIn("previo", folded)
        self.assertIn("Notas (extractivo)", folded)
        self.assertIn("user:", folded)
        self.assertIn("Foo.cpp", folded)
        self.assertIn("assistant:", folded)

    def test_extractive_fold_clips_to_cap(self) -> None:
        dropped = [{"role": "user", "content": "x" * 200}]
        folded = mem.extractive_fold("OLD" * 40, dropped, max_chars=40, excerpt_chars=20)
        self.assertLessEqual(len(folded), 40)
        self.assertTrue(folded.startswith("…\n"))

    def test_summary_prompt_includes_prev_and_dropped(self) -> None:
        sys_p, user_p = mem.summary_prompt(
            "decidimos editar session.md",
            [{"role": "user", "content": "y el pack?"}],
        )
        self.assertIn("compactador", sys_p)
        self.assertIn("decidimos editar session.md", user_p)
        self.assertIn("y el pack?", user_p)
        self.assertIn("[user]", user_p)


class ChatSessionTest(unittest.TestCase):
    def test_commit_and_reset(self) -> None:
        s = mem.ChatSession()
        s.system = "sys"
        s.commit("hola", "adiós")
        self.assertEqual(s.turns, 1)
        self.assertEqual(len(s.recent), 2)
        self.assertTrue(s.status()["on"])
        s.reset()
        self.assertEqual(s.turns, 0)
        self.assertEqual(s.recent, [])
        self.assertEqual(s.system, "")
        self.assertFalse(s.status()["on"])

    def test_pop_overflow_and_apply_summary(self) -> None:
        s = mem.ChatSession(recent_max_chars=50, recent_max_turns=4, summary_max_chars=20)
        for i in range(3):
            s.commit("user-" + str(i) * 8, "as-" + str(i) * 8)
        self.assertTrue(s.needs_compact())
        dropped = s.pop_overflow()
        self.assertTrue(dropped)
        self.assertFalse(s.needs_compact())
        s.apply_summary("principio de la charla que debe recortarse al final")
        self.assertTrue(s.summary.startswith("…\n"))
        self.assertLessEqual(len(s.summary), 20)

    def test_build_messages_uses_session_state(self) -> None:
        s = mem.ChatSession()
        s.system = "rol"
        s.summary = "mem"
        s.commit("u1", "a1")
        msgs = s.build_messages("u2")
        self.assertEqual(msgs[-1]["content"], "u2")
        self.assertEqual(msgs[-2]["content"], "a1")
        self.assertIn("mem", msgs[0]["content"])


class HandleAskPostTest(unittest.TestCase):
    def setUp(self) -> None:
        import host_llama_spy as spy

        self.spy = spy
        spy.chat_session.recent_max_turns = mem.RECENT_MAX_TURNS
        spy.chat_session.recent_max_chars = mem.RECENT_MAX_CHARS
        spy.chat_session.summary_max_chars = mem.SUMMARY_MAX_CHARS
        spy.reset_chat_session()

    def tearDown(self) -> None:
        self.spy.chat_session.recent_max_turns = mem.RECENT_MAX_TURNS
        self.spy.chat_session.recent_max_chars = mem.RECENT_MAX_CHARS
        self.spy.chat_session.summary_max_chars = mem.SUMMARY_MAX_CHARS
        self.spy.reset_chat_session()

    def test_reset_without_user(self) -> None:
        code, payload = self.spy.handle_ask_post(
            {"mode": "chat", "reset": True}, "127.0.0.1", 1
        )
        self.assertEqual(code, 200)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["thread"]["turns"], 0)

    def test_empty_user_oneshot(self) -> None:
        code, payload = self.spy.handle_ask_post({"user": "  "}, "127.0.0.1", 1)
        self.assertEqual(code, 400)
        self.assertIn("user vacío", payload["error"])

    def test_chat_sends_prior_turns(self) -> None:
        from unittest import mock

        def fake_ask(user, system, host, port, messages=None, log_kind=""):
            return "ok:" + user[:12], "rid", ""

        with mock.patch.object(self.spy, "run_direct_ask", side_effect=fake_ask) as m:
            c1, p1 = self.spy.handle_ask_post(
                {"mode": "chat", "user": "hola", "system": "sé breve"}, "h", 1
            )
            self.assertEqual(c1, 200)
            self.assertEqual(p1["thread"]["turns"], 1)
            c2, p2 = self.spy.handle_ask_post({"mode": "chat", "user": "seguimos"}, "h", 1)
            self.assertEqual(c2, 200)
            self.assertEqual(p2["thread"]["turns"], 2)
        last_kwargs = m.call_args.kwargs
        blob = str(last_kwargs.get("messages"))
        self.assertIn("hola", blob)
        self.assertIn("seguimos", blob)
        self.assertEqual(last_kwargs.get("log_kind"), "chat")

    def test_overflow_triggers_summary_call(self) -> None:
        from unittest import mock

        self.spy.chat_session.recent_max_turns = 2
        self.spy.chat_session.recent_max_chars = 80
        kinds = []

        def fake_ask(user, system, host, port, messages=None, log_kind=""):
            kinds.append(log_kind or "chat")
            if log_kind == "summary":
                return "resumen compactado de Foo.cpp", "sum", ""
            return "ok", "rid", ""

        with mock.patch.object(self.spy, "run_direct_ask", side_effect=fake_ask):
            for i, text in enumerate(("uno", "dos", "tres")):
                code, payload = self.spy.handle_ask_post(
                    {"mode": "chat", "user": text + "-mensaje"}, "h", 1
                )
                self.assertEqual(code, 200, payload)
        self.assertIn("summary", kinds)
        self.assertGreater(self.spy.chat_session.status()["summary_chars"], 0)

    def test_summary_llm_failure_uses_extractive(self) -> None:
        from unittest import mock

        self.spy.chat_session.recent_max_turns = 2
        self.spy.chat_session.recent_max_chars = 80

        def fake_ask(user, system, host, port, messages=None, log_kind=""):
            if log_kind == "summary":
                return "", "sum", "backend down"
            return "ok", "rid", ""

        with mock.patch.object(self.spy, "run_direct_ask", side_effect=fake_ask):
            for text in ("uno", "dos", "tres"):
                code, payload = self.spy.handle_ask_post(
                    {"mode": "chat", "user": text + "-mensaje"}, "h", 1
                )
                self.assertEqual(code, 200, payload)
        self.assertIn("extractivo", self.spy.chat_session.summary)
        self.assertGreater(self.spy.chat_session.status()["summary_chars"], 0)


class CompletionCtxTest(unittest.TestCase):
    def setUp(self) -> None:
        import host_llama_spy as spy

        self.spy = spy
        spy.clear_last_chat_ctx()

    def tearDown(self) -> None:
        self.spy.clear_last_chat_ctx()

    def test_usage_total_tokens(self) -> None:
        ctx = self.spy.completion_ctx({
            "usage": {
                "prompt_tokens": 40,
                "completion_tokens": 12,
                "total_tokens": 52,
                "prompt_tokens_details": {"cached_tokens": 8},
            }
        })
        self.assertEqual(ctx["n_tokens"], 52)
        self.assertEqual(ctx["prompt"], 40)
        self.assertEqual(ctx["predicted"], 12)
        self.assertEqual(ctx["cached"], 8)

    def test_timings_cache_plus_prompt_plus_predicted(self) -> None:
        ctx = self.spy.completion_ctx({
            "timings": {"cache_n": 236, "prompt_n": 1, "predicted_n": 35}
        })
        self.assertEqual(ctx["n_tokens"], 272)
        self.assertEqual(ctx["prompt"], 237)
        self.assertEqual(ctx["predicted"], 35)

    def test_record_and_reset(self) -> None:
        self.spy.record_chat_ctx({"n_tokens": 400, "prompt": 300, "predicted": 100})
        self.assertEqual(self.spy.last_chat_ctx()["n_tokens"], 400)
        self.spy.reset_chat_session()
        self.assertEqual(self.spy.last_chat_ctx()["n_tokens"], 0)


if __name__ == "__main__":
    unittest.main()
