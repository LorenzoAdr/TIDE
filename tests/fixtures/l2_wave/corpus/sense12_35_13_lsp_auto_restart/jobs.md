### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, clangd process alive
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída no respuesta del servidor LSP; lo leído clangd process alive es solo un stub placeholder en el archivo y los callers de reinicio run lsp missing install manejan arranque e instalación, no monitorización de fallo
Abierto:
- check process health
- on process exit
- check lsp health

### Trabajo 2
consulta: dónde se detecta que el proceso del servidor de lenguaje se ha caído o no responde para disparar el reinicio
keep: M5
leído: on transport reader eof, start
Cerrado:
leído: on transport reader eof, start
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se reinicia el servidor de lenguaje cuando se cierra la conexión de transporte
keep: M5
leído: on transport reader eof, start, restart lsp for workspace, run, on lsp missing install
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio por cierre de transporte; lo leído muestra que on transport reader eof solo limpia el pid y marca ready false sin disparar reinicio, y los callers de restart lsp for workspace son on lsp missing install éxito de instalación y run arranque inicial, no el cierre de transporte
Abierto:
- schedule debounced lsp restart
- on lsp notification


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T2
    visto: on transport reader eof, start
    extra: set notification handler, set reader eof handler

T3
    visto: on transport reader eof, start, restart lsp for workspace, run
    extra: set notification handler, set reader eof handler

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: on transport reader eof
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → set notification handler
  T2 → set reader eof handler
  T3 → set notification handler
  T3 → set reader eof handler
