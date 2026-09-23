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
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5 M8
leído: ensure completion server, ensure ready, start completion server, ready, health ok, stop owned unlocked, stop listener on port
Cerrado:
encontré el mecanismo: la detección de caída no-respuesta es health ok probe HTTP health y el disparo de reinicio ocurre en start completion server cuando health ok falla o el stamp cambia, invocando stop listener on port antes de relanzar
Abierto:
- start server
- handle route
- run level1 async

### Trabajo 2
consulta: dónde se programa el reinicio diferido del servidor de lenguaje cuando se detecta que se ha caído
keep: M2 M5
leído: ensure completion server, ensure ready, start completion server
Cerrado:
leído: ensure completion server, ensure ready, start completion server
Abierto:
- handle route
- run level1 async
- stop owned unlocked
- stop listener on port

### Trabajo 3
consulta: dónde se reinicia el servidor de lenguaje cuando el probe de salud falla o el proceso se cae
keep: M10 M2
leído: restart lsp for workspace, schedule debounced lsp restart, clangd index healthy, client latest completion id
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd index healthy, client latest completion id
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ensure completion server, ensure ready, start completion server, ready
    extra: Model Store, default cache dir
    entre interno: ensure completion server → stop owned unlocked

T2
    visto: ensure completion server, ensure ready, start completion server
    extra: Model Store, default cache dir

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd index healthy, client latest completion id
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: ensure completion server
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Model Store
  T1 → default cache dir
  T2 → Model Store
  T2 → default cache dir
  T3 → on lsp missing install
  T3 → on workspace opened

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
Cerrado de T1:
encontré el mecanismo: la detección de caída no-respuesta es health ok probe HTTP health y el disparo de reinicio ocurre en start completion server cuando health ok falla o el stamp cambia, invocando stop listener on port antes de relanzar

T2 preguntó:
dónde se programa el reinicio diferido del servidor de lenguaje cuando se detecta que se ha caído
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: ensure completion server, ensure ready, start completion server

T3 preguntó:
dónde se reinicia el servidor de lenguaje cuando el probe de salud falla o el proceso se cae
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: restart lsp for workspace, schedule debounced lsp restart, clangd index healthy, client latest completion id

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
exploradores agotados. Lo leído no era el disparo. Cierra ese mapeo; no cites esos nombres como el mecanismo del ancla. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
