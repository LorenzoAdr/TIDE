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
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder para disparar un reinicio
keep: M2 M4
leído: on transport reader eof, schedule debounced lsp restart
Cerrado:
leído: on transport reader eof, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje LSP tras detectar que se ha caído
keep: M2 M5
leído: restart lsp for workspace, schedule debounced lsp restart, stop, start, spawn language server, on transport reader eof
Cerrado:
encontré el mecanismo de detección y reinicio: la detección es transport reader eof EOF del pipe stdout del servidor, pero este handler SOLO marca ready false y limpia el PID; NO dispara el reinicio. El reinicio es EXTERNO y REACTIVO se ejecuta desde application vía restart lsp for workspace llamado tras instalación LSP o en run o schedule debounced lsp restart tras cambios de config . No hay reinicio automático inmediato tras caída en el código leído; el sistema espera una acción de la aplicación o el usuario para llamar a restart lsp for workspace que a su vez llama a symbol provider workspace opened que internamente llama a start .
Abierto:
- on workspace opened

### Trabajo 3
consulta: qué hace schedule debounced lsp restart cuando se llama tras detectar que el servidor se ha caído
keep: M1 M2
leído: schedule debounced lsp restart, restart lsp for workspace, on transport reader eof
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio tras caída; lo leído on transport reader eof solo limpia estado local ready, waitpid y no invoca schedule debounced lsp restart ni ningún callback de reinicio
Abierto:
- on transport reader eof caller chain
- run loop lsp check


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, schedule debounced lsp restart
    extra: apply workspace settings, setup build environment watching

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, stop, start
    extra: on lsp missing install, on workspace opened

T3
    visto: schedule debounced lsp restart, restart lsp for workspace, on transport reader eof
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: on transport reader eof
  T1=>T3  mismo objeto: on transport reader eof
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → apply workspace settings
  T1 → setup build environment watching
  T2 → on lsp missing install
  T2 → on workspace opened
  T3 → apply workspace settings
  T3 → setup build environment watching

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder para disparar un reinicio
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: on transport reader eof, schedule debounced lsp restart

T2 preguntó:
dónde se reinicia el servidor de lenguaje LSP tras detectar que se ha caído
Cerrado de T2:
encontré el mecanismo de detección y reinicio: la detección es transport reader eof EOF del pipe stdout del servidor, pero este handler SOLO marca ready false y limpia el PID; NO dispara el reinicio. El reinicio es EXTERNO y REACTIVO se ejecuta desde application vía restart lsp for workspace llamado tras instalación LSP o en run o schedule debounced lsp restart tras cambios de config . No hay reinicio automático inmediato tras caída en el código leído; el sistema espera una acción de la aplicación o el usuario para llamar a restart lsp for workspace que a su vez llama a symbol provider workspace opened que internamente llama a start .

T3 preguntó:
qué hace schedule debounced lsp restart cuando se llama tras detectar que el servidor se ha caído
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio tras caída; lo leído on transport reader eof solo limpia estado local ready, waitpid y no invoca schedule debounced lsp restart ni ningún callback de reinicio

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
exploradores agotados. Lo leído no era el disparo. Cierra ese mapeo; un leído: de otra caza no afirma el ancla; no lo parafrasees como el mecanismo. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
