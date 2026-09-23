### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se dispara el reinicio
keep: M2 M6
leído: restart lsp for workspace, set background paused, spawn language server
Cerrado:
leído: restart lsp for workspace, set background paused, spawn language server
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se programa el reinicio diferido del servidor LSP tras una caída
keep: M5 M7
leído: schedule debounced lsp restart, restart lsp for workspace, run
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el objeto de la consulta: el reinicio diferido se programa en schedule debounced lsp restart 786 789, que establece un deadline de 2 segundos lsp restart deadline y un latch pending lsp restart . El disparo tras caída no está en el código leído los callers visibles son cambios de configuración, pero el mecanismo de programación del delay sí está completo.
Abierto:
- lsp restart deadline
- pending lsp restart
- on lsp missing install
- detect lsp crash

### Trabajo 3
consulta: dónde se detecta que el servidor LSP se ha caído o no responde y se dispara el reinicio
keep: M2 M3 M4
leído: on transport reader eof, reader loop, start, schedule debounced lsp restart, notify lsp status
Cerrado:
leído: on transport reader eof, reader loop, start, schedule debounced lsp restart, notify lsp status
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, set background paused, spawn language server
    extra: on lsp missing install, on workspace opened

T2
    visto: schedule debounced lsp restart, restart lsp for workspace, run
    extra: apply workspace settings, setup build environment watching

T3
    visto: on transport reader eof, reader loop, start, schedule debounced lsp restart
    extra: open host pty, contains

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  sin camino
  T2=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  ensure backend started → reader loop
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → apply workspace settings
  T2 → setup build environment watching
  T3 → open host pty
  T3 → contains
