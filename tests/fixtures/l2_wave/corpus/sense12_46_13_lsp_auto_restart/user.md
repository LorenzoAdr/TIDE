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
1. [en_curso] A locator: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
2. [pendiente] B locator: dónde se reinicia el servidor LSP automáticamente tras detectar una caída (hacia reinicio)
3. [pendiente] P puente: hay camino detectar la caída y reiniciar el servidor (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
Abierto:
- ensure backend started

### Trabajo 2
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run, ensure backend started
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run, ensure backend started
Abierto:
- set backend epoch
- register backend wake callback

### Trabajo 3
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M5
leído: notify lsp status, ensure python lsp async, finish python lsp start locked
Cerrado:
leído: notify lsp status, ensure python lsp async, finish python lsp start locked
Abierto:
- stop lsp

### Trabajo 4
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M5
leído: schedule debounced lsp restart, restart lsp for workspace, run, on lsp missing install
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace, run, on lsp missing install
Abierto:
- ensure backend started


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: run level1 async, handle route

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
    extra: on workspace opened, set workspace clangd options

T3
    visto: notify lsp status, ensure python lsp async, finish python lsp start locked
    extra: Make Editor Panel, on document opened

T4
    visto: schedule debounced lsp restart, restart lsp for workspace, run, on lsp missing install
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  sin camino
  T1=>T4  mismo objeto: restart lsp for workspace
  T2=>T3  sin camino
  T2=>T4  mismo objeto: restart lsp for workspace
  T3=>T4  sin camino
hacia el resto:
  T1 → run level1 async
  T1 → handle route
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → Make Editor Panel
  T3 → on document opened

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
Cerrado de T1:
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install

T2 preguntó:
dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
Cerrado de T2:
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run, ensure backend started

T3 preguntó:
dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
Cerrado de T3:
leído: notify lsp status, ensure python lsp async, finish python lsp start locked

T4 preguntó:
dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
Cerrado de T4:
leído: schedule debounced lsp restart, restart lsp for workspace, run, on lsp missing install

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: pasar, no_pasar, revisar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
