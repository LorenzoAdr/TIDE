### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio automático
keep: M1 M10
leído: restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se dispara el reinicio automático
keep: M2 M6
leído: restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms, on transport reader eof, set reader eof handler, start
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms, on transport reader eof, set reader eof handler, start
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: qué evento o error dispara la función de reinicio del servidor de lenguaje
keep: M5 M8
leído: on transport reader eof, set reader eof handler, start, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio automático tras error de transporte; lo leído muestra que transport reader eof solo limpia estado ready false waitpid sin disparar reinicio, y que restart lsp for workspace es invocado manualmente o por configuración, no por el EOF del transporte
Abierto:
- on transport reader eof
- reader eof handler


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, completion wait timeout ms, on transport reader eof
    extra: on lsp missing install, on workspace opened

T3
    visto: on transport reader eof, set reader eof handler, start, restart lsp for workspace
    extra: set notification handler, set response acceptance filter

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → on lsp missing install
  T2 → on workspace opened
  T3 → set notification handler
  T3 → set response acceptance filter
