# Admin battery — calidad harness + notebook + cadenas

## Qué mide (v3)

| Dimensión | Criterio |
|-----------|----------|
| Elección | `do` / `tipo` / `arg` gold |
| Inbox | `summary` no vacío tras spawn |
| Eco shell | `log_tail` con contenido real |
| Truncado | salidas largas → `truncated=1` + marcadores head/tail |
| Reply | no vacío; shell libre anclado a inbox |
| Notebook | acumulación de paths/facts (ronda 7+) |
| Cadena | `expect_chain` ordenado (ronda 8+) |

## Shell libre

Allowlist lectura/enumeración: `ls find pwd echo wc head tail cat file du which stat tree rg …`  
Sin `| > < ; & sudo rm curl`.

Runtime: `admin_run_shell_safe` + `admin_clip_output` (head 1200 + tail 1200).

## Ronda 6 (calidad + shell)

**13/13** pass. `shell_long`: truncated con head/tail.

## Ronda 7 (search/read + notebook)

**16/16** pass. `notebook_accum`: search→read → `notebook_n=2`.

## Shell tipado (paths/facts)

`admin_shell_enrich_result` rellena el notebook tras shell:

| cmd | paths | facts |
|-----|-------|-------|
| `ls`/`find` | dir + entradas unidas | `list:dir=… n≈…` |
| `head`/`tail`/`cat` | path del argv | `peek:path=… kind=…` + líneas |
| `wc` | path | `wc:path=… lines=N` |

Scorer cadena: `expect_notebook_path_any` / `expect_notebook_fact_any`.

## Web / internet

Spawn `web` (arg=query). Proveedores: stub / Brave / DDG.

Spawn `web_fetch` (arg=URL): solo si la URL está en el notebook (tras `web`).  
HTTP(S) público; deniega localhost/privado. Stub: `TUIDE_WEB_FETCH_STUB=1`  
(o automático si search stub). Facts: `fetch:url=` / `fetch:text=`.

Casos: `chain_web_research` (solo search), `chain_web_fetch` (search→fetch→cerrar).

## Ronda 8 — cadenas (dummy Cursor)

**10/10** pass (incl. web + web_fetch), con paths tipados en notebook.

| id | Cadena |
|----|--------|
| `chain_ls_head_version` | ls battery → head cases.json → version |
| `chain_ls_tail_do` | ls round_07 → tail notebook_accum → último do |
| `chain_find_wc_lines` | ls path → wc -l → líneas |
| `chain_two_heads_docs` | ls docs/ai → head ×2 → resumen |
| `chain_ls_then_search` | ls *admin* → search AdminSessionUi |
| `chain_git_then_ls_tuide` | git status → ls .tuide |
| `chain_discover_head_then_read` | ls → head hpp → read |
| `chain_long_then_decide` | ls /usr/bin (trunc) → head battery → decidir |
| `chain_web_research` | web (internet) → citar URL |
| `chain_web_fetch` | web → web_fetch (URL anclada) → cerrar |

```bash
python3 tools/l2_wave/admin_battery.py --round 8 \
  --cases tests/fixtures/l2_admin/battery/cases_chain.json \
  --out .tuide/ai/l2_admin_battery/round_08
```

Sin `--script` en `admin-run` (modo LLM real) se puede reusar el mismo `prompt` de cada caso para comparar reacción frente al dummy.

Histórico: `round_01`…`07` en fixtures; score en `.tuide/ai/l2_admin_battery/`.
