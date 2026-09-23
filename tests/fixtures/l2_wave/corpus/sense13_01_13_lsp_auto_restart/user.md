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
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio automático
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio automático
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se programa el reinicio diferido del servidor de lenguaje
keep: M3
leído: schedule debounced lsp restart, restart lsp for workspace
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se gestiona el ciclo de vida del proceso del servidor de lenguaje para añadir la detección de caída
keep: M5
leído: ensure completion server, start completion server, stop owned unlocked, stop listener on port, health ok
Cerrado:
encontré el objeto de la consulta: la gestión del ciclo de vida y la detección de caída health check se gestionan en start completion server que lanza el proceso y verifica health ok y health ok que realiza la petición HTTP al endpoint health . La detección de caída ya existe mediante health ok que se invoca tras el fork para confirmar que el servidor está listo y es el mecanismo a extender o conectar para la detección continua.
Abierto:
- start server


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: schedule debounced lsp restart, restart lsp for workspace
    extra: setup build environment watching, apply workspace settings

T3
    visto: ensure completion server, start completion server, stop owned unlocked, stop listener on port
    extra: Model Store, default cache dir
    entre interno: ensure completion server → stop owned unlocked

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → setup build environment watching
  T2 → apply workspace settings
  T3 → Model Store
  T3 → default cache dir

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio automático
Cerrado de T1:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
(vacío)

T2 preguntó:
dónde se programa el reinicio diferido del servidor de lenguaje
Cerrado de T2:
leído: schedule debounced lsp restart, restart lsp for workspace

T3 preguntó:
dónde se gestiona el ciclo de vida del proceso del servidor de lenguaje para añadir la detección de caída
Cerrado de T3:
encontré el objeto de la consulta: la gestión del ciclo de vida y la detección de caída health check se gestionan en start completion server que lanza el proceso y verifica health ok y health ok que realiza la petición HTTP al endpoint health . La detección de caída ya existe mediante health ok que se invoca tras el fork para confirmar que el servidor está listo y es el mecanismo a extender o conectar para la detección continua.

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
