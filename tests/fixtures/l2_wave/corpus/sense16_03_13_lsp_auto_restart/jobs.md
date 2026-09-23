### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde y se dispara el reinicio
keep: M1 M4
leído: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on lsp notification, reader loop, wait response, stop, start
Cerrado:
leído: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on lsp notification, reader loop, wait response, stop, start
Abierto:
- send request

### Trabajo 2
consulta: dónde se reinicia el servidor LSP tras un fallo o timeout
keep: M5 M4
leído: schedule debounced lsp restart, restart lsp for workspace, wait response, reader loop, send request, start, read message, stop
Cerrado:
encontré el mecanismo de reinicio tras timeout el fallo EOF cierre de fd se detecta en reader loop vía read message y dispara reader eof handler el timeout se detecta en wait response y retorna fallo al caller, pero no reinicia directamente el reinicio tras timeout depende de la lógica de negocio del caller, ej. lsp client no del transporte . El reinicio debounced se programa en schedule debounced lsp restart y ejecuta restart lsp for workspace .
Abierto:
- on lsp crash

### Trabajo 3
consulta: dónde se detecta el cierre inesperado del servidor LSP y se dispara el reinicio automático
keep: M8 M9
leído: on transport reader eof, restart lsp for workspace
Cerrado:
encontré el objeto de la consulta: la detección del cierre inesperado está en transport reader eof que limpia el estado ready y espera al proceso hijo y el reinicio automático se dispara desde restart lsp for workspace que reconfigura el workspace y reinicia el provider de símbolos . La cadena causal está anclada: el EOF del transport notifica al cliente, y la aplicación orquesta el reinicio.
Abierto:
- restart lsp for workspace


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on lsp notification
    extra: async worker main, completions at

T2
    visto: schedule debounced lsp restart, restart lsp for workspace, wait response, reader loop
    extra: apply workspace settings, setup build environment watching
    entre interno: restart lsp for workspace → send request

T3
    visto: on transport reader eof, restart lsp for workspace
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T3  sin camino
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → async worker main
  T1 → completions at
  T2 → apply workspace settings
  T2 → setup build environment watching
  T3 → on lsp missing install
  T3 → on workspace opened
