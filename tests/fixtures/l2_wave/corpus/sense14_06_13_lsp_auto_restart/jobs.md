### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde para disparar el reinicio
keep: M2 M3
leído: on transport reader eof, schedule debounced lsp restart, restart lsp for workspace, start
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de no respuesta timeout heartbeat ni el disparo de reinicio desde lsp client; lo leído muestra que la detección de caída es solo vía EOF on transport reader eof y el reinicio se dispara desde application cpp por cambios de configuración o arranque, no por fallo del servidor
Abierto:
- on transport reader eof → schedule debounced lsp restart
- timeout handler

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP deja de responder para disparar el reinicio
keep: M2 M3
leído: completion wait timeout ms, wait response, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: completion wait timeout ms, wait response, restart lsp for workspace, schedule debounced lsp restart
Abierto:
- send request


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, schedule debounced lsp restart, restart lsp for workspace, start
    extra: apply workspace settings, setup build environment watching

T2
    visto: completion wait timeout ms, wait response, restart lsp for workspace, schedule debounced lsp restart
    extra: ensure python lsp async, completions at

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T2  initialize → send lsp request → wait response
hacia el resto:
  T1 → apply workspace settings
  T1 → setup build environment watching
  T2 → ensure python lsp async
  T2 → completions at
