### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se inicia el reinicio del servidor LSP tras detectar que el proceso ha muerto
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: quién llama a la función de reinicio del LSP cuando se detecta que el proceso ha muerto
keep: M5
leído: clangd process alive
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré el mecanismo de detección de muerte del proceso que dispara el reinicio; lo leído en clangd process alive es un cuerpo truncado que no muestra la lógica de detección ni el disparo, y los callers de restart lsp for workspace son lsp missing install instalación y setup build environment watching configuración, no la detección de muerte.
Abierto:
- clangd process alive body
- on process exit
- check process alive

### Trabajo 4
consulta: dónde se registra el listener de salida del proceso LSP para detectar que ha muerto
keep: M5
leído: spawn clangd, spawn language server, on transport reader eof, transport running
Cerrado:
leído: spawn clangd, spawn language server, on transport reader eof, transport running
Abierto:
- start async worker


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
    extra: on lsp missing install, on workspace opened

T3
    visto: clangd process alive, restart lsp for workspace, schedule debounced lsp restart
    extra: read file text, english strings

T4
    visto: spawn clangd, spawn language server, on transport reader eof, transport running
    extra: make clangd spec, has value

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T1=>T4  sin camino
  T2=>T3  mismo objeto: restart lsp for workspace
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → on lsp missing install
  T2 → on workspace opened
  T3 → read file text
  T3 → english strings
