### Trabajo 1
consulta: dónde se marca la línea roja en el margen izquierdo del editor para errores de compilación
keep: M1 M4
leído: diagnostic severity tr, diagnostics on line, diagnostics for translation unit, line gutter marker, handle gutter marker click, line diagnostic marker from map, diagnostics for editor line
Cerrado:
leído: diagnostic severity tr, diagnostics on line, diagnostics for translation unit, line gutter marker, handle gutter marker click, line diagnostic marker from map, diagnostics for editor line
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola para crear diagnósticos
keep: M10
leído: run gfortran diagnostics, parse gfortran stderr
Cerrado:
encontré el mecanismo: parse gfortran stderr lee la salida stderr del compilador no la consola UI línea a línea usando regex para extraer ubicación y mensaje, creando objetos Diagnostic; run gfortran diagnostics orquesta la escritura temporal y la invocación del compilador
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se actualiza la lista de diagnósticos del editor al terminar la compilación
keep: M1 M2
leído: apply lsp diagnostics to panel, sync diagnostic cache, ISymbol Provider, diagnostics revision, client diagnostics revision, refresh diagnostics cache locked, diagnostics for file
Cerrado:
leído: apply lsp diagnostics to panel, sync diagnostic cache, ISymbol Provider, diagnostics revision, client diagnostics revision, refresh diagnostics cache locked, diagnostics for file
Abierto:
- append client diagnostics


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostic severity tr, diagnostics on line, diagnostics for translation unit, line gutter marker
    extra: Make Editor Panel, make diagnostic modal

T2
    visto: run gfortran diagnostics, parse gfortran stderr
    extra: refresh fortran compiler diagnostics, set ui inhibited

T3
    visto: apply lsp diagnostics to panel, sync diagnostic cache, ISymbol Provider, diagnostics revision
    extra: allows lsp ui, diagnostics reveal allowed

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → make diagnostic modal
  T2 → refresh fortran compiler diagnostics
  T2 → set ui inhibited
  T3 → allows lsp ui
  T3 → diagnostics reveal allowed
