### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder para disparar un reinicio
keep: M2 M4
leído: on transport reader eof, schedule debounced lsp restart
Cerrado:
leído: on transport reader eof, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje LSP tras detectar que se ha caído
keep: M2 M5
leído: restart lsp for workspace, schedule debounced lsp restart, stop, start, spawn language server, on transport reader eof
Cerrado:
encontré el mecanismo de detección y reinicio: la detección es transport reader eof EOF del pipe stdout del servidor, pero este handler SOLO marca ready false y limpia el PID; NO dispara el reinicio. El reinicio es EXTERNO y REACTIVO se ejecuta desde application vía restart lsp for workspace llamado tras instalación LSP o en run o schedule debounced lsp restart tras cambios de config . No hay reinicio automático inmediato tras caída en el código leído; el sistema espera una acción de la aplicación o el usuario para llamar a restart lsp for workspace que a su vez llama a symbol provider workspace opened que internamente llama a start .
Abierto:
- on workspace opened

### Trabajo 3
consulta: qué hace schedule debounced lsp restart cuando se llama tras detectar que el servidor se ha caído
keep: M1 M2
leído: schedule debounced lsp restart, restart lsp for workspace, on transport reader eof
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio tras caída; lo leído on transport reader eof solo limpia estado local ready, waitpid y no invoca schedule debounced lsp restart ni ningún callback de reinicio
Abierto:
- on transport reader eof caller chain
- run loop lsp check


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, schedule debounced lsp restart
    extra: apply workspace settings, setup build environment watching

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, stop, start
    extra: on lsp missing install, on workspace opened

T3
    visto: schedule debounced lsp restart, restart lsp for workspace, on transport reader eof
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: on transport reader eof
  T1=>T3  mismo objeto: on transport reader eof
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → apply workspace settings
  T1 → setup build environment watching
  T2 → on lsp missing install
  T2 → on workspace opened
  T3 → apply workspace settings
  T3 → setup build environment watching
