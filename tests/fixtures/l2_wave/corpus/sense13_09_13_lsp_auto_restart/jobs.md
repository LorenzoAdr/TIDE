### Trabajo 1
consulta: dónde se dispara el reinicio automático del servidor LSP cuando se cae o se congela
keep: M7
leído: schedule debounced lsp restart, apply workspace settings, setup build environment watching
Cerrado:
leído: schedule debounced lsp restart, apply workspace settings, setup build environment watching
Abierto:
- restart lsp for workspace

### Trabajo 2
consulta: dónde se detecta que el servidor LSP se ha caído o dejado de responder
keep: M2 M4
leído: on transport reader eof, reader loop, wait response
Cerrado:
encontré los dos mecanismos de detección: 1 Caída del socket reader loop detecta EOF en la lectura y ejecuta reader eof handler que apunta a transport reader eof que limpia el PID . 2 Dejado de responder timeout : wait response usa pending cv wait until con un deadline basado en timeout ms si expira, retorna false indicando que el servidor no respondió a tiempo.
Abierto:
- send request

### Trabajo 3
consulta: dónde se llama a schedule debounced lsp restart cuando el reader loop detecta EOF
keep: M2
leído: reader loop, on transport reader eof, start, set reader eof handler
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la llamada a schedule debounced lsp restart desde el reader loop o su handler de EOF; lo leído muestra que reader loop invoca reader eof handler asignado en start a on transport reader eof, pero on transport reader eof solo hace waitpid y no llama a schedule debounced lsp restart
Abierto:
- schedule debounced lsp restart caller from eof path


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: schedule debounced lsp restart, apply workspace settings, setup build environment watching
    extra: Workspace Config, restart lsp for workspace

T2
    visto: on transport reader eof, reader loop, wait response
    extra: open host pty, contains

T3
    visto: reader loop, on transport reader eof, start, set reader eof handler
    extra: open host pty, contains

entre abiertas:
  T1=>T2  apply workspace settings → setup build environment watching → reader loop
  T1=>T3  apply workspace settings → setup build environment watching → reader loop
  T2=>T3  mismo objeto: on transport reader eof
hacia el resto:
  T1 → Workspace Config
  T1 → restart lsp for workspace
  T2 → open host pty
  T2 → contains
  T3 → open host pty
  T3 → contains
