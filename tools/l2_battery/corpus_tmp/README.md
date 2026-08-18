# Corpus temporal (borrar cuando el replay ya no haga falta)

Copias pequeñas de rounds 7B para máquinas sin GPU. **No** es `.tuide/` completo.

| Dir | Origen |
|-----|--------|
| `l2_phase_e_hard2/` | `.tuide/ai/l2_phase_e_hard2` |
| `hard_repeat/` | `.tuide/ai/l2_overnight/hard_repeat` |

Por caso: `session.md`, `run.log`, `state.json`. En la raíz del round: `results.jsonl`, `metrics.json`.

```bash
python3 tools/l2_battery/replay_failed_hunks.py \
  --round-dir tools/l2_battery/corpus_tmp/l2_phase_e_hard2 \
  --round-dir tools/l2_battery/corpus_tmp/hard_repeat \
  --cli build/l2_harness_cli
```
