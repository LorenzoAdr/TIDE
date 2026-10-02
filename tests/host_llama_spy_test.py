#!/usr/bin/env python3
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import host_llama_spy as spy  # noqa: E402


class ExtractDeltaPartsTest(unittest.TestCase):
    def test_content_only(self) -> None:
        obj = {"choices": [{"delta": {"content": "hola"}}]}
        self.assertEqual(spy.extract_delta(obj), "hola")
        self.assertEqual(spy.extract_delta_parts(obj), ("hola", ""))

    def test_reasoning_content_stream(self) -> None:
        obj = {"choices": [{"delta": {"reasoning_content": "mmh", "content": ""}}]}
        self.assertEqual(spy.extract_delta_parts(obj), ("", "mmh"))
        self.assertEqual(spy.extract_delta(obj), "")

    def test_reasoning_alias(self) -> None:
        obj = {"choices": [{"delta": {"reasoning": "paso"}}]}
        self.assertEqual(spy.extract_delta_parts(obj), ("", "paso"))

    def test_message_fields(self) -> None:
        obj = {"choices": [{"message": {"content": "ok", "reasoning_content": "why"}}]}
        self.assertEqual(spy.extract_delta_parts(obj), ("ok", "why"))

    def test_empty_obj(self) -> None:
        self.assertEqual(spy.extract_delta_parts({}), ("", ""))
        self.assertEqual(spy.extract_delta_parts({"choices": []}), ("", ""))

    def test_delta_ignores_swapped_message(self) -> None:
        think = "Needles for busy_strip and clear_busy_spinner in the controller."
        action = '{"action":"ola_v1","do":"needles","ids":["M2"]}'
        obj = {
            "choices": [
                {
                    "delta": {"content": None, "reasoning_content": None},
                    "message": {"content": think, "reasoning_content": action},
                    "finish_reason": "stop",
                }
            ]
        }
        self.assertEqual(spy.extract_delta_parts(obj), ("", ""))
        self.assertEqual(spy.extract_delta_parts(obj, delta_only=True), ("", ""))

    def test_delta_token_not_mixed_with_message(self) -> None:
        obj = {
            "choices": [
                {
                    "delta": {"reasoning_content": "mmh"},
                    "message": {
                        "content": "full thinking dump",
                        "reasoning_content": '{"action":"ola_v1"}',
                    },
                }
            ]
        }
        self.assertEqual(spy.extract_delta_parts(obj), ("", "mmh"))


class StreamFoldTest(unittest.TestCase):
    def test_finish_message_does_not_invert(self) -> None:
        think = (
            "The spinner stays because begin_thinking never pairs with "
            "clear_busy_spinner after the L2 propose returns."
        )
        action = '{"action":"ola_v1","do":"needles","ids":["M2","M3"]}'
        chunks = [
            {"choices": [{"delta": {"reasoning_content": think}}]},
            {"choices": [{"delta": {"content": action}}]},
            {
                "choices": [
                    {
                        "delta": {"content": None, "reasoning_content": None},
                        "message": {"content": think, "reasoning_content": action},
                        "finish_reason": "stop",
                    }
                ]
            },
        ]
        content, reason = spy.fold_sse_parts(chunks)
        self.assertEqual(content, action)
        self.assertEqual(reason, think)

    def test_swapped_delta_dump_dropped(self) -> None:
        think = (
            "The spinner stays because begin_thinking never pairs with "
            "clear_busy_spinner after the L2 propose returns."
        )
        action = '{"action":"ola_v1","do":"needles","ids":["M2","M3"]}'
        chunks = [
            {"choices": [{"delta": {"reasoning_content": think}}]},
            {"choices": [{"delta": {"content": action}}]},
            {"choices": [{"delta": {"content": think, "reasoning_content": action}}]},
        ]
        content, reason = spy.fold_sse_parts(chunks)
        self.assertEqual(content, action)
        self.assertEqual(reason, think)

    def test_unswap_when_only_finish_is_inverted(self) -> None:
        think = "Long prose about the busy spinner and who clears it after propose."
        action = '{"action":"ola_v1","do":"peek","id":"M1"}'
        content, reason = spy.maybe_unswap_reasoning(think, action)
        self.assertEqual(content, action)
        self.assertEqual(reason, think)

    def test_unswap_leaves_correct_split(self) -> None:
        think = "Long prose about the busy spinner and who clears it after propose."
        action = '{"action":"ola_v1","do":"peek","id":"M1"}'
        self.assertEqual(spy.maybe_unswap_reasoning(action, think), (action, think))

    def test_chat_body_keeps_reasoning(self) -> None:
        import json
        raw = spy.chat_body_from_sse("answer", "m", "think")
        body = json.loads(raw.decode("utf-8"))
        msg = body["choices"][0]["message"]
        self.assertEqual(msg["content"], "answer")
        self.assertEqual(msg["reasoning_content"], "think")


