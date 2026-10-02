# L2 explore locate without LSP

**Estado:** normativo (P5)  
**Flag:** `L2_EXPLORE_PHASE_A`  
**Relacionado:** [`docs/plans/l2-explore-phase-a-b.md`](../plans/l2-explore-phase-a-b.md)

## Objetivo

Completar **explore_a → explore_b → pack** con el **índice estructural local** (Tree-sitter / `SymbolIndex` / `RepoMap` / `get_code_of` / `file_outline` / `search`) aunque **clangd u otros language servers estén down**.

Diagnostics LSP pueden seguir existiendo para feedback de edit/compile; **no** bloquean localización.

## Tools permitidos en `explore_a` / `explore_b` (Phase A on)

| Permitido (local) | Prohibido en locate (LSP / híbrido) |
|-------------------|--------------------------------------|
| `get_code_of` | `workspace_symbols` |
| `file_outline` | `hover` |
| `search` | `definition` |
| `repo_map` | `references` |
| `read_file` / `list_files` | `diagnostics` |
| `headers_of` / `sibling_of` | `context_pack` (puede pedir LSP) |
| `list_tools` | |

En `explore_a` el runtime **no** acepta tools del modelo: los peeks los inyecta el runtime vía `get_code_of`.

## Degradación

1. Sin LSP: peeks y pack usan TS + disco.
2. Sin `SymbolIndex` fresco: `get_code_of` / outline por path siguen leyendo el archivo.
3. `context_pack` no está en el hot path A/B; si se necesita, es fase edit o flag off.

## Criterio de aceptación

Test de integración: registry **sin** tools LSP → bootstrap Phase A → seed cola → `a_judge` → `a_done` → `apply_plan` desde watchlist → `pack.md` con fragmentos. Verde.
