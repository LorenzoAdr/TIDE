### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5
leído: schedule debounced lsp restart, restart lsp for workspace, clangd process alive, clangd index healthy, process alive
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace, clangd process alive, clangd index healthy, process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se dispara el reinicio
keep: M2 M3
leído: on transport reader eof, start, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de no-respuesta timeout heartbeat ni el disparador de reinicio automático tras caída; lo leído es la detección de EOF caída física que solo limpia estado sin reiniciar, y funciones de reinicio manual debounced por configuración
Abierto:
- on lsp notification
- set reader eof handler

### Trabajo 3
consulta: dónde se detecta que el servidor de lenguaje LSP deja de responder y se dispara el reinicio automático
keep: M2 M7
leído: lsp client cpp, application cpp
Cerrado:
leído: lsp client cpp, application cpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: schedule debounced lsp restart, restart lsp for workspace, clangd process alive, clangd index healthy
    extra: setup build environment watching, apply workspace settings

T2
    visto: on transport reader eof, start, restart lsp for workspace, schedule debounced lsp restart
    extra: set notification handler, set reader eof handler

T3
    visto: lsp client cpp, application cpp

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → setup build environment watching
  T1 → apply workspace settings
  T2 → set notification handler
  T2 → set reader eof handler
