### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, notify lsp status
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, notify lsp status
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje LSP tras detectar que se ha caído
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, notify lsp status
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída del LSP que dispara el reinicio; lo leído son las funciones de reinicio restart lsp for workspace schedule debounced lsp restart y notificación de estado notify lsp status pero falta el código que detecta la muerte del proceso el peek de clangd process alive fue truncado y no mostró el cuerpo, y las agujas de detección como exit o process exited dieron hits 0
Abierto:
- finish python lsp start locked
- on process exit
- check alive
- waitpid handler

### Trabajo 3
consulta: dónde se detecta que el proceso del servidor LSP ha salido o muerto para disparar el reinicio
keep: M5
leído: clangd process alive
Cerrado:
leído: clangd process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se llama a restart lsp for workspace cuando el proceso LSP muere
keep: M5
leído: clangd process alive
Cerrado:
leído: clangd process alive
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, notify lsp status
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, notify lsp status
    extra: on lsp missing install, run level1 async

T3
    visto: clangd process alive
    extra: read file text, write terminal init script

T4
    visto: clangd process alive
    extra: read file text, write terminal init script

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: clangd process alive
  T1=>T4  mismo objeto: clangd process alive
  T2=>T3  mismo objeto: clangd process alive
  T2=>T4  mismo objeto: clangd process alive
  T3=>T4  mismo objeto: clangd process alive
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → on lsp missing install
  T2 → run level1 async
  T3 → read file text
  T3 → write terminal init script
