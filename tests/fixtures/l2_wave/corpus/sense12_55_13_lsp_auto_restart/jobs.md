### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M1 M4
leído: on transport reader eof, start, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: on transport reader eof, start, restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se reinicia el servidor LSP tras detectar que se ha caído o no responde
keep: M1
leído: restart lsp for workspace, on lsp missing install, stop, start, on transport reader eof
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio automático tras caída no-respuesta; lo leído es reinicio tras instalación de toolpack on lsp missing install y detección de EOF pasiva on transport reader eof que no dispara reinicio
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, start, restart lsp for workspace, schedule debounced lsp restart
    extra: set notification handler, set reader eof handler

T2
    visto: restart lsp for workspace, on lsp missing install, stop, start
    extra: on workspace opened, set workspace clangd options

entre abiertas:
  T1=>T2  mismo objeto: on transport reader eof
hacia el resto:
  T1 → set notification handler
  T1 → set reader eof handler
  T2 → on workspace opened
  T2 → set workspace clangd options
