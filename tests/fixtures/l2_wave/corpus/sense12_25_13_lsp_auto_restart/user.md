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

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se reinicia el servidor de lenguaje LSP cuando se cae o deja de responder
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
Abierto:
- ensure backend started

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M5
leído: ensure backend started
Cerrado:
leído: ensure backend started
Abierto:
- set backend epoch
- register backend wake callback

### Trabajo 3
consulta: dónde se llama a reiniciar el servidor de lenguaje LSP tras detectar que se ha caído o dejado de responder
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, ensure backend started
Cerrado:
encontré el objeto de la consulta: el reinicio tras caída se dispara en lsp missing install llamado desde run que invoca a restart lsp for workspace el efecto es la reconfiguración del provider y reapertura de documentos
Abierto:
- on lsp missing install


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
    extra: on workspace opened, set workspace clangd options

T2
    visto: ensure backend started
    extra: set backend epoch, debug adapter kind for program

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, ensure backend started
    extra: on lsp missing install, on workspace opened
    entre interno: restart lsp for workspace → ensure backend started

entre abiertas:
  T1=>T2  extra toca visto: ensure backend started
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: ensure backend started
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → set backend epoch
  T2 → debug adapter kind for program
  T3 → on workspace opened
  T3 → set workspace clangd options

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se reinicia el servidor de lenguaje LSP cuando se cae o deja de responder
Cerrado de T1:
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run

T2 preguntó:
dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
Cerrado de T2:
leído: ensure backend started

T3 preguntó:
dónde se llama a reiniciar el servidor de lenguaje LSP tras detectar que se ha caído o dejado de responder
Cerrado de T3:
encontré el objeto de la consulta: el reinicio tras caída se dispara en lsp missing install llamado desde run que invoca a restart lsp for workspace el efecto es la reconfiguración del provider y reapertura de documentos

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: plan, cerrar
explorar agotado. Si falta un eslabón, plan (cada fase trae consulta tuya) o cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
