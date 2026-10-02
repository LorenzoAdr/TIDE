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

Plan de búsqueda (romper):
1. [en_curso] A locator: dónde se detecta que el servidor LSP se ha caído o dejado de responder
2. [pendiente] B locator: dónde se programa el reinicio automático del servidor LSP tras la detección
3. [pendiente] P puente: hay camino la detección de caída y el reinicio automático (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M2 M5
leído: on lsp notification, spawn language server
Cerrado:
leído: on lsp notification, spawn language server
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se programa el reinicio del servidor LSP cuando se detecta que falta o no responde
keep: M1 M2
leído: restart lsp for workspace, on lsp missing install, on lsp notification
Cerrado:
leído: restart lsp for workspace, on lsp missing install, on lsp notification
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el servidor LSP se ha caído o dejado de responder
keep: M1 M7
leído: wait response, start, on transport reader eof
Cerrado:
encontré los dos mecanismos de detección: 1 transport reader eof detecta la caída del proceso cierre de socket EOF y limpia el PID; 2 wait response detecta la falta de respuesta timeout mediante timeout en la variable de condición. No hay un mecanismo unificado de salud activo, sino detección reactiva por fallo de transporte o espera agotada.
Abierto:
- send request


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on lsp notification, spawn language server
    extra: contains, is lsp trackable path

T2
    visto: restart lsp for workspace, on lsp missing install, on lsp notification
    extra: on workspace opened, set workspace clangd options

T3
    visto: wait response, start, on transport reader eof
    extra: send request, contains

entre abiertas:
  T1=>T2  mismo objeto: on lsp notification
  T1=>T3  spawn language server → initialize → send lsp request → wait response
  T2=>T3  extra toca visto: on lsp notification
hacia el resto:
  T1 → contains
  T1 → is lsp trackable path
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → send request
  T3 → contains

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: on lsp notification, spawn language server

T2 preguntó:
dónde se programa el reinicio del servidor LSP cuando se detecta que falta o no responde
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: restart lsp for workspace, on lsp missing install, on lsp notification

T3 preguntó:
dónde se detecta que el servidor LSP se ha caído o dejado de responder
Cerrado de T3:
encontré los dos mecanismos de detección: 1 transport reader eof detecta la caída del proceso cierre de socket EOF y limpia el PID; 2 wait response detecta la falta de respuesta timeout mediante timeout en la variable de condición. No hay un mecanismo unificado de salud activo, sino detección reactiva por fallo de transporte o espera agotada.

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.
Un leído: de otra caza no afirma el disparo; no selles el ancla con esos nombres.

Legal ahora: cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. Un leído: de otra caza no afirma el disparo; no selles el ancla con esos nombres. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
