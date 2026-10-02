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

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: quién llama a schedule debounced lsp restart cuando el servidor LSP deja de responder
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: quién llama a schedule debounced lsp restart cuando el servidor LSP deja de responder
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se invoca lsp missing install cuando falta el servidor LSP
keep: M1
leído: lsp missing prompt for status key, maybe show lsp missing toast, Application
Cerrado:
encontré el objeto de la consulta: la invocación ocurre en el constructor de Application Application, línea 327, donde se detecta que falta el LSP y se encola una tarea UI para llamar a maybe show lsp missing toast
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: schedule debounced lsp restart

T3
    visto: lsp missing prompt for status key, maybe show lsp missing toast, Application
    extra: is lsp missing status key, find tuide source root

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T3  Application → set workspace → maybe generate host compile commands → restart lsp for workspace
  T2=>T3  Application → set workspace → setup build environment watching → schedule debounced lsp restart
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T3 → is lsp missing status key
  T3 → find tuide source root

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
Cerrado de T1:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
leído: restart lsp for workspace, schedule debounced lsp restart

T2 preguntó:
quién llama a schedule debounced lsp restart cuando el servidor LSP deja de responder
Cerrado de T2:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
(vacío)

T3 preguntó:
dónde se invoca lsp missing install cuando falta el servidor LSP
Cerrado de T3:
encontré el objeto de la consulta: la invocación ocurre en el constructor de Application Application, línea 327, donde se detecta que falta el LSP y se encola una tarea UI para llamar a maybe show lsp missing toast

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: plan, cerrar
explorar agotado. Si falta un eslabón, plan (cada fase trae consulta tuya) o cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
