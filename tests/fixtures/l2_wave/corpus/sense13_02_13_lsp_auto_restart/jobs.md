### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
keep: M7
leído: wait until healthy, status text, health ok, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de caída no-respuesta del servidor de lenguaje LSP para disparar reinicio; lo leído es la lógica de salud del backend L2 llama backend y el reinicio manual debounced del LSP clangd tras cambios de configuración, pero no hay un watchdog que detecte caída del LSP y dispare restart lsp for workspace automáticamente
Abierto:
- start server
- on connection lost
- on error

### Trabajo 2
consulta: dónde se maneja la desconexión o el error del servidor de lenguaje para intentar reconectar
keep: M9
leído: schedule debounced lsp restart
Cerrado:
leído: schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se inicia el servidor de lenguaje y se establece la conexión inicial
keep: M7
leído: spawn language server, start, initialize
Cerrado:
encontré dónde se inicia el servidor y se establece la conexión: start llama a spawn language server que hace fork exec y devuelve transport start para el transporte, luego llama a initialize que envía la request initialize LSP y la notificación initialized
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: wait until healthy, status text, health ok, restart lsp for workspace
    extra: ensure ready, start server

T2
    visto: schedule debounced lsp restart
    extra: apply workspace settings, setup build environment watching

T3
    visto: spawn language server, start, initialize
    extra: c str, set notification handler

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → ensure ready
  T1 → start server
  T2 → apply workspace settings
  T2 → setup build environment watching
  T3 → c str
  T3 → set notification handler
