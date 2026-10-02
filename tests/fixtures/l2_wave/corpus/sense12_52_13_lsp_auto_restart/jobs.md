### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o no responde para disparar el reinicio
keep: M3
leído: start, stop, on transport reader eof
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de caída no-respuesta para disparar el reinicio; lo leído muestra que on transport reader eof solo limpia estado ready false, waitpid sin llamar a start ni a ningún método de reinicio, y stop es una parada manual que no reinicia
Abierto:
- on lsp notification
- spawn language server

### Trabajo 2
consulta: dónde se programa el reinicio del servidor LSP tras detectar que ha caído
keep: M4
leído: lsp client cpp, restart lsp for workspace
Cerrado:
leído: lsp client cpp, restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se llama a schedule debounced lsp restart cuando el servidor deja de responder
keep: M1
leído: schedule debounced lsp restart, restart lsp for workspace
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: start, stop, on transport reader eof
    extra: set notification handler, set reader eof handler

T2
    visto: lsp client cpp, restart lsp for workspace
    extra: on lsp missing install, on workspace opened

T3
    visto: schedule debounced lsp restart, restart lsp for workspace
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → set notification handler
  T1 → set reader eof handler
  T2 → on lsp missing install
  T2 → on workspace opened
  T3 → apply workspace settings
  T3 → setup build environment watching
