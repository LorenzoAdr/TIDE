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
peek: application apply event
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
peek: l2 effect registry ascii lower copy
M12  kind=hole  ov=0  stems: l2 brain remote l2 brain remote
owns: l2 brain remote
gap: no state
peek: l2 brain remote make l2 brain
bridges: T4[M3,M6]
holes: llama net apply ai runtime env l2 effect registry list file fns l2 effect registry ascii lower copy l2 brain remote make l2 brain

Plan de búsqueda (romper):
1. [en_curso] A locator: dónde se detecta que el proceso del servidor LSP se ha caído o dejado de responder durante la ejecución
2. [pendiente] B locator: dónde se reinicia automáticamente el servidor LSP tras detectar la caída
3. [pendiente] P puente: hay camino la detección de la caída del LSP y su reinicio automático (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o no responde para disparar el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se llama a reiniciar el LSP cuando se detecta que el proceso no está vivo
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se inicia la detección automática de la caída del LSP al abrir el workspace
keep: M5
leído: set workspace, restart lsp for workspace, schedule debounced lsp restart, clangd process alive, run, on workspace opened
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el objeto de la consulta: la detección automática de la caída del LSP al abrir el workspace se inicia en run vía lsp missing install que llama a restart lsp for workspace . Esta función ejecuta symbol provider workspace opened que es el punto de entrada para la inicialización detección del LSP en el contexto del workspace abierto. No hay un heartbeat explícito en clangd process alive vinculado al evento de apertura, sino un reinicio arranque condicional.
Abierto:
- on lsp missing install

### Trabajo 4
consulta: dónde se detecta que el proceso del servidor LSP se ha caído o dejado de responder durante la ejecución
keep: M5
leído: clangd process alive
Cerrado:
leído: clangd process alive
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
    extra: on lsp missing install, on workspace opened

T3
    visto: set workspace, restart lsp for workspace, schedule debounced lsp restart, clangd process alive
    extra: Application, filename

T4
    visto: clangd process alive
    extra: read file text, refresh fortran compiler diagnostics

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T1=>T4  mismo objeto: clangd process alive
  T2=>T3  mismo objeto: restart lsp for workspace
  T2=>T4  mismo objeto: clangd process alive
  T3=>T4  mismo objeto: clangd process alive
hacia el resto:
  T1 → on lsp missing install
  T1 → set workspace clangd options
  T2 → on lsp missing install
  T2 → set workspace clangd options
  T3 → Application
  T3 → filename

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor LSP ha caído o no responde para disparar el reinicio
Cerrado de T1:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive

T2 preguntó:
dónde se llama a reiniciar el LSP cuando se detecta que el proceso no está vivo
Cerrado de T2:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive

T3 preguntó:
dónde se inicia la detección automática de la caída del LSP al abrir el workspace
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el objeto de la consulta: la detección automática de la caída del LSP al abrir el workspace se inicia en run vía lsp missing install que llama a restart lsp for workspace . Esta función ejecuta symbol provider workspace opened que es el punto de entrada para la inicialización detección del LSP en el contexto del workspace abierto. No hay un heartbeat explícito en clangd process alive vinculado al evento de apertura, sino un reinicio arranque condicional.

T4 preguntó:
dónde se detecta que el proceso del servidor LSP se ha caído o dejado de responder durante la ejecución
Cerrado de T4:
leído: clangd process alive

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: pasar, no_pasar, revisar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso, ni un dump «leído:». no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

El Cerrado es un dump de nombres, no afirma el objeto. no_pasar o cierra ese mapeo; no pases.
