# L2 harness prompt (Cursor agent)

Use when `ai.level2.mode=harness` and `.tuide/ai/l2/session.md` exists.

## Phases

1. **explore_a** (default — `L2_EXPLORE_PHASE_A` promoted) — localización.
   - El runtime inyecta peeks efímeros desde la cola del mapa.
   - Juzga:
     ```json
     {"action":"a_judge","verdicts":[{"target":"…","verdict":"useful|reject|uncertain",
      "anchor":"path:Symbol","stem":"…","role":"primary","why":"…"}],"done":false}
     ```
   - Cierra loci (1–2 primary):
     ```json
     {"action":"a_done","loci":[{"stem":"…","anchor":"path:Symbol","role":"primary","why":"…"}],
      "summary":"…"}
     ```
   - **PROHIBIDO** `plan` / tools / `pack.md` / `done next=edit` en esta fase.

2. **explore_b** — acumulación desde loci.
   - Preferir `{"action":"plan","targets":[]}` (watchlist = loci) o targets de loci.
   - Éxito:
     ```json
     {"action":"done","summary":"… paths:líneas …","next":"edit"}
     ```
   - Fracaso:
     ```json
     {"action":"done","summary":"no encontré X; ¿puedes concretar módulo/símbolo?","next":"clarify"}
     ```

3. **edit** — Search/Replace unique match:
   ```json
   {"action":"edit","hunks":[{"path":"src/foo.cpp","search":"…","replace":"…"}]}
   ```
   El runtime compila solo.

4. **compile** — fail ≤3 → feedback stderr+old/new → re-`edit`; OK → resumen.

5. **clarify** — terminal: usuario debe reexplicar y relanzar.

Override: `L2_FEAT_L2_EXPLORE_PHASE_A=0` restaura explore mezclado (plan temprano + tools).

## Rules

- Solo escribe `.tuide/ai/l2/request.json`.
- `next=edit` exige evidencia concreta (no adivinar).
- `search` exacto y único en el archivo.
- En explore_a no uses `get_code_of` libre: peeks los pone el runtime.
- En explore_b preferir pack desde loci; sin caza libre multi-stem.
