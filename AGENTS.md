# Contrato del proyecto — tuide

IDE de terminal en C++17 (FTXUI, Tree-sitter, LSP, DAP). Este fichero lo lee el
agente de IA de tuide en cada consulta: sé breve y cierto. Si algo deja de ser
verdad, córrígelo aquí antes de que el agente lo repita.

## Compilar y ejecutar

```bash
./compile.sh                       # build completo (envuelve tools/compile.sh)
cmake -S . -B build && make -C build -j4   # incremental, durante el desarrollo
make -C build -j4 <objetivo>       # un solo target (p.ej. tuide, l2_admin_test)
./build/tuide --cwd .              # arrancar sobre este repo
```

Las dependencias (FTXUI, cppdap, Tree-sitter) se bajan a
`~/.cache/tuide-fetch`. Si ese caché falta, `cmake` falla al configurar y hay
que pasar por `./compile.sh`, que las recupera.

## Tests

Son binarios sueltos en `build/`, sin ctest:

```bash
make -C build -j4 <nombre>_test && ./build/<nombre>_test
```

Cada uno imprime `… OK` o `FAIL: <qué>` y devuelve 1. **Córrelos desde la raíz
del repo**: varios buscan fixtures y `tools/l2_battery/features_promoted.json`
por ruta relativa, y desde `build/` cambian de comportamiento.

Hay tests en rojo que no son tuyos (`bracket_match`, `text_ops`,
`tree_sitter`, `git_diff`, `csv_index`, `nm_reader`, `tabular_file`,
`l2_trail_build`, `l2_wave`, `level2_session`, `level2_autonomous_loop`).
Antes de "arreglar" un rojo, comprueba con `git stash` si ya fallaba.

## Convenciones

- `src/` va a **2 espacios** de indentación y 100 columnas. El `.clang-format`
  del repo está desfasado (dice 4 y tabs): imita el archivo que estás editando.
- Identificadores en inglés; comentarios en español y explicando **por qué**,
  no qué. Un comentario que repite el código sobra.
- Mensajes de commit en inglés, imperativo, explicando el problema que resuelven.
- Nada de dependencias nuevas del sistema: la historia portable (`build-portable.sh`,
  binario con blobs) depende de no enlazar nada más. Si necesitas HTTP o TLS,
  mira cómo lo hace `src/ai/llm_client.cpp` (socket crudo + `curl` por fork).

## Zonas prohibidas

No edites: `third_party/`, `build/`, `.tuide/`, `.claude/worktrees/`,
`dist/`, `videos/`. Los `.claude/worktrees/*` son copias del propio repo y
contaminan cualquier búsqueda: si un grep devuelve algo de ahí, ignóralo.

## Mapa de la capa de IA

La ruta de producto es **`admin_v1`** (`src/ai/l2_admin.*` + `ai_controller`):
piloto, explorador, verificador y refutador, un JSON por turno. El transporte
al modelo es `src/ai/llm_client.*` (SSE, reintentos, `usage`, caps por
proveedor) y los backends por rol se configuran en `ai.roles.*`.

`level0_router`, `level1_agent`, `level2_session` y `level2_autonomous_loop`
son **legado fuera del hot path** (solo con `ai.admin_enabled=false`). No
inviertas ahí sin leer `docs/plans/ai-harness-v2-plan.md`.

Los planes vivos están en `docs/plans/`; los specs en `docs/ai/`.
