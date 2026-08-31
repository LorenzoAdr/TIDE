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


class ThinkingPayloadTest(unittest.TestCase):
    def tearDown(self) -> None:
        spy.thinking_inject = None

    def test_inject_on(self) -> None:
        spy.thinking_inject = True
        out = spy.apply_thinking_payload({"messages": []})
        self.assertTrue(out["chat_template_kwargs"]["enable_thinking"])

    def test_inject_off_overrides(self) -> None:
        spy.thinking_inject = False
        out = spy.apply_thinking_payload({"chat_template_kwargs": {"enable_thinking": True}})
        self.assertFalse(out["chat_template_kwargs"]["enable_thinking"])

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


if __name__ == "__main__":
    unittest.main()
