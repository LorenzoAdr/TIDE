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
consulta: dónde se detecta que el servidor LSP se ha caído o no responde
keep: M1 M4
leído: on transport reader eof, wait response, reader loop
Cerrado:
encontré los dos mecanismos de detección: 1 Caída del proceso: reader loop detecta EOF en la lectura y ejecuta reader eof handler que apunta a transport reader eof el cual limpia el PID con waitpid . 2 No respuesta timeout : wait response espera en pending cv hasta deadline si expira, borra la petición pendiente y retorna false propagando el fallo al cliente.
Abierto:
- send request

### Trabajo 2
consulta: dónde se reinicia el servidor LSP tras un fallo
keep: M6 M5
leído: on transport reader eof, wait response, reader loop, start, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de reinicio tras fallo: el fallo EOF se detecta en reader loop que invoca el handler transport reader eof en lsp client . Este handler limpia el estado ready false waitpid pero NO reinicia directamente. El reinicio se orquesta en application : schedule debounced lsp restart programa un reinicio diferido 2s, y restart lsp for workspace ejecuta la reapertura del workspace y documentos. El disparo del reinicio tras fallo específico no está completamente trazado en los callers de schedule debounced lsp restart solo settings build env, pero el flujo de fallo- limpieza está claro. El hueco es qué conecta exactamente el EOF del transport a la llamada de schedule debounced lsp restart o
Abierto:
- send request
- apply workspace settings

### Trabajo 3
consulta: qué hace el handler de fin de lectura del transporte para iniciar el reinicio del servidor
keep: M1 M6
leído: on transport reader eof, spawn language server, start
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio del servidor en el handler de fin de lectura; lo leído muestra que on transport reader eof solo limpia estado ready false, waitpid y NO invoca spawn ni restart. El reinicio no ocurre en este handler.
Abierto:
- reader loop


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, wait response, reader loop
    extra: send request, contains

T2
    visto: on transport reader eof, wait response, reader loop, start
    extra: send request, contains

T3
    visto: on transport reader eof, spawn language server, start
    extra: c str, initialize

entre abiertas:
  T1=>T2  mismo objeto: on transport reader eof
  T1=>T3  mismo objeto: on transport reader eof
  T1=>T3  spawn language server → initialize → send lsp request → wait response
  T2=>T3  mismo objeto: on transport reader eof
  T2=>T3  spawn language server → initialize → send lsp request → wait response
hacia el resto:
  T1 → send request
  T1 → contains
  T2 → send request
  T2 → contains
  T3 → c str
  T3 → initialize

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor LSP se ha caído o no responde
Cerrado de T1:
encontré los dos mecanismos de detección: 1 Caída del proceso: reader loop detecta EOF en la lectura y ejecuta reader eof handler que apunta a transport reader eof el cual limpia el PID con waitpid . 2 No respuesta timeout : wait response espera en pending cv hasta deadline si expira, borra la petición pendiente y retorna false propagando el fallo al cliente.

T2 preguntó:
dónde se reinicia el servidor LSP tras un fallo
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de reinicio tras fallo: el fallo EOF se detecta en reader loop que invoca el handler transport reader eof en lsp client . Este handler limpia el estado ready false waitpid pero NO reinicia directamente. El reinicio se orquesta en application : schedule debounced lsp restart programa un reinicio diferido 2s, y restart lsp for workspace ejecuta la reapertura del workspace y documentos. El disparo del reinicio tras fallo específico no está completamente trazado en los callers de schedule debounced lsp restart solo settings build env, pero el flujo de fallo- limpieza está claro. El hueco es qué conecta exactamente el EOF del transport a la llamada de schedule debounced lsp restart o

T3 preguntó:
qué hace el handler de fin de lectura del transporte para iniciar el reinicio del servidor
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio del servidor en el handler de fin de lectura; lo leído muestra que on transport reader eof solo limpia estado ready false, waitpid y NO invoca spawn ni restart. El reinicio no ocurre en este handler.

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
exploradores agotados. Lo leído no era el disparo. Cierra ese mapeo; no cites esos nombres como el mecanismo del ancla. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
