Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=12  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M1  kind=other  ov=6  stems: editor state
owns: editor state
gap: state,trigger,effect
peek: editor state CursorPos
M2  kind=object  ov=0  stems: main layout
owns: main layout
gap: state,trigger,effect
peek: main layout AiController
M3  kind=other  ov=6  stems: app session raw pty screen
owns: app session
gap: state,effect
peek: shell session cursor row
peek-edge: app session cursor row -call-> raw pty screen cursor row
M4  same=M3  kind=other  ov=6  stems: app session
peek: shell session cursor col
M5  kind=object  ov=5  stems: editor panel
owns: editor panel
gap: state,trigger,effect
peek: editor panel end mouse selection
M6  kind=caller  ov=5  stems: panel
owns: panel
gap: state,trigger,effect
peek: panel ModalInputLine
port: M6=>M7 panel ModalInputLine -call-> raw pty screen text
M7  kind=caller  ov=5  stems: file picker file picker
owns: file picker
nucleus: ctrl chord active, ctrl chord armed
peek: file picker MakeFilePickerOverlay, file picker cancel ctrl chord
peek-edge: file picker cancel ctrl chord -write-> ctrl chord active
M8  kind=caller  ov=0  stems: level2 session level2 session
owns: level2 session
nucleus: search
peek: level2 session hunk shape error, level2 session hunk expands over opener
peek-edge: level2 session split mixed sibling hunks -write-> search
M9  kind=hole  ov=0  stems: intent embed attempted  llama backend visual highlight ai trace
owns: intent embed attempted  / ai controller make store progress
nucleus: intent embed attempted 
peek: ai controller make store progress, ai controller begin download
peek-edge: ai controller make store progress -call-> ai controller begin download
M10  kind=hole  ov=0  stems: editor text llama backend level1 action level0 intent index
owns: editor text
gap: no state
peek: level1 agent rank index needle candidates, level1 agent ascii lower simple
peek-edge: level1 agent rank index needle candidates -call-> level1 agent ascii lower simple
M11  kind=hole  ov=5  stems: ai controller level2 autonomous loop level0 intent index compile commands lookup
owns: ai controller
gap: no state
peek: level2 debrief collect from session observations, level2 debrief push unique
peek-edge: level2 debrief collect from session observations -call-> level2 debrief push unique
M12  kind=hole  ov=5  stems: embedding backend l2 effect registry ai missing toast open file confirm
owns: embedding backend
gap: no state
peek: level2 autonomous loop slim ranked map for prompt, ai controller clear
peek-edge: level2 autonomous loop slim ranked map for prompt -call-> ai controller clear
bridges: T3[M9,M12] T4[M9,M12]
holes: ai controller make store progress level1 agent rank index needle candidates level2 debrief collect from session observations level2 autonomous loop slim ranked map for prompt

Plan de búsqueda (romper):
1. [en_curso] A locator: dónde se guarda la posición del cursor al cerrar el editor
2. [pendiente] B locator: dónde se restaura la posición del cursor al abrir el editor
3. [pendiente] C locator: dónde se guarda y restaura la lista de archivos abiertos al cerrar y abrir el proyecto
4. [pendiente] D locator: dónde se guarda y restaura la visibilidad de los paneles laterales al cerrar y abrir el proyecto

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor y los archivos abiertos al cerrar y abrir el proyecto
keep: M1 M8
leído: save state
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardado restauración del estado del editor cursor archivos; lo leído save state serializa exclusivamente el estado de la sesión de IA turnos, acciones, pendientes, no el estado visual del editor.
Abierto:
- save editor state
- restore editor state

### Trabajo 2
consulta: dónde se persiste y recupera la posición del cursor y la lista de archivos abiertos al salir y entrar al proyecto
keep: M10 M11
leído: reset to single cursor, reset helix editor state, save workspace session, flush active tab, save
Cerrado:
leído: reset to single cursor, reset helix editor state, save workspace session, flush active tab, save
Abierto:
- handle editor keys
- run custom event drain
- run background generation

### Trabajo 3
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1 M9
leído: close, save
Cerrado:
leído: close, save
Abierto:
- run background generation


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save state
    extra: state path, write file

T2
    visto: reset to single cursor, reset helix editor state, save workspace session, flush active tab
    extra: set pos, start command

T3
    visto: close, save
    extra: request start, discover repos

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: save
hacia el resto:
  T1 → state path
  T1 → write file
  T2 → set pos
  T2 → start command
  T3 → request start
  T3 → discover repos

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se guarda y restaura la posición del cursor y los archivos abiertos al cerrar y abrir el proyecto
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardado restauración del estado del editor cursor archivos; lo leído save state serializa exclusivamente el estado de la sesión de IA turnos, acciones, pendientes, no el estado visual del editor.

T2 preguntó:
dónde se persiste y recupera la posición del cursor y la lista de archivos abiertos al salir y entrar al proyecto
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: reset to single cursor, reset helix editor state, save workspace session, flush active tab, save

T3 preguntó:
dónde se guarda la posición del cursor al cerrar el editor
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: close, save

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase. Lo leído no era el disparo. No pases como si afirmara. no_pasar o cierra ese mapeo. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
