### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o no responde para disparar el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se llama a reiniciar el LSP cuando se detecta que el proceso no está vivo
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se inicia la detección automática de la caída del LSP al abrir el workspace
keep: M5
leído: set workspace, restart lsp for workspace, schedule debounced lsp restart, clangd process alive, run, on workspace opened
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el objeto de la consulta: la detección automática de la caída del LSP al abrir el workspace se inicia en run vía lsp missing install que llama a restart lsp for workspace . Esta función ejecuta symbol provider workspace opened que es el punto de entrada para la inicialización detección del LSP en el contexto del workspace abierto. No hay un heartbeat explícito en clangd process alive vinculado al evento de apertura, sino un reinicio arranque condicional.
Abierto:
- on lsp missing install

### Trabajo 4
consulta: dónde se detecta que el proceso del servidor LSP se ha caído o dejado de responder durante la ejecución
keep: M5
leído: clangd process alive
Cerrado:
leído: clangd process alive
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
    extra: on lsp missing install, on workspace opened

T3
    visto: set workspace, restart lsp for workspace, schedule debounced lsp restart, clangd process alive
    extra: Application, filename

T4
    visto: clangd process alive
    extra: read file text, refresh fortran compiler diagnostics

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T1=>T4  mismo objeto: clangd process alive
  T2=>T3  mismo objeto: restart lsp for workspace
  T2=>T4  mismo objeto: clangd process alive
  T3=>T4  mismo objeto: clangd process alive
hacia el resto:
  T1 → on lsp missing install
  T1 → set workspace clangd options
  T2 → on lsp missing install
  T2 → set workspace clangd options
  T3 → Application
  T3 → filename
