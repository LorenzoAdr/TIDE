### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder para disparar un reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder para disparar un reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de caída o timeout del servidor LSP; lo leído son solo los efectos de reinicio restart lsp for workspace, schedule debounced lsp restart y el disparador de instalación faltante on lsp missing install, pero no la lógica que detecta que el servidor ha caído o dejado de responder
Abierto:
- lsp server process monitor
- clangd connection status
- lsp ping pong
- check alive

### Trabajo 3
consulta: dónde se monitoriza el estado del proceso LSP para detectar si ha caído o dejado de responder
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se monitoriza el estado del proceso LSP para detectar si ha caído o dejado de responder
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: run level1 async, on workspace opened

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → run level1 async
  T2 → on workspace opened
