### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M3 M8
leído: restart lsp for workspace, wait response, send lsp request
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré un detector activo de caída timeout que dispare el reinicio; lo leído muestra que el reinicio es manual desde on lsp missing install y que la detección de fallo timeout en wait response solo devuelve false sin lógica de recuperación automática
Abierto:
- send request
- handle response
- on error
- check alive

### Trabajo 2
consulta: dónde se reinicia el servidor LSP tras un fallo de conexión o timeout
keep: M4 M5
leído: restart lsp for workspace, schedule debounced lsp restart, wait response, wait fd readable, read message
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, wait response, wait fd readable, read message
Abierto:
- send request


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, wait response, send lsp request
    extra: on lsp missing install, on workspace opened
    entre interno: restart lsp for workspace → send lsp request

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, wait response, wait fd readable
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → on lsp missing install
  T2 → on workspace opened
