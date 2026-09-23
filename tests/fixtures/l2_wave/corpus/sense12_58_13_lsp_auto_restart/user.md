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
keep: M2 M3
leído: restart lsp for workspace, schedule debounced lsp restart, on transport reader eof, start, stop
Cerrado:
encontré el mecanismo de detección y disparo: la caída se detecta en transport reader eof disparado por el transport al cerrar el pipe EOF, pero este callback SOLO limpia estado ready false waitpid . NO dispara el reinicio directamente. El reinicio se dispara desde restart lsp for workspace que es llamado por lsp missing install o run inicio, o programado vía schedule debounced lsp restart ante cambios de config. No hay un camino directo de transport reader eof a restart lsp for workspace en el código leído; la detección de caída parece ser pasiva limpieza y el reinicio activo está desacoplado o depende de un ciclo de vida superior no completamente rastreado en esta ola posiblemente application
Abierto:
- on lsp missing install
- on workspace opened

### Trabajo 2
consulta: dónde se programa el reinicio diferido del servidor LSP tras un fallo de transporte
keep: M1 M4
leído: schedule debounced lsp restart, on transport reader eof, restart lsp for workspace, start
Cerrado:
encontré el objeto de la consulta: el reinicio diferido se programa en schedule debounced lsp restart que establece el deadline y se dispara tras el fallo de transporte detectado en transport reader eof que limpia el estado del proceso hijo, aunque el circuito causal completo el EOF y la programación del reinicio queda parcialmente abierto el EOF no llama directamente al scheduler en el código leído, sugiriendo un paso intermedio o una lógica de reconexión no leída en start o stop .
Abierto:
- run loop check

### Trabajo 3
consulta: dónde se llama a schedule debounced lsp restart tras detectar el fallo en transport reader eof
keep: M8 M10
leído: on transport reader eof, start, set reader eof handler, schedule debounced lsp restart
Cerrado:
leído: on transport reader eof, start, set reader eof handler, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, on transport reader eof, start
    extra: on lsp missing install, on workspace opened

T2
    visto: schedule debounced lsp restart, on transport reader eof, restart lsp for workspace, start
    extra: apply workspace settings, setup build environment watching

T3
    visto: on transport reader eof, start, set reader eof handler, schedule debounced lsp restart
    extra: set notification handler, set response acceptance filter

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → apply workspace settings
  T2 → setup build environment watching
  T3 → set notification handler
  T3 → set response acceptance filter

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor LSP se ha caído o no responde y se dispara el reinicio
Cerrado de T1:
encontré el mecanismo de detección y disparo: la caída se detecta en transport reader eof disparado por el transport al cerrar el pipe EOF, pero este callback SOLO limpia estado ready false waitpid . NO dispara el reinicio directamente. El reinicio se dispara desde restart lsp for workspace que es llamado por lsp missing install o run inicio, o programado vía schedule debounced lsp restart ante cambios de config. No hay un camino directo de transport reader eof a restart lsp for workspace en el código leído; la detección de caída parece ser pasiva limpieza y el reinicio activo está desacoplado o depende de un ciclo de vida superior no completamente rastreado en esta ola posiblemente application

T2 preguntó:
dónde se programa el reinicio diferido del servidor LSP tras un fallo de transporte
Cerrado de T2:
encontré el objeto de la consulta: el reinicio diferido se programa en schedule debounced lsp restart que establece el deadline y se dispara tras el fallo de transporte detectado en transport reader eof que limpia el estado del proceso hijo, aunque el circuito causal completo el EOF y la programación del reinicio queda parcialmente abierto el EOF no llama directamente al scheduler en el código leído, sugiriendo un paso intermedio o una lógica de reconexión no leída en start o stop .

T3 preguntó:
dónde se llama a schedule debounced lsp restart tras detectar el fallo en transport reader eof
Cerrado de T3:
leído: on transport reader eof, start, set reader eof handler, schedule debounced lsp restart

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
