# Administrador LLM único (chat)

Sustituye el hot path L0→L1→L2 del tab AI. El código legacy permanece compilado pero **no** se llama desde NL cuando `ai.admin_enabled=true` (default).

## Contrato `admin_v1`

```json
{"action":"admin_v1","do":"spawn|cerrar|ask_user","why":"…",
 "spawn":{"tipo":"explore|build|git|shell|search|read|diagnostics|test|edit|web",
          "brief":"…","arg":"…","search":"…","replace":"…"},
 "reply":"…"}
```

- **Sin plan multi-fase.** El “plan” es el **notebook** de sesión: cada spawn aporta paths/facts; el admin reacciona al inbox + notebook + UI.
- `explore` = **stub** (`no_concluyente`) mientras sense/wave se tunea. No llama a `l2_wave`.
- `edit` exige path anclado (notebook o archivo activo en UI) + `search`/`replace` únicos.
- `web` = búsqueda en internet (Brave/DDG/stub); distinto de `search` (rg en repo).
- `web_fetch` = cuerpo de una URL **ya en el notebook** (tras `web`).

## Contexto de sesión

Estado en `.tuide/ai/l2_admin/`:

| Artefacto | Rol |
|-----------|-----|
| `state.json` | consulta, jobs, notebook, UI snapshot, reply |
| `notebook.md` | evidencias legibles (paths, facts) |

UI capturada al entrar: `active_path`, `cursor_line`, selección (clip). Follow-up NL reabre la sesión y conserva el notebook.

## Uso en la app

1. `/backend remote` (o `local`) si el mode legacy es `dry_run`.
2. Escribe NL en el tab AI → `Admin ▸ …`
3. `/build`, `/git`, `/cancel`, `/new` siguen como atajos.
4. Spawns reales: search/read (tools o `rg`/FS), diagnostics/test (tools/TaskRunner), edit (`apply_hunk_to_workspace_file`), git/build/shell como antes.

`ai.admin_enabled=false` restaura L0→L1→L2.

## Harness (paralelo a sense)

```bash
./build/l2_harness_cli admin-run --out /tmp/admin1 --prompt "busca AdminState" \
  --script tests/fixtures/l2_admin/script_git_cerrar.txt

# Batería (incluye notebook + search/read):
python3 tools/l2_wave/admin_battery.py --round 7 \
  --out .tuide/ai/l2_admin_battery/round_07
```

Ver [l2-admin-battery.md](l2-admin-battery.md).

No modifica `wave-explore` ni escribe en `.tuide/ai/l2_wave/`.

## Relación con exploración

La batería sense y `l2_harness_cli wave-explore` son independientes. Enchufar explore real al admin es una PR futura, cuando el tuning de olas esté cerrado.

## Legacy

L0 (`level0_router`), L1 (`level1_agent`), L2 session/autonomous: en desuso en el chat. Documentación histórica: [l2-autonomous.md](l2-autonomous.md), [master-spec.md](master-spec.md).
