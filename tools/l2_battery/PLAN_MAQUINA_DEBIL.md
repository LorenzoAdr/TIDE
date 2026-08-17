# Plan: máquina sin 7B

El cuello actual es el **runtime** (apply / shape / sibling), no otro prompt pack.
Esta máquina no sustituye al 7B: reinyecta hunks que el 7B **ya emitió**.

## 0. Traer código y artefactos

```bash
git checkout feature/IA
git pull
```

Copia (o monta) al menos un round con `session.md`:

- `.tuide/ai/l2_phase_e_hard2/`
- `.tuide/ai/l2_overnight/hard_repeat/`
- o el smoke/hard que esté corriendo ahora (`.tuide/ai/l2_phase_s_*`)

Sin esos `session.md` el replay no tiene corpus.

## 1. Tests (minutos, 0 LLM)

```bash
cmake --build build -j$(nproc) --target \
  search_replace_test level2_session_test l2_action_test level2_autonomous_loop_test
./build/search_replace_test
L2_FEAT_POST_EDIT_COVERAGE=1 ./build/level2_session_test
./build/l2_action_test
./build/level2_autonomous_loop_test
```

Si falla aquí, no hace falta GPU: el harness está roto.

## 2. Replay de hunks fallidos (lo más útil)

Contra archivos limpios de `git HEAD` (no hace falta llama):

```bash
python3 tools/l2_battery/replay_failed_hunks.py \
  --round-dir .tuide/ai/l2_phase_e_hard2 \
  --round-dir .tuide/ai/l2_overnight/hard_repeat
```

Con binario (flex C++ real, sigue sin LLM):

```bash
cmake --build build -j$(nproc) --target l2_harness_cli
python3 tools/l2_battery/replay_failed_hunks.py \
  --round-dir .tuide/ai/l2_phase_e_hard2 \
  --round-dir .tuide/ai/l2_overnight/hard_repeat \
  --cli build/l2_harness_cli
```

Salida: conteo `would_apply_*` / `blocked_opener_*` / `no_match` + JSONL
(`.tuide/ai/hunk_replay.jsonl` o `--jsonl ruta`).

Dry-run de un hunk suelto:

```bash
./build/l2_harness_cli hunk-try hunk.json   # {path, search, replace} o {text, search, replace}
```

## 3. Cómo leer el veredicto

| Veredicto | Significa |
|-----------|-----------|
| `would_apply_cli` / `would_apply_py` | El 7B ya había acertado el hunk; el runtime viejo lo rechazó. El MVP actual lo aplicaría. |
| `blocked_opener_struct` | `struct Foo {` → cuerpo (o archivo equivocado). **Bien** que siga bloqueado. |
| `blocked_opener` | Opener de función/namespace; revisar si el insert-rewrite debería haberlo salvado. |
| `no_match` | Search no está en el archivo (whitespace, path mal, o pack colapsado). |
| `missing_file` | Path del hunk no existe en este checkout. |

Anotar 1 línea por caso: *“este JSON del 7B ahora aplica / aún no y por qué”*.

## 4. Forensics extra (opcional)

```bash
python3 tools/l2_battery/score_facets.py \
  --cases tools/l2_battery/prompt_packs/cases_hard.json \
  --round-dir .tuide/ai/l2_phase_e_hard2
```

En cada caso: `run.log`, `state.json` (`edit_fail_count`, `coverage_gate_pushback`, `last_action`).

`l2_harness_cli` **sin** `run` (tampoco llama):

```bash
./build/l2_harness_cli bootstrap "…"
./build/l2_harness_cli plan src/util/shell_utils.hpp:command_exists
./build/l2_harness_cli edit hunks.json
```

Cuidado: `edit` **escribe** el árbol. Preferir `hunk-try` / replay.

## No hacer aquí

- `l2_harness_cli run` / `cases_hard` / prompt sweep (necesitan el 7B).
- Un 1.5B “por si acaso”: no es señal de producto.
- Dos rounds en paralelo en el mismo repo.

## Reparto

| Máquina 7B | Esta máquina |
|------------|----------------|
| Smoke/hard reales, una cola | Tests + replay de cada `search (failed)` nuevo |
| Criterio PASS de casos | “este JSON ahora aplica / aún no” |
