### Trabajo 1
consulta: dónde se leen los errores de compilación del shell para marcarlos en el editor
keep: M4
leído: scan shell output for linker errors, parse linker undefined reference, request binary symbols panel
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que lee errores de compilación gcc clang del shell; lo leído es exclusivamente el parser de errores de enlace linker en binary symbols panel que busca símbolos indefinidos y abre un panel de símbolos, no marcas en el editor.
Abierto:
- handle context menu keys
- parse compile errors
- diagnostics from shell
- apply diagnostics

### Trabajo 2
consulta: dónde se parsean los errores de compilación del shell para generar diagnósticos
keep: M10
leído: parse gfortran stderr
Cerrado:
leído: parse gfortran stderr
Abierto:
- run gfortran diagnostics

### Trabajo 3
consulta: dónde se aplican los diagnósticos de compilación al margen del editor para pintar la línea roja
keep: M1 M5
leído: apply lsp diagnostics to panel, diagnostics for editor line, show diagnostic suffix on line, rebuild diagnostic suffix cache, line gutter marker, line diagnostic marker from map, handle gutter marker click
Cerrado:
leído: apply lsp diagnostics to panel, diagnostics for editor line, show diagnostic suffix on line, rebuild diagnostic suffix cache, line gutter marker, line diagnostic marker from map, handle gutter marker click
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: scan shell output for linker errors, parse linker undefined reference, request binary symbols panel
    extra: tick binary symbols panel, has value

T2
    visto: parse gfortran stderr
    extra: refresh fortran compiler diagnostics, flush pending did change for key

T3
    visto: apply lsp diagnostics to panel, diagnostics for editor line, show diagnostic suffix on line, rebuild diagnostic suffix cache
    extra: ISymbol Provider, allows lsp ui

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → tick binary symbols panel
  T1 → has value
  T2 → refresh fortran compiler diagnostics
  T2 → flush pending did change for key
  T3 → ISymbol Provider
  T3 → allows lsp ui
