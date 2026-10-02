### Trabajo 1
consulta: cómo se procesan los errores de compilación del shell para marcarlos en el editor
keep: M5 M3
leído: scan shell output for linker errors, parse linker undefined reference, request binary symbols panel, tick binary symbols panel
Cerrado:
leído: scan shell output for linker errors, parse linker undefined reference, request binary symbols panel, tick binary symbols panel
Abierto:
- handle context menu keys

### Trabajo 2
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
keep: M2
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line, apply lsp diagnostics to panel, push active file diagnostics from cache
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el dibujo de la línea roja; lo leído es la construcción de datos rebuild diagnostics by line y la invalidación de caché push active file diagnostics from cache, pero no el renderizado del gutter
Abierto:
- draw diagnostic gutter
- render line decorations
- gutter paint

### Trabajo 3
consulta: dónde se pinta la línea roja en el margen izquierdo del editor para marcar errores
keep: M1 M6
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se pinta la línea roja en el margen izquierdo del editor para marcar errores
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: scan shell output for linker errors, parse linker undefined reference, request binary symbols panel, tick binary symbols panel
    extra: has value, Symbol Filter Runner

T2
    visto: rebuild diagnostics by line if changed, rebuild diagnostics by line, apply lsp diagnostics to panel, push active file diagnostics from cache
    extra: ISymbol Provider, panel diagnostics match doc

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → has value
  T1 → Symbol Filter Runner
  T2 → ISymbol Provider
  T2 → panel diagnostics match doc
