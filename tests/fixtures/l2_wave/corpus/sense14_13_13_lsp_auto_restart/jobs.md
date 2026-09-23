### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M3 M9
leído: wait response, send lsp request, restart lsp for workspace
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída no-respuesta que dispare el reinicio; lo leído muestra que restart lsp for workspace se dispara únicamente tras una instalación exitosa on lsp missing install o en el arranque run, y que wait response retorna false en timeout sin lógica de reinicio
Abierto:
- send request
- handle response
- on error
- on transport error

### Trabajo 2
consulta: dónde se maneja el error de transporte o la respuesta fallida de una petición LSP para decidir si se reinicia
keep: M2 M8
leído: reader loop, initialize, read message, start, on transport reader eof, wait response, completion wait timeout ms
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de decisión de reinicio tras error de transporte o timeout; lo leído muestra que el transporte notifica EOF al cliente on transport reader eof pero este solo limpia estado ready false, waitpid sin disparar reinicio, y wait response retorna false en timeout error sin lógica de recuperación automática; el reinicio parece ser manual o externo restart lsp for workspace en application cpp, no vinculado a estos fallos
Abierto:
- send request
- restart lsp for workspace
- on lsp notification


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: wait response, send lsp request, restart lsp for workspace
    extra: send request, contains
    entre interno: restart lsp for workspace → send lsp request

T2
    visto: reader loop, initialize, read message, start
    extra: open host pty, contains

entre abiertas:
  T1=>T2  mismo objeto: wait response
  T1=>T2  initialize → send lsp request
hacia el resto:
  T1 → send request
  T1 → contains
  T2 → open host pty
  T2 → contains
