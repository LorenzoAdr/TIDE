### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
Abierto:
- run custom event drain

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, process build environment updates, configure bash language server
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, process build environment updates, configure bash language server
Abierto:
- run custom event drain
- finish bash lsp start locked

### Trabajo 3
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run, process build environment updates
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de caída no-respuesta del servidor LSP; lo leído es el reinicio tras instalación on lsp missing install y tras cambio de entorno process build environment updates, pero no hay código que detecte un crash o timeout del proceso clangd
Abierto:
- run custom event drain
- clangd process monitor
- lsp server exit handler
- clangd heartbeat


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, process build environment updates, configure bash language server
    extra: on lsp missing install, on workspace opened

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
    extra: on workspace opened, set workspace clangd options

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → on workspace opened
  T3 → set workspace clangd options
