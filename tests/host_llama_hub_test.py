#!/usr/bin/env python3
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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
        self.assertIn("qwen2.5-1.5b-instruct-q4_k_m", ids)
        self.assertIn("llama-3.3-70b-instruct-q4_k_m", ids)


class HostLlamaPerfTest(unittest.TestCase):
    def setUp(self) -> None:
        hub.launch_opts.clear()
        hub._llama_help.clear()

    def _env(self, **kwargs: str) -> dict:
        base = {
            "TUIDE_HOST_FLASH_ATTN": "on",
            "TUIDE_HOST_CACHE_TYPE": "q8_0",
            "TUIDE_HOST_THREADS": "8",
            "TUIDE_HOST_NP": "1",
            "TUIDE_HOST_DRAFT": "off",
            "TUIDE_HOST_THINKING": "on",
            "TUIDE_HOST_EMBED_NGL": "0",
            "TUIDE_HOST_NGL": "99",
            "TUIDE_HOST_CHAT_CTX": "32768",
            "TUIDE_HOST_DRAFT_N_MAX": "16",
        }
        base.update(kwargs)
        return base

    def test_chat_quality_preserving_flags(self) -> None:
        with mock.patch.dict(os.environ, self._env(), clear=False):
            cmd = hub.chat_llama_argv(
                "llama-server", "/tmp/qwen2.5-coder-14b.gguf", "127.0.0.1", 18080, "alias"
            )
        self.assertEqual(cmd[cmd.index("-fa") + 1], "on")
        self.assertEqual(cmd[cmd.index("-ctk") + 1], "q8_0")
        self.assertEqual(cmd[cmd.index("-ctv") + 1], "q8_0")
        self.assertEqual(cmd[cmd.index("-np") + 1], "1")
        self.assertEqual(cmd[cmd.index("-t") + 1], "8")
        self.assertEqual(cmd[cmd.index("-tb") + 1], "8")
        self.assertEqual(cmd[cmd.index("-ngl") + 1], "99")
        self.assertEqual(cmd[cmd.index("-c") + 1], "32768")
        self.assertIn("--metrics", cmd)
        self.assertNotIn("-md", cmd)

    def test_embed_stays_on_cpu(self) -> None:
        with mock.patch.dict(os.environ, self._env(), clear=False):
            cmd = hub.embed_llama_argv("llama-server", "/tmp/nomic.gguf", "127.0.0.1", 18765)
        self.assertEqual(cmd[cmd.index("-ngl") + 1], "0")
        self.assertIn("--embedding", cmd)
        self.assertIn("--metrics", cmd)

    def test_cache_type_off_omits_ctk(self) -> None:
        with mock.patch.dict(os.environ, self._env(TUIDE_HOST_CACHE_TYPE="off"), clear=False):
            cmd = hub.chat_llama_argv(
                "llama-server", "/tmp/qwen2.5-coder-7b.gguf", "127.0.0.1", 8080, "a"
            )
        self.assertNotIn("-ctk", cmd)
        self.assertNotIn("-ctv", cmd)

    def test_draft_forced_when_gguf_present(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            draft = Path(td) / "qwen2.5-1.5b-instruct-q4_k_m.gguf"
            chat = Path(td) / "qwen2.5-coder-14b-instruct-q4_k_m.gguf"
            draft.write_bytes(b"d" * 200)
            chat.write_bytes(b"c" * 2000)
            env = self._env(
                TUIDE_HOST_DRAFT="on",
                TUIDE_HOST_DRAFT_GGUF=str(draft),
            )
            with mock.patch.dict(os.environ, env, clear=False):
                cmd = hub.chat_llama_argv("llama-server", str(chat), "127.0.0.1", 8080, "a")
        self.assertEqual(cmd[cmd.index("-md") + 1], str(draft))
        self.assertEqual(cmd[cmd.index("--spec-draft-n-max") + 1], "16")
        self.assertEqual(cmd[cmd.index("-ctkd") + 1], "q8_0")
        self.assertEqual(cmd[cmd.index("-ngld") + 1], "99")

    def test_no_draft_for_1_5b_chat(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            draft = Path(td) / "qwen2.5-1.5b-instruct-q4_k_m.gguf"
            chat = Path(td) / "qwen2.5-1.5b-instruct-q4_k_m-copy.gguf"
            draft.write_bytes(b"d" * 200)
            chat.write_bytes(b"c" * 200)
            env = self._env(
                TUIDE_HOST_DRAFT="on",
                TUIDE_HOST_DRAFT_GGUF=str(draft),
            )
            with mock.patch.dict(os.environ, env, clear=False):
                cmd = hub.chat_llama_argv("llama-server", str(chat), "127.0.0.1", 8080, "a")
        self.assertNotIn("-md", cmd)

    def test_auto_draft_skips_when_ram_tight(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            draft = Path(td) / "qwen2.5-1.5b-instruct-q4_k_m.gguf"
            chat = Path(td) / "qwen2.5-coder-32b-instruct-q4_k_m.gguf"
            draft.write_bytes(b"d" * 1024)
            chat.write_bytes(b"c" * 4096)
            env = self._env(
                TUIDE_HOST_DRAFT="auto",
                TUIDE_HOST_DRAFT_GGUF=str(draft),
            )
            with mock.patch.dict(os.environ, env, clear=False):
                with mock.patch.object(hub, "total_ram_bytes", return_value=2 * 1024 * 1024 * 1024):
                    self.assertEqual(hub.select_draft_path(str(chat)), "")
                    cmd = hub.chat_llama_argv("llama-server", str(chat), "127.0.0.1", 8080, "a")
        self.assertNotIn("-md", cmd)

    def test_model_supports_thinking(self) -> None:
        self.assertTrue(hub.model_supports_thinking("qwen3.6-27b-q8_0.gguf"))
        self.assertTrue(hub.model_supports_thinking("/cache/Qwen_Qwen3.6-27B-Q8_0.gguf"))
        self.assertTrue(hub.model_supports_thinking("DeepSeek-R1-Distill-Qwen-32B.gguf"))
        self.assertTrue(hub.model_supports_thinking("gpt-oss-20b-mxfp4.gguf"))
        self.assertTrue(hub.model_supports_thinking("Magistral-Small-2509-Q4_K_M.gguf"))
        self.assertFalse(hub.model_supports_thinking("Llama-3.3-70B-Instruct-Q4_K_M.gguf"))
        self.assertFalse(hub.model_supports_thinking("qwen2.5-coder-32b-instruct-q4_k_m.gguf"))

    def test_thinking_flags_on_qwen3(self) -> None:
        hub._llama_help.clear()
        hub._llama_help["llama-server"] = ""
        with mock.patch.dict(os.environ, self._env(TUIDE_HOST_THINKING="on"), clear=False):
            cmd = hub.chat_llama_argv(
                "llama-server", "/tmp/qwen3.6-27b-q8_0.gguf", "127.0.0.1", 8080, "a"
            )
        self.assertIn("--jinja", cmd)
        self.assertEqual(cmd[cmd.index("--reasoning-format") + 1], "deepseek")
        self.assertEqual(
            cmd[cmd.index("--chat-template-kwargs") + 1],
            '{"enable_thinking":true}',
        )

    def test_thinking_off_disables_kwargs(self) -> None:
        hub._llama_help.clear()
        hub._llama_help["llama-server"] = ""
        with mock.patch.dict(os.environ, self._env(TUIDE_HOST_THINKING="off"), clear=False):
            cmd = hub.chat_llama_argv(
                "llama-server", "/tmp/qwen3.6-27b-q8_0.gguf", "127.0.0.1", 8080, "a"
            )
        self.assertEqual(
            cmd[cmd.index("--chat-template-kwargs") + 1],
            '{"enable_thinking":false}',
        )
        self.assertEqual(cmd[cmd.index("--reasoning-budget") + 1], "0")

    def test_thinking_skipped_for_llama70(self) -> None:
        hub._llama_help.clear()
        with mock.patch.dict(os.environ, self._env(TUIDE_HOST_THINKING="on"), clear=False):
            cmd = hub.chat_llama_argv(
                "llama-server",
                "/tmp/Llama-3.3-70B-Instruct-Q4_K_M.gguf",
                "127.0.0.1",
                8080,
                "a",
            )
        self.assertNotIn("--jinja", cmd)
        self.assertNotIn("--reasoning-format", cmd)
        self.assertNotIn("--chat-template-kwargs", cmd)

    def test_apply_thinking_opt(self) -> None:
        self.assertEqual(hub.apply_launch_opts({"thinking": "off"}), "")
        self.assertEqual(hub.effective_launch_opts()["thinking"], "off")
        self.assertIn("thinking", hub.apply_launch_opts({"thinking": "maybe"}))

    def test_parse_on_off_auto(self) -> None:
        self.assertEqual(hub.parse_on_off_auto("0"), "off")
        self.assertEqual(hub.parse_on_off_auto("yes"), "on")
        self.assertEqual(hub.parse_on_off_auto(""), "auto")

    def test_apply_launch_opts_overrides_env(self) -> None:
        with mock.patch.dict(os.environ, self._env(), clear=False):
            err = hub.apply_launch_opts({
                "flash_attn": "off",
                "cache_type": "f16",
                "threads": "4",
                "np": "2",
                "ngl": "40",
                "chat_ctx": "8192",
                "draft": "off",
            })
            self.assertEqual(err, "")
            cmd = hub.chat_llama_argv(
                "llama-server", "/tmp/qwen2.5-coder-14b.gguf", "127.0.0.1", 8080, "a"
            )
        self.assertEqual(cmd[cmd.index("-fa") + 1], "off")
        self.assertNotIn("-ctk", cmd)
        self.assertEqual(cmd[cmd.index("-t") + 1], "4")
        self.assertEqual(cmd[cmd.index("-np") + 1], "2")
        self.assertEqual(cmd[cmd.index("-ngl") + 1], "40")
        self.assertEqual(cmd[cmd.index("-c") + 1], "8192")

    def test_apply_launch_opts_rejects_bad_cache(self) -> None:
        self.assertIn("cache_type", hub.apply_launch_opts({"cache_type": "nope"}))

    def test_default_launch_opts_keys(self) -> None:
        env = {
            "TUIDE_HOST_FLASH_ATTN": "on",
            "TUIDE_HOST_CACHE_TYPE": "q8_0",
            "TUIDE_HOST_NP": "1",
            "TUIDE_HOST_EMBED_NGL": "0",
            "TUIDE_HOST_DRAFT": "auto",
            "TUIDE_HOST_THINKING": "on",
        }
        with mock.patch.dict(os.environ, env, clear=False):
            opts = hub.default_launch_opts()
            payload = hub.status_payload()
        self.assertEqual(opts["flash_attn"], "on")
        self.assertEqual(opts["cache_type"], "q8_0")
        self.assertEqual(opts["np"], "1")
        self.assertEqual(opts["embed_ngl"], "0")
        self.assertEqual(opts["draft"], "auto")
        self.assertEqual(opts["thinking"], "on")
        self.assertIn("launch", payload)
        self.assertIn("drafts", payload)
        self.assertIn("launch_defaults", payload)
        self.assertIn("host", payload)


class HostLlamaSpyStatusTest(unittest.TestCase):
    def test_status_payload_includes_thread(self) -> None:
        payload = hub.status_payload()
        self.assertIn("thread", payload)
        self.assertIn("turns", payload["thread"])
        self.assertIn("summary_chars", payload["thread"])
        self.assertIn("recent_turns", payload["thread"])
        self.assertIn("host", payload)
        self.assertIn("ram_total", payload["host"])
        self.assertIn("llm", payload["host"])

    def test_clear_history_truncates_jsonl(self) -> None:
        fd, path = tempfile.mkstemp(suffix=".jsonl")
        os.close(fd)
        prev = hub.spy.jsonl_path
        try:
            hub.spy.jsonl_path = path
            hub.spy.jsonl_emit({"kind": "req", "id": "x", "user": "hola"})
            self.assertGreater(os.path.getsize(path), 0)
            out = hub.spy.clear_history()
            self.assertTrue(out["ok"])
            self.assertEqual(os.path.getsize(path), 0)
        finally:
            hub.spy.jsonl_path = prev
            os.unlink(path)

    def test_request_stop_generation_sets_flag(self) -> None:
        hub.spy.cancel_generation.clear()
        try:
            out = hub.spy.request_stop_generation("127.0.0.1", 1)
            self.assertTrue(out["ok"])
            self.assertTrue(hub.spy.cancel_generation.is_set())
        finally:
            hub.spy.cancel_generation.clear()


class HostMetricsTest(unittest.TestCase):
    def test_parse_prom_metrics(self) -> None:
        text = "\n".join([
            "# HELP llamacpp:kv_cache_usage_ratio x",
            "llamacpp:kv_cache_usage_ratio 0.25",
            'llamacpp:kv_cache_tokens{slot="0"} 2048',
        ])
        m = hub.parse_prom_metrics(text)
        self.assertEqual(hub.prom_get(m, "llamacpp:kv_cache_usage_ratio"), 0.25)
        self.assertEqual(hub.prom_get(m, "llamacpp:kv_cache_tokens"), 2048)

    def test_parse_ioreg_gpu(self) -> None:
        blob = (
            '"PerformanceStatistics" = {"In use system memory (driver)"=0,'
            '"Alloc system memory"=17890721792,"Device Utilization %"=38,'
            '"In use system memory"=463257600}'
        )
        g = hub.parse_ioreg_gpu(blob)
        self.assertEqual(g["gpu_pct"], 38)
        self.assertEqual(g["gpu_alloc"], 17890721792)
        self.assertEqual(g["gpu_in_use"], 463257600)

    def test_parse_vm_stat(self) -> None:
        text = (
            "Mach Virtual Memory Statistics: (page size of 16384 bytes)\n"
            "Pages free:                               100.\n"
            "Pages active:                             200.\n"
            "Pages wired down:                         50.\n"
            "Pages occupied by compressor:              25.\n"
        )
        used = hub.parse_vm_stat(text, 16384)
        self.assertEqual(used, (200 + 50 + 25) * 16384)

    def test_parse_pmset_therm(self) -> None:
        self.assertEqual(
            hub.parse_pmset_therm("Note: No thermal warning level has been recorded\n"),
            "ok",
        )
        self.assertEqual(
            hub.parse_pmset_therm("CPU_Speed_Limit\t= 80\n"),
            "throttle",
        )

    def test_estimate_kv_llama70_q8(self) -> None:
        # 80L · 8192 embd · 64H · 8 KV · q8 · 8192 ctx · 1 slot
        n = hub.estimate_kv_bytes(80, 8192, 64, 8, 8192, 1, "q8_0")
        self.assertEqual(n, 2 * 80 * (8192 * 8 / 64) * 8192 * 1)

    def test_llm_view_uses_metrics_ratio(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "x.gguf"
            path.write_bytes(b"x" * 1024)
            view = hub.llm_view(
                str(path), 8192, 1, "q8_0", "",
                {"llamacpp:kv_cache_usage_ratio": 0.5, "llamacpp:kv_cache_tokens": 4096},
                rss=2048, cpu_pct=12.0,
            )
        self.assertEqual(view["weights"], 1024)
        self.assertEqual(view["n_tokens"], 4096)
        self.assertEqual(view["kv_ratio"], 0.5)
        self.assertEqual(view["n_ctx"], 8192)
        self.assertEqual(view["ctx_src"], "kv")

    def test_resolve_ctx_tokens_prefers_live_slot(self) -> None:
        n, ratio, src = hub.resolve_ctx_tokens(
            8192,
            {"llamacpp:n_tokens_max": 400},
            slots=[{
                "n_prompt_tokens": 800,
                "next_token": {"n_decoded": 50},
                "is_processing": True,
            }],
            last_tokens=200,
        )
        self.assertEqual(n, 850)
        self.assertEqual(src, "slot")
        self.assertAlmostEqual(ratio, 850 / 8192)

    def test_resolve_ctx_tokens_idle_uses_last_then_peak(self) -> None:
        n, ratio, src = hub.resolve_ctx_tokens(
            32768, {"llamacpp:n_tokens_max": 1200}, slots=[], last_tokens=900
        )
        self.assertEqual((n, src), (900, "last"))
        self.assertAlmostEqual(ratio, 900 / 32768)
        n2, _, src2 = hub.resolve_ctx_tokens(32768, {"llamacpp:n_tokens_max": 1200})
        self.assertEqual((n2, src2), (1200, "peak"))
        n3, r3, src3 = hub.resolve_ctx_tokens(32768, {})
        self.assertEqual((n3, r3, src3), (0, 0.0, ""))
        n4, _, src4 = hub.resolve_ctx_tokens(
            32768, {}, slots=[{"next_token": {"n_decoded": 10}}], last_tokens=500
        )
        self.assertEqual((n4, src4), (500, "last"))

    def test_tokens_from_slot_cache_plus_processed(self) -> None:
        n = hub.tokens_from_slot({
            "n_prompt_tokens_cache": 100,
            "n_prompt_tokens_processed": 20,
            "next_token": {"n_decoded": 7},
        })
        self.assertEqual(n, 127)

    def test_tokens_from_slot_ignores_decoded_only(self) -> None:
        n = hub.tokens_from_slot({
            "is_processing": True,
            "next_token": {"n_decoded": 136},
        })
        self.assertEqual(n, 0)

    def test_parse_prom_n_tokens_max(self) -> None:
        m = hub.parse_prom_metrics("llamacpp:n_tokens_max 1536\n")
        self.assertEqual(hub.prom_get(m, "llamacpp:n_tokens_max"), 1536)

    def test_read_gguf_meta(self) -> None:
        def u32(n: int) -> bytes:
            return n.to_bytes(4, "little")

        def u64(n: int) -> bytes:
            return n.to_bytes(8, "little")

        def gg_str(s: str) -> bytes:
            b = s.encode("utf-8")
            return u64(len(b)) + b

        kvs = b""
        kvs += gg_str("general.architecture") + u32(8) + gg_str("llama")
        kvs += gg_str("llama.block_count") + u32(4) + u32(80)
        kvs += gg_str("llama.embedding_length") + u32(4) + u32(8192)
        kvs += gg_str("llama.attention.head_count") + u32(4) + u32(64)
        kvs += gg_str("llama.attention.head_count_kv") + u32(4) + u32(8)
        blob = b"GGUF" + u32(3) + u64(0) + u64(5) + kvs
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "t.gguf"
            path.write_bytes(blob)
            meta = hub.read_gguf_meta(str(path))
        self.assertEqual(meta["arch"], "llama")
        self.assertEqual(meta["n_layer"], 80)
        self.assertEqual(meta["n_embd"], 8192)
        self.assertEqual(meta["n_head"], 64)
        self.assertEqual(meta["n_head_kv"], 8)


class HostBrowserTest(unittest.TestCase):
    def test_app_prefers_webkit_window(self) -> None:
        with mock.patch.dict(os.environ, {"TUIDE_HOST_BROWSER": "app"}, clear=False):
            with mock.patch.object(hub, "ensure_host_webapp", return_value="/tmp/tuide-host-webapp"):
                with mock.patch.object(hub.platform, "system", return_value="Darwin"):
                    cmd = hub.browser_launch_argv("http://127.0.0.1:18767/#launch")
        self.assertEqual(cmd, ["/tmp/tuide-host-webapp", "http://127.0.0.1:18767/#launch"])

    def test_safari_mode_is_plain_safari(self) -> None:
        with mock.patch.dict(os.environ, {"TUIDE_HOST_BROWSER": "safari"}, clear=False):
            with mock.patch.object(hub.platform, "system", return_value="Darwin"):
                cmd = hub.browser_launch_argv("http://127.0.0.1:18767")
        self.assertEqual(cmd, ["open", "-a", "Safari", "http://127.0.0.1:18767"])

    def test_system_uses_open(self) -> None:
        with mock.patch.dict(os.environ, {"TUIDE_HOST_BROWSER": "system"}, clear=False):
            with mock.patch.object(hub.platform, "system", return_value="Darwin"):
                cmd = hub.browser_launch_argv("http://127.0.0.1:18767")
        self.assertEqual(cmd, ["open", "http://127.0.0.1:18767"])


if __name__ == "__main__":
    unittest.main()
