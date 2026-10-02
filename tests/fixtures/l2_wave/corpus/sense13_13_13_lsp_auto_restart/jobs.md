### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde para disparar el reinicio
keep: M2 M3
leído: on transport reader eof, start, schedule debounced lsp restart, restart lsp for workspace
Cerrado:
leído: on transport reader eof, start, schedule debounced lsp restart, restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP deja de responder por timeout para disparar el reinicio
keep: M1 M2
leído: wait response, reader loop, schedule debounced lsp restart, restart lsp for workspace, run
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de timeout que dispara el reinicio; lo leído muestra que wait response detecta el timeout a nivel de transporte pero no dispara reinicio, y restart lsp for workspace es llamado por run loop UI y lsp missing install sin evidencia de un watcher de timeout global que invoque schedule debounced lsp restart
Abierto:
- send request
- lsp restart deadline
- pending lsp restart
- on lsp missing install

### Trabajo 3
consulta: dónde se usa el deadline de reinicio del lsp para detectar que no responde
keep: M4
leído: schedule debounced lsp restart
Cerrado:
leído: schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, start, schedule debounced lsp restart, restart lsp for workspace
    extra: set notification handler, set reader eof handler

T2
    visto: wait response, reader loop, schedule debounced lsp restart, restart lsp for workspace
    extra: send request, contains

T3
    visto: schedule debounced lsp restart
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T2  initialize → send lsp request → wait response
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T1 → set notification handler
  T1 → set reader eof handler
  T2 → send request
  T2 → contains
  T3 → apply workspace settings
  T3 → setup build environment watching
