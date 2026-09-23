### Trabajo 1
consulta: dónde se pintan las líneas rojas en el margen del editor para marcar errores
keep: M1
leído: diagnostic severity color, diagnostics for editor line, line gutter marker, handle gutter marker click
Cerrado:
leído: diagnostic severity color, diagnostics for editor line, line gutter marker, handle gutter marker click
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se parsean los errores de compilación de la consola para crear diagnósticos
keep: M7
leído: scan shell output for linker errors, make diagnostic modal
Cerrado:
leído: scan shell output for linker errors, make diagnostic modal
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: cómo se envían los diagnósticos creados desde la consola al editor para que se pinten en el margen
keep: M1
leído: diagnostics for editor line, diagnostics on line, rebuild diagnostics by line, rebuild diagnostics by line if changed, apply lsp diagnostics to panel, push active file diagnostics from cache
Cerrado:
leído: diagnostics for editor line, diagnostics on line, rebuild diagnostics by line, rebuild diagnostics by line if changed, apply lsp diagnostics to panel, push active file diagnostics from cache
Abierto:
- handle editor mouse
- handle gutter marker click


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostic severity color, diagnostics for editor line, line gutter marker, handle gutter marker click
    extra: Make Editor Panel, make diagnostic modal

T2
    visto: scan shell output for linker errors, make diagnostic modal
    extra: tick binary symbols panel, parse linker undefined reference

T3
    visto: diagnostics for editor line, diagnostics on line, rebuild diagnostics by line, rebuild diagnostics by line if changed
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  make diagnostic modal → diagnostic severity color
  T1=>T3  mismo objeto: diagnostics for editor line
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → tick binary symbols panel
  T2 → parse linker undefined reference
  T3 → Make Editor Panel
  T3 → Make Main Layout
