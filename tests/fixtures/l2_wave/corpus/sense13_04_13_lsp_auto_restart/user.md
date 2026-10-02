Consulta del usuario (ancla, claim; no la copies entera a un hijo):
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=12  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M1  kind=other  ov=3  stems: lsp missing prompt
owns: lsp missing prompt
gap: state,effect
peek: lsp missing prompt catalog, lsp missing prompt lsp missing prompt for status key
peek-edge: lsp missing prompt lsp missing prompt for status key -call-> lsp missing prompt catalog
M2  kind=other  ov=3  stems: lsp symbol provider
owns: lsp symbol provider
gap: state,trigger,effect
peek: lsp symbol provider configure bash language server
M3  kind=caller  ov=0  stems: model store
owns: model store
gap: state,effect
peek: model store has llama server, model store resolve llama server
port: M3=>M6 llama backend ensure completion server -call-> model store resolve llama server
M4  same=M2  kind=other  ov=3  stems: lsp symbol provider
peek: lsp symbol provider join fortran startup thread
M5  kind=latch  ov=3  stems: application application
owns: application
nucleus: pending lsp restart , last lsp environment fingerprint 
peek: application restart lsp for workspace, application schedule debounced lsp restart
port: M5=>M7 application restart lsp for workspace -write-> status message
M6  kind=caller  ov=0  stems: embedding backend llama backend embedding backend llama backend
owns: embedding backend
nucleus: server pid , errno, server stamp , server pid 
peek: embedding backend start server, llama backend start completion server
port: M6=>M3 llama backend ensure completion server -call-> model store resolve llama server
M7  same=M5  kind=caller  ov=3  stems: application
peek: application apply event, application restart lsp for workspace
M8  kind=caller  ov=0  stems: ai controller ai controller
owns: ai controller
nucleus: settings 
peek: ai controller begin insert at, llama net apply ai runtime env
port: M8=>M7 application run -call-> ai controller begin insert at
M9  kind=hole  ov=0  stems: settings  workspace config app settings model store
owns: settings  / llama net apply ai runtime env
nucleus: settings 
peek: llama net apply ai runtime env, api base
peek-edge: llama net apply ai runtime env -write-> api base
M10  kind=hole  ov=0  stems: l2 effect registry l2 effect registry
owns: l2 effect registry
gap: no state
peek: l2 effect registry list file fns, l2 effect registry read abs
peek-edge: l2 effect registry list file fns -call-> l2 effect registry read abs
M11  same=M8  kind=hole  ov=0  stems: ai controller
peek: l2 effect registry ascii lower copy, l2 effect registry collect text tokens
M12  kind=hole  ov=0  stems: l2 brain remote l2 brain remote
owns: l2 brain remote
gap: no state
peek: l2 brain remote make l2 brain
bridges: T4[M3,M6]
holes: llama net apply ai runtime env l2 effect registry list file fns l2 effect registry ascii lower copy l2 brain remote make l2 brain

Trabajos ya hechos:
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

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde para disparar un reinicio
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída no-respuesta que dispare el reinicio; lo leído muestra que transport reader eof solo limpia estado PID ready sin llamar a restart lsp for workspace ni a schedule debounced lsp restart y que el reinicio se dispara desde run o lsp missing install causas distintas a la caída del proceso

T2 preguntó:
dónde se gestiona el ciclo de vida del proceso LSP para añadir detección de caída y reinicio automático
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída no-respuesta que dispare el reinicio; lo leído muestra que restart lsp for workspace es solo el efecto reinicio manual disparado externamente y clangd process alive es un check de estado sin lógica de reinicio asociada en el cuerpo leído

T3 preguntó:
dónde se inicia el proceso del servidor LSP y se maneja su salida para detectar caídas
Cerrado de T3:
encontré el objeto y el mecanismo parcial: el proceso se inicia en spawn language server fork exec y la salida se maneja vía transport que registra transport reader eof para detectar EOF caída por cierre de pipe . Sin embargo, el handler actual solo limpia el PID y no dispara reinicio, lo cual confirma la brecha funcional reportada en olas anteriores.

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
