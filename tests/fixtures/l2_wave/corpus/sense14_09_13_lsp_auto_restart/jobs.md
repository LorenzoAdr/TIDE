### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde y se programa su reinicio
keep: M1 M2
leído: restart lsp for workspace, start, on transport reader eof
Cerrado:
leído: restart lsp for workspace, start, on transport reader eof
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: cómo se reinicia el servidor LSP automáticamente tras detectar que se ha caído o no responde
keep: M3 M6
leído: on transport reader eof, stop, start, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré un mecanismo de reinicio automático tras caída o no-respuesta; lo leído muestra que on transport reader eof solo limpia estado ready false sin relanzar, y las funciones de restart son invocadas manualmente o por cambios de configuración, no por el detector de fallos
Abierto:
- on transport reader eof
- health check
- ping handler

### Trabajo 3
consulta: dónde se detecta que el servidor LSP deja de responder y se programa su reinicio
keep: M1 M2
leído: schedule debounced lsp restart, stop, restart lsp for workspace, run
Cerrado:
leído: schedule debounced lsp restart, stop, restart lsp for workspace, run
Abierto:
- ensure backend started


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, start, on transport reader eof
    extra: on lsp missing install, on workspace opened

T2
    visto: on transport reader eof, stop, start, restart lsp for workspace
    extra: request start, Workspace Indexer

T3
    visto: schedule debounced lsp restart, stop, restart lsp for workspace, run
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: stop
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → request start
  T2 → Workspace Indexer
  T3 → apply workspace settings
  T3 → setup build environment watching
