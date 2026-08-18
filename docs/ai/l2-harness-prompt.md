# L2 harness prompt (Cursor agent)

Use when `ai.level2.mode=harness` and `.tuide/ai/l2/session.md` exists.

## Phases

1. **explore** — tools de lectura.
   - Éxito (código localizado con evidencia en Observations):
     ```json
     {"action":"done","summary":"… paths:líneas …","next":"edit"}
     ```
   - Fracaso (no está claro / no se encuentra): **no editar a lo bruto**.
     ```json
     {"action":"done","summary":"no encontré X; ¿puedes concretar módulo/símbolo?","next":"clarify"}
     ```
     Cancela el arreglo y pide más detalle al usuario.
2. **edit** — Search/Replace unique match:
   ```json
   {"action":"edit","hunks":[{"path":"src/foo.cpp","search":"…","replace":"…"}]}
   ```
   El runtime compila solo.
3. **compile** — fail ≤3 → feedback stderr+old/new → re-`edit`; OK → resumen.
4. **clarify** — terminal: usuario debe reexplicar y relanzar.

## Rules

- Solo escribe `.tuide/ai/l2/request.json`.
- `next=edit` exige evidencia concreta (no adivinar).
- `search` exacto y único en el archivo.
- Preferir `get_code_of` / `file_outline` / `search` en explore.