class ThinkingPayloadTest(unittest.TestCase):
    def tearDown(self) -> None:
        spy.thinking_inject = None

    def test_inject_on(self) -> None:
        spy.thinking_inject = True
        out = spy.apply_thinking_payload({"messages": []}, src="direct")
        self.assertTrue(out["chat_template_kwargs"]["enable_thinking"])

    def test_inject_off_overrides_direct(self) -> None:
        spy.thinking_inject = False
        out = spy.apply_thinking_payload(
            {"chat_template_kwargs": {"enable_thinking": True}}, src="direct"
        )
        self.assertFalse(out["chat_template_kwargs"]["enable_thinking"])

    def test_vm_leaves_client_kwargs(self) -> None:
        spy.thinking_inject = False
        payload = {
            "chat_template_kwargs": {"enable_thinking": True},
            "thinking_budget_tokens": 1536,
            "reasoning_budget_tokens": 1536,
        }
        out = spy.apply_thinking_payload(payload, src="vm")
        self.assertTrue(out["chat_template_kwargs"]["enable_thinking"])
        self.assertEqual(out["thinking_budget_tokens"], 1536)
        self.assertEqual(out["reasoning_budget_tokens"], 1536)

    def test_vm_does_not_inject(self) -> None:
        spy.thinking_inject = True
        payload = {"messages": []}
        out = spy.apply_thinking_payload(payload, src="vm")
        self.assertNotIn("chat_template_kwargs", out)

    def test_no_inject_leaves_payload(self) -> None:
        spy.thinking_inject = None
        payload = {"messages": []}
        self.assertIs(spy.apply_thinking_payload(payload), payload)
        self.assertNotIn("chat_template_kwargs", payload)


class TokBatchChannelTest(unittest.TestCase):
    def test_emits_channel(self) -> None:
        fd, path = tempfile.mkstemp(suffix=".jsonl")
        os.close(fd)
        prev = spy.jsonl_path
        spy.jsonl_path = path
        try:
            batch = spy.TokBatch("req-1", "chat")
            batch.add("mmh ", "reason")
            batch.add("ok", "answer")
            batch.flush()
            lines = Path(path).read_text(encoding="utf-8").strip().splitlines()
            self.assertEqual(len(lines), 2)
            import json
            first = json.loads(lines[0])
            second = json.loads(lines[1])
            self.assertEqual(first["channel"], "reason")
            self.assertEqual(first["text"], "mmh ")
            self.assertEqual(second["channel"], "answer")
            self.assertEqual(second["text"], "ok")
        finally:
            spy.jsonl_path = prev
            os.unlink(path)


class RehomeThinkTest(unittest.TestCase):
    def test_no_tag_unchanged(self) -> None:
        self.assertEqual(spy.rehome_think("why", "hello"), ("why", "hello"))
        self.assertEqual(spy.rehome_think("", "hello"), ("", "hello"))

    def test_early_split_uses_last_close(self) -> None:
        reason = "Here's a thinking process:\n\n1.  **Analyze User Input:**\n   - **Query:** \"cómo se cancela"
        content = (
            "- **Object of Query:** spinner\n"
            "2.  **Map Query to Atlas:**\n"
            "   All constraints verified.\n"
            "</think>\n\n"
            '{"action":"causal_atlas_survey_v1"}'
        )
        r, a = spy.rehome_think(reason, content)
        self.assertIn("Map Query to Atlas", r)
        self.assertNotIn("</think>", r)
        self.assertNotIn("</think>", a)
        self.assertEqual(a, '{"action":"causal_atlas_survey_v1"}')
        self.assertTrue(r.startswith("Here's a thinking process:"))

    def test_tags_only_in_content(self) -> None:
        content = "<think>\nplan\n</think>\n\nfinal"
        r, a = spy.rehome_think("", content)
        self.assertEqual(r, "plan\n")
        self.assertEqual(a, "final")

    def test_empty_answer_after_close(self) -> None:
        r, a = spy.rehome_think("short", "more think</think>")
        self.assertIn("more think", r)
        self.assertEqual(a, "")

    def test_last_close_wins(self) -> None:
        content = "aaa</think>bbb</think>\n\nans"
        r, a = spy.rehome_think("pre", content)
        self.assertIn("aaa", r)
        self.assertIn("bbb", r)
        self.assertEqual(a, "ans")

    def test_stray_close_without_open_is_ignored(self) -> None:
        content = "example uses </think> as HTML then the real answer"
        self.assertEqual(spy.rehome_think("", content), ("", content))


if __name__ == "__main__":
    unittest.main()
