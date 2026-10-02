### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde para disparar un reinicio
keep: M2 M3
leído: on transport reader eof, start, schedule debounced lsp restart, restart lsp for workspace
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída no-respuesta que dispare el reinicio; lo leído muestra que transport reader eof solo limpia estado PID ready sin llamar a restart lsp for workspace ni a schedule debounced lsp restart y que el reinicio se dispara desde run o lsp missing install causas distintas a la caída del proceso
Abierto:
- on transport reader eof

### Trabajo 2
consulta: dónde se gestiona el ciclo de vida del proceso LSP para añadir detección de caída y reinicio automático
keep: M8
leído: restart lsp for workspace, run, clangd process alive
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída no-respuesta que dispare el reinicio; lo leído muestra que restart lsp for workspace es solo el efecto reinicio manual disparado externamente y clangd process alive es un check de estado sin lógica de reinicio asociada en el cuerpo leído
Abierto:
- spawn process
- on process exit
- watchdog timer

### Trabajo 3
consulta: dónde se inicia el proceso del servidor LSP y se maneja su salida para detectar caídas
keep: M5 M2
leído: start, on transport reader eof, spawn language server
Cerrado:
encontré el objeto y el mecanismo parcial: el proceso se inicia en spawn language server fork exec y la salida se maneja vía transport que registra transport reader eof para detectar EOF caída por cierre de pipe . Sin embargo, el handler actual solo limpia el PID y no dispara reinicio, lo cual confirma la brecha funcional reportada en olas anteriores.
Abierto:
- set reader eof handler
- reader loop


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, start, schedule debounced lsp restart, restart lsp for workspace
    extra: set notification handler, set reader eof handler

T2
    visto: restart lsp for workspace, run, clangd process alive
    extra: on lsp missing install, on workspace opened

T3
    visto: start, on transport reader eof, spawn language server
    extra: set notification handler, set reader eof handler

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T2  apply workspace settings → setup build environment watching → schedule debounced lsp restart
  T1=>T3  mismo objeto: on transport reader eof
  T2=>T3  sin camino
hacia el resto:
  T1 → set notification handler
  T1 → set reader eof handler
  T2 → on lsp missing install
  T2 → on workspace opened
  T3 → set notification handler
  T3 → set reader eof handler
