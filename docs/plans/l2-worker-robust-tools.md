# Fortalecer tools débiles (tras robustez 7/13)

Los briefs ya llegan al piloto. Fallan el **sensor** y el **bucle de tools**: una nota de árbitro se confunde con resultado de tool; cubre dice sí en trampas temáticas; `need_code` sin target cierra el informe.

## Qué no tocar

Piloto, survey, tally, `fn_card`, batería 20, hardcodear stems/símbolos de un caso.

## Tres palancas

1. **Notas de árbitro ≠ tool.** `## Notas` con `----- nota -----` no autoriza el informe de como/gap. Solo un chunk `----- follow|need_code|outline -----` cuenta. Si la tool vino vacía (sin target / sin hops), seguir pidiendo tool; no pasar a informe.
2. **Cubre-trampa.** El solape consulta↔why no debe usar el `brief` (el modelo copia tokens de la consulta). Nudge `no_cubre` si why+owns+stem no solapan. Prompt: tokens temáticos (IA, chat, panel) no son cubre.
3. **Reintentos de tool.** Mientras no haya follow/need_code ejecutado, no gastar el único nudge: repetir “emite tool con target” hasta el presupuesto.

## Medida

```bash
python3 tools/l2_worker_probe.py --pack robustness --cycle 2
```

Éxito: bajan `trap_lie` y `sin_tool`; `brief_ok` se mantiene; hit spinner/cancel/toolpacks no regresan.

## Ciclo 3 (toolpacks + como)

- No voltear `cubre` si la ficha solapa la consulta (`zone_ov > 0`): el brief ya citó el acto.
- Tras follow, si el informe pone `verdict=need_code`, repedir `chain|no_cubre`.

