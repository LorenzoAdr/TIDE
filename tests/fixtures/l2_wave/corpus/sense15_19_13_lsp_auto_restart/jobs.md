### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder para disparar un reinicio
keep: M5 M10
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp notification, spawn language server
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp notification, spawn language server
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor LSP ha dejado de responder o se ha caído para disparar el reinicio
keep: M1 M6
leído: on lsp notification, completion wait timeout ms, parse wait timeout ms, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: on lsp notification, completion wait timeout ms, parse wait timeout ms, restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se dispara el reinicio del servidor LSP cuando se agota el tiempo de espera de una petición
keep: M1 M10
leído: schedule debounced lsp restart
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el disparo del reinicio por timeout de petición; lo leído es un debounce de 2s tras cambios de configuración setup build environment watching, no un timeout de respuesta LSP
Abierto:
- wait for document ready
- sync document and wait
- wait fd readable


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp notification, spawn language server
    extra: on lsp missing install, on workspace opened

T2
    visto: on lsp notification, completion wait timeout ms, parse wait timeout ms, restart lsp for workspace
    extra: is lsp trackable path, normalize lsp path

T3
    visto: schedule debounced lsp restart
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T2  spawn language server → initialize → did open → generation → parse wait timeout ms
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → is lsp trackable path
  T2 → normalize lsp path
  T3 → apply workspace settings
  T3 → setup build environment watching
