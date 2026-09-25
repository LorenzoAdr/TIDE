# Administrador LLM único (chat)

Sustituye el hot path L0→L1→L2 del tab AI. El código legacy permanece compilado pero **no** se llama desde NL cuando `ai.admin_enabled=true` (default).

## Contrato `admin_v1`

```json
{"action":"admin_v1","do":"spawn|cerrar|editar|confirmar_editar|seguir_explorando|ask_user","why":"…",
 "spawn":{"tipo":"explore|build|git|shell|search|read|diagnostics|test|edit|web",
          "brief":"…","arg":"…","search":"…","replace":"…"},
 "cubre":"…","falta":"…","reply":"…"}
```

- **Sin plan multi-fase.** El “plan” es el **notebook** de sesión: cada spawn aporta paths/facts; el admin reacciona al inbox + notebook + UI.
- `explore` = **stub** (`no_concluyente`) mientras sense/wave se tunea. No llama a `l2_wave`. Hasta `kAdminMaxExplores` (3).
- `editar` / `cerrar` (con notebook): el runtime puede lanzar un **verificador adversarial**
  (N≤4 olas, tools `entre`/`read` anclados) que intenta refutar arcos A→B antes de aceptar.
  Entrada: consulta + anclas (símbolos/paths), **sin** narrativa del explorador ni `why` del piloto.
- `editar` no edita: pide el handoff a edición. El runtime exige una **segunda pasada** (`confirmar_editar` con `cubre`/`falta`, o `seguir_explorando` con brief del hueco, o `cerrar`).
- `edit` (spawn) exige path anclado + `search`/`replace` únicos **y** `edit_confirmed` (tras `confirmar_editar`).
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

## Relación con exploración (merge a un solo piloto)

`admin_v1` es el **piloto de producto** (chat). Wave `control_v1` y L2 autonomous son la misma función de “dirigir investigación” que divergió en harness/legado.

Estado congelado de búsqueda del hijo: **grep + read** (canal `rg --json`, paths anclados). Sense/tool-eval no añade más tools de caza.

Handoff explore→edit: `editar` abre confirmación; `confirmar_editar` (cubre/falta) habilita `spawn edit`; `seguir_explorando` relanza explore con el hueco. Sondas: `tools/l2_wave/probe_admin_exit.py`, `probe_admin_pilot.py`.

Pendiente de merge:
1. `spawn explore` deja de ser stub y lanza el hijo de caza (worker wave o lite grep+read) con `brief` = consulta.
2. Atomizar: un fenómeno por `explore` (hasta `kAdminMaxExplores`); el hijo cierra factual (`encontrado|no_encontrado|parcial`).
3. Dieta mínima del piloto: rol + orden + tipos spawn legales (sin pistas runtime ni sello sense en el hot path).
4. `control_v1` Full/sello queda solo en baterías Sense; el producto no lo reinyecta.

## Legacy

L0 (`level0_router`), L1 (`level1_agent`), L2 session/autonomous: en desuso en el chat. Documentación histórica: [l2-autonomous.md](l2-autonomous.md), [master-spec.md](master-spec.md). Wave `wave-explore --control` sigue para sense.
