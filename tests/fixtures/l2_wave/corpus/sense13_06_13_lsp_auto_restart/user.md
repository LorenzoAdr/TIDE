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
keep: M5
leído: status text, start completion server, ensure completion server, health ok, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: status text, start completion server, ensure completion server, health ok, restart lsp for workspace, schedule debounced lsp restart
Abierto:
- start server
- stop owned unlocked
- stop listener on port

### Trabajo 2
consulta: dónde se lee el estado del proceso del servidor para saber si está vivo o muerto
keep: M1
leído: process alive
Cerrado:
encontré el mecanismo: process alive lee el estado vía waitpid child pid, status, WNOHANG y actualiza child pid si el proceso ha terminado
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se usa el estado del proceso para decidir si reiniciar el servidor
keep: M7
leído: ensure completion server, restart lsp for workspace, ensure ready, start server, stop owned unlocked, health ok
Cerrado:
encontré el objeto de la consulta: la decisión de reiniciar se basa en el estado del proceso server pid 0 y su salud health ok en ensure ready L945 y start server L792
Abierto:
- run level1 async
- start completion server
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: status text, start completion server, ensure completion server, health ok
    extra: cache dir, stop listener on port

T2
    visto: process alive
    extra: clangd process alive

T3
    visto: ensure completion server, restart lsp for workspace, ensure ready, start server
    extra: Model Store, default cache dir
    entre interno: ensure completion server → ensure ready

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: ensure completion server
  T2=>T3  sin camino
hacia el resto:
  T1 → cache dir
  T1 → stop listener on port
  T2 → clangd process alive
  T3 → Model Store
  T3 → default cache dir

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: status text, start completion server, ensure completion server, health ok, restart lsp for workspace, schedule debounced lsp restart

T2 preguntó:
dónde se lee el estado del proceso del servidor para saber si está vivo o muerto
Cerrado de T2:
encontré el mecanismo: process alive lee el estado vía waitpid child pid, status, WNOHANG y actualiza child pid si el proceso ha terminado

T3 preguntó:
dónde se usa el estado del proceso para decidir si reiniciar el servidor
Cerrado de T3:
encontré el objeto de la consulta: la decisión de reiniciar se basa en el estado del proceso server pid 0 y su salud health ok en ensure ready L945 y start server L792

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
