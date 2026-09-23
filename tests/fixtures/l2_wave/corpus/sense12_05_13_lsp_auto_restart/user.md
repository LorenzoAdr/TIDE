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
1. [en_curso] A locator: dónde se detecta que el proceso LSP ha muerto o no responde y se dispara el reinicio automático
2. [pendiente] B locator: dónde se reinicia el servidor LSP cuando falta la instalación o el entorno cambia
3. [pendiente] P puente: el mecanismo de detección de caída A cubre el caso de deja de responder' o requiere un watchdog separado (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: quién llama a schedule debounced lsp restart cuando el servidor LSP deja de responder
keep: M5
leído: schedule debounced lsp restart
Cerrado:
leído: schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: quién invoca el reinicio diferido del LSP cuando se detecta que el servidor falta o no responde
keep: M5
leído: process build environment updates, restart lsp for workspace, on lsp missing install
Cerrado:
encontré el objeto de la consulta: el reinicio diferido se invoca vía schedule debounced lsp restart cuyo único caller anclado es setup build environment watching disparado por cambios en el entorno de build, no por fallo directo del LSP . El caso de servidor falta lsp missing install invoca restart lsp for workspace directamente sin debounce. La consulta mezcla dos caminos: el diferido entorno y el inmediato falta .
Abierto:
- run custom event drain
- setup build environment watching

### Trabajo 4
consulta: dónde se detecta que el proceso LSP ha muerto o no responde y se dispara el reinicio automático
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
Cerrado:
Lo leído no contestó ESA pregunta. Un hop de Abierto/extra no es un hueco.
no encontré la detección de muerte no-respuesta del LSP; lo leído es un reinicio basado en cambios de entorno de build fingerprint y configuración, no en salud del proceso
Abierto:
- run custom event drain
- lsp health check
- clangd alive
- lsp heartbeat
- process exit handler


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: schedule debounced lsp restart, restart lsp for workspace
    extra: apply workspace settings, setup build environment watching

T3
    visto: process build environment updates, restart lsp for workspace, on lsp missing install, schedule debounced lsp restart
    extra: run custom event drain, active environment fingerprint

T4
    visto: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T1=>T4  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T4  mismo objeto: schedule debounced lsp restart
  T3=>T4  mismo objeto: process build environment updates
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → apply workspace settings
  T2 → setup build environment watching
  T3 → run custom event drain
  T3 → active environment fingerprint

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
Cerrado de T1:
leído: restart lsp for workspace, schedule debounced lsp restart

T2 preguntó:
quién llama a schedule debounced lsp restart cuando el servidor LSP deja de responder
Cerrado de T2:
leído: schedule debounced lsp restart

T3 preguntó:
quién invoca el reinicio diferido del LSP cuando se detecta que el servidor falta o no responde
Cerrado de T3:
encontré el objeto de la consulta: el reinicio diferido se invoca vía schedule debounced lsp restart cuyo único caller anclado es setup build environment watching disparado por cambios en el entorno de build, no por fallo directo del LSP . El caso de servidor falta lsp missing install invoca restart lsp for workspace directamente sin debounce. La consulta mezcla dos caminos: el diferido entorno y el inmediato falta .

T4 preguntó:
dónde se detecta que el proceso LSP ha muerto o no responde y se dispara el reinicio automático
Cerrado de T4:
Lo leído no contestó ESA pregunta. Un hop de Abierto/extra no es un hueco.
no encontré la detección de muerte no-respuesta del LSP; lo leído es un reinicio basado en cambios de entorno de build fingerprint y configuración, no en salud del proceso

Lo leído no contestó ESA pregunta. Un hop de Abierto/extra no es un hueco.

Legal ahora: pasar, no_pasar, revisar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). Esta pregunta ya tiene respuesta (no hay objeto). No la relances. Cierra el claim si basta lo acumulado, o formula otra pregunta. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

explorar agotado. Si falta un eslabón, plan (cada fase trae consulta tuya) o cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
