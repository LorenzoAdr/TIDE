# Rúbrica Cursor — mechanism pack

Puntúa cada dossier de caso (1–5). No inventes aristas: solo lo visible en `dossier.md` / JSON.

| Campo | 1 | 3 | 5 |
|-------|---|---|---|
| `story_clarity` | No se ve trigger→state→effect | Relato parcial / un slot flojo | Mecanismo legible sin inventar |
| `falsifiability` | Todo mezclado; no se puede rechazar un brazo | Se intuye separación | Se podría reject un slot sin tumbar la zona |
| `noise` | Mini-cards/hubs ahogan el mechanism (5=mucho ruido) | Ruido moderado | Casi nada compite con mechanism (1=limpio) — **invertir al agregar** |
| `ports_useful` | Ports vacíos o irrelevantes | Algún enlace multi-zona | Ports ayudan a componer hipótesis |

`noise` en el JSON se reporta **crudo 1–5 donde 5 = mucho ruido**. En agregados del STATUS se usa `noise_inv = 6 - noise`.

## Salida por caso (JSON)

```json
{
  "id": "case_id",
  "story_clarity": 4,
  "falsifiability": 3,
  "noise": 2,
  "ports_useful": 3,
  "verdict": "keep_knobs",
  "notes": "Máximo 2 frases citando slots/edges del dossier."
}
```

`verdict`: `keep_knobs` | `tune` | `revert_direction`

## Agregado de ronda

Escribir `cursor_scores_tN.json` con `{ "rows": [ ... ], "summary": { "mean_story_clarity", "mean_noise", "mean_falsifiability", "mean_ports_useful", "tune_count", "worst3": [...] } }`.
