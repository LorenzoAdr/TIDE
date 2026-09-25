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
- `explore` = hijo **lite grep+read** (`admin_run_explore_lite`): el brain caza con
  `grep`/`read`/`cerrar`. Por ola admite **varios** `patterns[]` (≤3) y **varios**
  `paths[]` (≤2, solo anclados a hits de grep de ese explore). Totales ≤4 greps / ≤3 reads.
  Veredicto tipado `encontrado|no_encontrado|parcial`. Hasta `kAdminMaxExplores` (4).
  Stub solo si no hay brain (tests). `read` acepta `path`, `path:N`, `path:N:M`,
  `path:N-M`, `path:N-M,a-b` y `path:Class::method`; explore omite re-head del mismo path.
- `editar` / `cerrar` (con notebook): **verificador adversarial** (N≤4 + contra-pregunta
  si hay miss tipados) y, si aún `sostiene`, un **pase refutador** LLM. Sin reescritura
  heurística de dominio. Tope de pases → `dudoso` (bloquea; no absuelve).
  Entrada: consulta + anclas + veredictos tipados (explore/read/search) + `evidencia`
  path:línea + hechos del notebook + extractos de `log_tail` en read/search.
  **Sin** tesis/prosa del piloto ni del explorador. Summaries completos del job
  (≤`kAdminSummaryChars`); paths legibles también desde evidencia.
- `editar` no edita: pide el handoff a edición. El runtime exige una **segunda pasada** (`confirmar_editar` con `cubre`/`falta`, o `seguir_explorando` con brief del hueco, o `cerrar`).
- `edit` (spawn) exige path anclado + `search`/`replace` únicos **y** `edit_confirmed` (tras `confirmar_editar`).
- `web` = búsqueda en internet (Brave/DDG/stub); distinto de `search` (rg en repo).
- `ask_user`: el piloto pregunta; el lazo **pausa** y muestra la pregunta en el panel AI.
  La siguiente línea NL del usuario es la respuesta: se guarda en el diálogo, se reabre el
  lazo con la **consulta original** intacta y el notebook previo.
- Al **arrancar** la app se limpia `.tuide/ai/l2_admin/` (salvo `ask_user` pendiente),
  para no arrastrar la investigación de la ejecución anterior. Durante la misma
  ejecución **no** se limpia al cambiar de consulta: notebook + episodios se
  conservan, pero el **presupuesto** (proposes/spawns/explores/verify) se renueva
  por consulta. Si el cupo de explore se agota mid-consulta, el runtime degrada
  nuevos `explore` a `search` y el prompt prohíbe más exploradores. Solo un
  Reset/borrar del panel AI (o `/new`) limpia a mano.
- Abrir el panel AI **no** arranca mapa de símbolos ni embeds de stems (eso era L0/L1).
  Con `ai.admin_enabled=false` vuelve el warm legacy.

## Contexto de sesión

Estado en `.tuide/ai/l2_admin/`:

| Artefacto | Rol |
|-----------|-----|
| `state.json` | consulta, jobs, notebook, episodios, UI snapshot, reply |
| `notebook.md` | evidencias legibles (paths, facts) |

UI capturada al entrar: `active_path`, `cursor_line`, selección (clip). Continuable si hay
`clarify`, sesión abierta, notebook o episodios. Follow-up NL conserva notebook + episodios
y reinicia presupuestos (proposes/spawns/verify).

## WORKSPACE_ROOT (perímetro)

El proyecto abierto en TIDE (`workspace.root`) es el **único árbol** que el admin puede
tocar. Va explícito en el prompt del piloto (`## WORKSPACE_ROOT`) y en el del explore.

El runtime rechaza:
- `read` / `edit` con `..` o absolutos fuera del root
- `shell` con `..`, `cd /`/`~`, o tokens absolutos ajenos
- hits de search bajo `.tuide/` y `build/` (ruido de sesión)

Hoy en desarrollo el root suele ser el propio repo de TIDE; mañana será otro proyecto —
el modelo no debe asumir “soy el código de TIDE”, sino “analizo lo que hay bajo este root”.

## Uso en la app

1. `/backend remote` (o `local`) si el mode legacy es `dry_run`.
2. Escribe NL en el tab AI → relato de investigación (`→ Piloto…`, `· buscar/leer…`).
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
1. Atomizar: un fenómeno por `explore`; el hijo ya cierra factual (`encontrado|no_encontrado|parcial`).
2. Dieta mínima del piloto: rol + orden + tipos spawn legales (sin pistas runtime ni sello sense en el hot path).
3. `control_v1` Full/sello queda solo en baterías Sense; el producto no lo reinyecta.

## Legacy

L0 (`level0_router`), L1 (`level1_agent`), L2 session/autonomous: en desuso en el chat. Documentación histórica: [l2-autonomous.md](l2-autonomous.md), [master-spec.md](master-spec.md). Wave `wave-explore --control` sigue para sense.
