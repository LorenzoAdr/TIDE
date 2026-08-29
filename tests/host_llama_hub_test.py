#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import host_llama_hub as hub  # noqa: E402


class HostLlamaHubTest(unittest.TestCase):
    def test_allowed_hf_gguf_url(self) -> None:
        self.assertTrue(
            hub.allowed_import_url(
                "https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/main/x.gguf"
            )
        )
        self.assertFalse(hub.allowed_import_url("https://example.com/x.gguf"))
        self.assertFalse(hub.allowed_import_url("http://huggingface.co/x.gguf"))
        self.assertFalse(hub.allowed_import_url("https://huggingface.co/Qwen/README.md"))

    def test_alias_and_size(self) -> None:
        self.assertEqual(hub.alias_from_gguf("/tmp/foo-7b.gguf"), "foo-7b")
        self.assertEqual(hub.human_size(84106624), "80M")

    def test_catalog_has_tide_packs(self) -> None:
        ids = {m["id"] for m in hub.load_catalog_file()["models"]}
        self.assertIn("qwen2.5-coder-7b-instruct-q4_k_m", ids)
        self.assertIn("nomic-embed-text-v1.5-q4_k_m", ids)


if __name__ == "__main__":
    unittest.main()
