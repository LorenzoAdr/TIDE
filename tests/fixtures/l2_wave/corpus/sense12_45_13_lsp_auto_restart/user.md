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
1. [en_curso] A locator: dónde se llama al reinicio del LSP cuando el proceso no está vivo
2. [pendiente] B locator: dónde se detecta que el LSP no responde y se dispara el reinicio
3. [pendiente] P puente: hay camino la detección de caída y el reinicio automático (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se programa su reinicio
keep: M5
leído: schedule debounced lsp restart, restart lsp for workspace, run
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de caída no-respuesta del LSP; lo leído es la programación del reinicio tras cambios de entorno schedule debounced lsp restart y la ejecución del reinicio restart lsp for workspace, pero no hay lógica de ping timeout en el bucle principal run ni en los callers leídos
Abierto:
- lsp heartbeat
- lsp ping
- lsp timeout
- check lsp alive

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se programa su reinicio
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se programa su reinicio
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se comprueba si el servidor de lenguaje está vivo o responde antes de enviarle una petición
keep: M5
leído: process alive, clangd process alive, send request
Cerrado:
leído: process alive, clangd process alive, send request
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se llama al reinicio del LSP cuando el proceso no está vivo
keep: M5
leído: process alive, clangd process alive
Cerrado:
leído: process alive, clangd process alive
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: schedule debounced lsp restart, restart lsp for workspace, run
    extra: setup build environment watching, apply workspace settings

T2
    visto: (nada)

T3
    visto: process alive, clangd process alive, send request
    extra: read file text, TUIDE MON

T4
    visto: process alive, clangd process alive
    extra: read file text

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  sin camino
  T2=>T4  sin camino
  T3=>T4  mismo objeto: process alive
hacia el resto:
  T1 → setup build environment watching
  T1 → apply workspace settings
  T3 → read file text
  T3 → TUIDE MON
  T4 → read file text

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor de lenguaje se ha caído o no responde y se programa su reinicio
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de caída no-respuesta del LSP; lo leído es la programación del reinicio tras cambios de entorno schedule debounced lsp restart y la ejecución del reinicio restart lsp for workspace, pero no hay lógica de ping timeout en el bucle principal run ni en los callers leídos

T2 preguntó:
dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde y se programa su reinicio
Cerrado de T2:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
(vacío)

T3 preguntó:
dónde se comprueba si el servidor de lenguaje está vivo o responde antes de enviarle una petición
Cerrado de T3:
leído: process alive, clangd process alive, send request

T4 preguntó:
dónde se llama al reinicio del LSP cuando el proceso no está vivo
Cerrado de T4:
leído: process alive, clangd process alive

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: pasar, no_pasar, revisar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
