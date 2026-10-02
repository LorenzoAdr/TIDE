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
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M3 M9
leído: wait response, send lsp request, restart lsp for workspace
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída no-respuesta que dispare el reinicio; lo leído muestra que restart lsp for workspace se dispara únicamente tras una instalación exitosa on lsp missing install o en el arranque run, y que wait response retorna false en timeout sin lógica de reinicio
Abierto:
- send request
- handle response
- on error
- on transport error

### Trabajo 2
consulta: dónde se maneja el error de transporte o la respuesta fallida de una petición LSP para decidir si se reinicia
keep: M2 M8
leído: reader loop, initialize, read message, start, on transport reader eof, wait response, completion wait timeout ms
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de decisión de reinicio tras error de transporte o timeout; lo leído muestra que el transporte notifica EOF al cliente on transport reader eof pero este solo limpia estado ready false, waitpid sin disparar reinicio, y wait response retorna false en timeout error sin lógica de recuperación automática; el reinicio parece ser manual o externo restart lsp for workspace en application cpp, no vinculado a estos fallos
Abierto:
- send request
- restart lsp for workspace
- on lsp notification


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: wait response, send lsp request, restart lsp for workspace
    extra: send request, contains
    entre interno: restart lsp for workspace → send lsp request

T2
    visto: reader loop, initialize, read message, start
    extra: open host pty, contains

entre abiertas:
  T1=>T2  mismo objeto: wait response
  T1=>T2  initialize → send lsp request
hacia el resto:
  T1 → send request
  T1 → contains
  T2 → open host pty
  T2 → contains

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída no-respuesta que dispare el reinicio; lo leído muestra que restart lsp for workspace se dispara únicamente tras una instalación exitosa on lsp missing install o en el arranque run, y que wait response retorna false en timeout sin lógica de reinicio

T2 preguntó:
dónde se maneja el error de transporte o la respuesta fallida de una petición LSP para decidir si se reinicia
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de decisión de reinicio tras error de transporte o timeout; lo leído muestra que el transporte notifica EOF al cliente on transport reader eof pero este solo limpia estado ready false, waitpid sin disparar reinicio, y wait response retorna false en timeout error sin lógica de recuperación automática; el reinicio parece ser manual o externo restart lsp for workspace en application cpp, no vinculado a estos fallos

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: bosquejar, explorar, plan, cerrar
Este es el estado. Tú diriges: atomiza el trabajo (un hijo, un trozo). El plan no es un contrato: tras un Cerrado puedes soltarlo (explorar o plan nuevo). Esta pregunta ya tiene respuesta (no hay objeto). No la relances ni en otro keep. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero. 'mismo objeto' en entre abiertas es que la caza no se movió. Si queda un tirón, explorar; si el trabajo no cabe en uno, plan. consulta = prompt de investigación (fenómeno y verbo; el hijo no ve el ancla); why = tu estrategia (el hijo no la lee). Lo que no se caza no se nombra. hacia opcional: conceptos de cerca, no ids. evita opcional. Un explorar es una sola rama; atomiza, no empaquetes dos cazas en un hijo. tú eliges cómo seguir. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
