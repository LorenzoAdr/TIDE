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
consulta: dónde se detecta que el servidor LSP se ha caído o no responde y se dispara el reinicio
keep: M1 M4
leído: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on lsp notification, reader loop, wait response, stop, start
Cerrado:
leído: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on lsp notification, reader loop, wait response, stop, start
Abierto:
- send request

### Trabajo 2
consulta: dónde se reinicia el servidor LSP tras un fallo o timeout
keep: M5 M4
leído: schedule debounced lsp restart, restart lsp for workspace, wait response, reader loop, send request, start, read message, stop
Cerrado:
encontré el mecanismo de reinicio tras timeout el fallo EOF cierre de fd se detecta en reader loop vía read message y dispara reader eof handler el timeout se detecta en wait response y retorna fallo al caller, pero no reinicia directamente el reinicio tras timeout depende de la lógica de negocio del caller, ej. lsp client no del transporte . El reinicio debounced se programa en schedule debounced lsp restart y ejecuta restart lsp for workspace .
Abierto:
- on lsp crash

### Trabajo 3
consulta: dónde se detecta el cierre inesperado del servidor LSP y se dispara el reinicio automático
keep: M8 M9
leído: on transport reader eof, restart lsp for workspace
Cerrado:
encontré el objeto de la consulta: la detección del cierre inesperado está en transport reader eof que limpia el estado ready y espera al proceso hijo y el reinicio automático se dispara desde restart lsp for workspace que reconfigura el workspace y reinicia el provider de símbolos . La cadena causal está anclada: el EOF del transport notifica al cliente, y la aplicación orquesta el reinicio.
Abierto:
- restart lsp for workspace


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on lsp notification
    extra: async worker main, completions at

T2
    visto: schedule debounced lsp restart, restart lsp for workspace, wait response, reader loop
    extra: apply workspace settings, setup build environment watching
    entre interno: restart lsp for workspace → send request

T3
    visto: on transport reader eof, restart lsp for workspace
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T3  sin camino
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → async worker main
  T1 → completions at
  T2 → apply workspace settings
  T2 → setup build environment watching
  T3 → on lsp missing install
  T3 → on workspace opened

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor LSP se ha caído o no responde y se dispara el reinicio
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on lsp notification, reader loop, wait response, stop, start

T2 preguntó:
dónde se reinicia el servidor LSP tras un fallo o timeout
Cerrado de T2:
encontré el mecanismo de reinicio tras timeout el fallo EOF cierre de fd se detecta en reader loop vía read message y dispara reader eof handler el timeout se detecta en wait response y retorna fallo al caller, pero no reinicia directamente el reinicio tras timeout depende de la lógica de negocio del caller, ej. lsp client no del transporte . El reinicio debounced se programa en schedule debounced lsp restart y ejecuta restart lsp for workspace .

T3 preguntó:
dónde se detecta el cierre inesperado del servidor LSP y se dispara el reinicio automático
Cerrado de T3:
encontré el objeto de la consulta: la detección del cierre inesperado está en transport reader eof que limpia el estado ready y espera al proceso hijo y el reinicio automático se dispara desde restart lsp for workspace que reconfigura el workspace y reinicia el provider de símbolos . La cadena causal está anclada: el EOF del transport notifica al cliente, y la aplicación orquesta el reinicio.

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.
Un leído: de otra caza no afirma el disparo; no selles el ancla con esos nombres.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
