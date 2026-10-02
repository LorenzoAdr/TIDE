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
1. [en_curso] A locator: dónde se invoca la comprobación de salud del servidor LSP para detectar si se ha caído
2. [pendiente] B locator: dónde se decide reiniciar el servidor LSP tras detectar que se ha caído
3. [pendiente] P puente: hay camino invocar la comprobación de salud y decidir reiniciar el servidor (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se reinicia el servidor de lenguaje LSP cuando se cae o se reinicia el entorno
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, apply workspace settings, setup build environment watching, process build environment updates
Cerrado:
encontré el mecanismo de reinicio del LSP: se dispara por instalación faltante on lsp missing install, cambio de settings apply workspace settings o cambio de entorno de build process build environment updates vía schedule debounced lsp restart; el efecto es restart lsp for workspace que reabre el workspace y reinicia los documentos.
Abierto:
- reopen workspace documents

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, clangd index healthy, process alive, ready
Cerrado:
encontré el mecanismo de detección de caída del LSP: es una comprobación activa de salud clangd index healthy que verifica si el proceso hijo sigue vivo mediante kill process alive y si el cliente está listo ready . No hay detección pasiva por señal de muerte ni timeout de heartbeat; la detección ocurre cuando se consulta la salud del índice.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se consulta la salud del índice clangd para detectar si el servidor se ha caído
keep: M2
leído: clangd index healthy, ready, process alive
Cerrado:
encontré el mecanismo de consulta de salud: clangd index healthy delega en ready y process alive; este último detecta la caída del servidor verificando si el PID existe y respondiendo a kill pid, 0
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se invoca la comprobación de salud del servidor LSP para detectar si se ha caído
keep: M5
leído: clangd process alive, process alive, clangd index healthy
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la invocación de la comprobación de salud para detectar caída; lo leído son los cuerpos de las funciones de salud y sus llamadas internas, pero no sus callers externos que disparan la detección
Abierto:
- callers de clangd process alive
- callers de clangd index healthy


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, clangd index healthy
    extra: on lsp missing install, on workspace opened

T3
    visto: clangd index healthy, ready, process alive
    extra: clangd starting, tabular max allowed scroll

T4
    visto: clangd process alive, process alive, clangd index healthy
    extra: read file text, english strings

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T2  run custom event drain → clangd index healthy
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: clangd index healthy
  T2=>T4  mismo objeto: clangd process alive
  T3=>T4  mismo objeto: clangd index healthy
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → clangd starting
  T3 → tabular max allowed scroll

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se reinicia el servidor de lenguaje LSP cuando se cae o se reinicia el entorno
Cerrado de T1:
encontré el mecanismo de reinicio del LSP: se dispara por instalación faltante on lsp missing install, cambio de settings apply workspace settings o cambio de entorno de build process build environment updates vía schedule debounced lsp restart; el efecto es restart lsp for workspace que reabre el workspace y reinicia los documentos.

T2 preguntó:
dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
Cerrado de T2:
encontré el mecanismo de detección de caída del LSP: es una comprobación activa de salud clangd index healthy que verifica si el proceso hijo sigue vivo mediante kill process alive y si el cliente está listo ready . No hay detección pasiva por señal de muerte ni timeout de heartbeat; la detección ocurre cuando se consulta la salud del índice.

T3 preguntó:
dónde se consulta la salud del índice clangd para detectar si el servidor se ha caído
Cerrado de T3:
encontré el mecanismo de consulta de salud: clangd index healthy delega en ready y process alive; este último detecta la caída del servidor verificando si el PID existe y respondiendo a kill pid, 0

T4 preguntó:
dónde se invoca la comprobación de salud del servidor LSP para detectar si se ha caído
Cerrado de T4:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la invocación de la comprobación de salud para detectar caída; lo leído son los cuerpos de las funciones de salud y sus llamadas internas, pero no sus callers externos que disparan la detección

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: pasar, no_pasar, revisar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). Esta pregunta ya tiene respuesta (no hay objeto). No la relances ni en otro keep. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no refutes el claim entero. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
