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
consulta: dónde se reinicia el servidor de lenguaje LSP cuando se cae o se reinicia el entorno
keep: M4 M6
leído: schedule debounced lsp restart, on transport reader eof, apply workspace settings, setup build environment watching, start, restart lsp for workspace, run, on lsp missing install
Cerrado:
encontré el mecanismo de reinicio del LSP: la caída se detecta en transport reader eof EOF del transporte, pero el reinicio propiamente dicho se orquesta en Application mediante restart lsp for workspace . Este reinicio se dispara por cambio de entorno apply workspace settings - restart lsp for workspace por cambio de artefactos de build setup build environment watching - schedule debounced lsp restart - restart lsp for workspace en el loop principal, aunque el cuerpo del loop no se leyó, el patrón es claro, o tras instalación de LSP lsp missing install - restart lsp for workspace . El cuerpo de restart lsp for workspace confirma que reinicia el proveedor de símbolos y reabre documentos.
Abierto:
- run loop body

### Trabajo 2
consulta: dónde se detecta que el servidor LSP deja de responder o se congela y se dispara el reinicio
keep: M3 M11
leído: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on transport reader eof, restart lsp for workspace
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de congelación timeout global watchdog que dispare el reinicio; lo leído son timeouts por request específica completion parse y un handler de desconexión física EOF, pero ninguno de ellos invoca al reinicio ni implementa un watchdog de actividad
Abierto:
- watchdog loop
- on response timeout
- check server health


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: schedule debounced lsp restart, on transport reader eof, apply workspace settings, setup build environment watching
    extra: Workspace Config, apply clangd workspace config

T2
    visto: completion wait timeout ms, parse wait timeout ms, schedule debounced lsp restart, on transport reader eof
    extra: async worker main, completions at

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T2  initialize → did open → generation → parse wait timeout ms
hacia el resto:
  T1 → Workspace Config
  T1 → apply clangd workspace config
  T2 → async worker main
  T2 → completions at

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se reinicia el servidor de lenguaje LSP cuando se cae o se reinicia el entorno
Cerrado de T1:
encontré el mecanismo de reinicio del LSP: la caída se detecta en transport reader eof EOF del transporte, pero el reinicio propiamente dicho se orquesta en Application mediante restart lsp for workspace . Este reinicio se dispara por cambio de entorno apply workspace settings - restart lsp for workspace por cambio de artefactos de build setup build environment watching - schedule debounced lsp restart - restart lsp for workspace en el loop principal, aunque el cuerpo del loop no se leyó, el patrón es claro, o tras instalación de LSP lsp missing install - restart lsp for workspace . El cuerpo de restart lsp for workspace confirma que reinicia el proveedor de símbolos y reabre documentos.

T2 preguntó:
dónde se detecta que el servidor LSP deja de responder o se congela y se dispara el reinicio
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de congelación timeout global watchdog que dispare el reinicio; lo leído son timeouts por request específica completion parse y un handler de desconexión física EOF, pero ninguno de ellos invoca al reinicio ni implementa un watchdog de actividad

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: bosquejar, explorar, plan, cerrar
Este es el estado. Tú diriges: atomiza el trabajo (un hijo, un trozo). El plan no es un contrato: tras un Cerrado puedes soltarlo (explorar o plan nuevo). Esta pregunta ya tiene respuesta (no hay objeto). No la relances ni en otro keep. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero. 'mismo objeto' en entre abiertas es que la caza no se movió. Si queda un tirón, explorar; si el trabajo no cabe en uno, plan. consulta = prompt de investigación (fenómeno y verbo; el hijo no ve el ancla); why = tu estrategia (el hijo no la lee). Lo que no se caza no se nombra. hacia opcional: conceptos de cerca, no ids. evita opcional. Un explorar es una sola rama; atomiza, no empaquetes dos cazas en un hijo. tú eliges cómo seguir. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

El último Cerrado negó el mapeo. Cierra ESE mapeo (hueco o sin camino), no selles un polo que sí se leyó.
