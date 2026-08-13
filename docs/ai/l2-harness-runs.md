# L2 harness validation runs

Also see **autonomous local/remote**: [`l2-autonomous.md`](l2-autonomous.md) (`ai.level2.mode=local|remote`, `/l2_run`, streaming `L2 ▸ fase=…`).

## Explore → edit → compile

1. Query L1 con `ai.level2.mode=harness` → `session.md` en fase **explore**.
2. Tools de lectura; luego:
   ```bash
   ./build/l2_harness_cli done "listo" --edit
   # o request.json: {"action":"done","summary":"…","next":"edit"}
   ```
3. Emitir hunks:
   ```bash
   ./build/l2_harness_cli edit /path/to/hunks.json
   ```
   El runtime aplica Search/Replace y lanza compile automáticamente.
4. Si compile falla: lee feedback en Observations y reemite `edit` (≤3). Si OK: `done` resumen.

## Explore-only cases

| # | Query | Expected focus |
|---|-------|----------------|
| 1 | cómo despierta la UI / wake bridge | `ui_wake`, bridge wake |
| 2 | dónde se arma el embed / two-stage L1 | `coding_embed_rerank` |
| 3 | cómo se escribe map_last | `dump_ranked_map_md` |

```bash
tools/l2_harness.sh validate-fixtures
./build/search_replace_test && ./build/level2_session_test && ./build/level2_autonomous_loop_test
```
