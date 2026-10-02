### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
keep: M1
leído: clear editor line paint caches, editor panel cpp, line gutter marker
Cerrado:
leído: clear editor line paint caches, editor panel cpp, line gutter marker
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola para marcarlos en el editor
keep: M1 M11
leído: diagnostics for translation unit, diagnostics on line, diagnostic severity tr
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, amplia un hop; no refutes el claim entero.
no encontré el mecanismo que lee errores de compilación de la consola para marcarlos en el editor; lo leído confirma que el editor usa diagnostics de LSP diagnostics cpp y que el panel de símbolos binarios solo escanea errores de linker de la shell scan shell output for linker errors, pero no existe un puente que convierta la salida de compilación gcc clang en diagnostics para el editor.
Abierto:
- handle editor mouse
- compiler output to diagnostics
- parse compiler errors
- shell to editor diagnostics


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: clear editor line paint caches, editor panel cpp, line gutter marker
    extra: Make Editor Panel, Make Main Layout

T2
    visto: diagnostics for translation unit, diagnostics on line, diagnostic severity tr
    extra: Make Main Layout, build include tree

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → Make Main Layout
  T2 → build include tree
