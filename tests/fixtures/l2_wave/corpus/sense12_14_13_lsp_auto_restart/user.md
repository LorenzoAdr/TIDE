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
1. [en_curso] A locator: dónde se registra el listener de salida del proceso LSP para detectar que ha muerto
2. [pendiente] B locator: dónde se lanza el proceso LSP y se captura su pid para monitorizarlo
3. [pendiente] P puente: el listener de salida del proceso LSP dispara el reinicio automático (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se inicia el reinicio del servidor LSP tras detectar que el proceso ha muerto
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: quién llama a la función de reinicio del LSP cuando se detecta que el proceso ha muerto
keep: M5
leído: clangd process alive
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré el mecanismo de detección de muerte del proceso que dispara el reinicio; lo leído en clangd process alive es un cuerpo truncado que no muestra la lógica de detección ni el disparo, y los callers de restart lsp for workspace son lsp missing install instalación y setup build environment watching configuración, no la detección de muerte.
Abierto:
- clangd process alive body
- on process exit
- check process alive

### Trabajo 4
consulta: dónde se registra el listener de salida del proceso LSP para detectar que ha muerto
keep: M5
leído: spawn clangd, spawn language server, on transport reader eof, transport running
Cerrado:
leído: spawn clangd, spawn language server, on transport reader eof, transport running
Abierto:
- start async worker


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
    extra: on lsp missing install, on workspace opened

T3
    visto: clangd process alive, restart lsp for workspace, schedule debounced lsp restart
    extra: read file text, english strings

T4
    visto: spawn clangd, spawn language server, on transport reader eof, transport running
    extra: make clangd spec, has value

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T1=>T4  sin camino
  T2=>T3  mismo objeto: restart lsp for workspace
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → on lsp missing install
  T2 → on workspace opened
  T3 → read file text
  T3 → english strings

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
Cerrado de T1:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive

T2 preguntó:
dónde se inicia el reinicio del servidor LSP tras detectar que el proceso ha muerto
Cerrado de T2:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive

T3 preguntó:
quién llama a la función de reinicio del LSP cuando se detecta que el proceso ha muerto
Cerrado de T3:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré el mecanismo de detección de muerte del proceso que dispara el reinicio; lo leído en clangd process alive es un cuerpo truncado que no muestra la lógica de detección ni el disparo, y los callers de restart lsp for workspace son lsp missing install instalación y setup build environment watching configuración, no la detección de muerte.

T4 preguntó:
dónde se registra el listener de salida del proceso LSP para detectar que ha muerto
Cerrado de T4:
leído: spawn clangd, spawn language server, on transport reader eof, transport running

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: pasar, no_pasar, revisar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

explorar agotado. Si falta un eslabón, plan (cada fase trae consulta tuya) o cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
