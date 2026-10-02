### Trabajo 1
consulta: dónde se aplican los resaltados visuales en el margen del editor para marcar errores
keep: M1 M11
leído: apply visual highlight fold regions, compute, drain visual highlight results, show diagnostic suffix on line, rebuild diagnostic suffix cache, active diagnostic suffix map, apply lsp diagnostics to panel, diagnostics on line
Cerrado:
encontré el mecanismo de aplicación: los resaltados de error en el margen se aplican como sufijos de texto en el gutter mediante rebuild diagnostic suffix cache en editor panel que construye strings iconos códigos por línea y los almacena en diagnostic suffix by line estos se consultan en show diagnostic suffix line y se renderizan en el dibujo del gutter del editor. El sistema visual highlight no maneja errores, solo folds brackets.
Abierto:
- handle editor mouse
- draw gutter line

### Trabajo 2
consulta: dónde se parsean los errores de compilación de la consola para alimentar el sistema de diagnósticos del editor
keep: M1 M2
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que parsea errores de compilación de la consola para alimentar diagnósticos; lo leído es un parser específico de errores de linker scan shell output for linker errors que alimenta el panel de símbolos binarios, no el sistema de diagnósticos del editor que usa LSP .
Abierto:
- parse compiler errors from shell
- shell to lsp diagnostics

### Trabajo 3
consulta: dónde se construye el mapa de sufijos de diagnóstico por línea para pintar el margen del editor
keep: M1 M11
leído: rebuild diagnostic suffix cache
Cerrado:
leído: rebuild diagnostic suffix cache
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: apply visual highlight fold regions, compute, drain visual highlight results, show diagnostic suffix on line
    extra: Make Editor Panel, Make Main Layout

T2
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, parse linker undefined reference

T3
    visto: rebuild diagnostic suffix cache
    extra: apply lsp diagnostics to panel, push active file diagnostics from cache

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: rebuild diagnostic suffix cache
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → tick binary symbols panel
  T2 → parse linker undefined reference
  T3 → push active file diagnostics from cache
  T3 → ISymbol Provider
