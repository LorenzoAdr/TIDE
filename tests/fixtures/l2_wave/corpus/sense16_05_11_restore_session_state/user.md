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
1. [fallo] A locator: dónde se guarda y restaura la posición del cursor en el archivo al cerrar y abrir un proyecto
2. [en_curso] B locator: dónde se guarda y restaura la visibilidad de los paneles laterales al cerrar y abrir un proyecto
3. [pendiente] P puente: hay camino guardar restaurar posición del cursor y guardar restaurar visibilidad de paneles laterales (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
keep: M1 M9
leído: session pack needles, session path, load, Application, save, set workspace, save workspace session
Cerrado:
encontré el mecanismo de guardado y restauración del estado del editor tabs abiertos y tab activo al cambiar de proyecto. El estado se persiste en session md ruta derivada de workspace root mediante save y se restaura mediante load . El disparo de guardado ocurre en set workspace antes de limpiar tabs y en eventos de cierre; la restauración se invoca en el constructor Application tras detectar el workspace.
Abierto:
- run custom event drain
- Event (restauración explícita post-cierre)

### Trabajo 2
consulta: dónde se guarda y restaura la posición del cursor en el archivo al cerrar y abrir un proyecto
keep: M1 M10
leído: save, load
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardar restaurar la posición del cursor en el archivo al cerrar y abrir un proyecto; lo leído en load solo serializa open tabs y active tab path, sin cursor position ni line column
Abierto:
- save state
- restore state
- save cursor positions

### Trabajo 3
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al cerrar y abrir un proyecto
keep: M7 M10
leído: save workspace session
Cerrado:
leído: save workspace session
Abierto:
- run custom event drain


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: session pack needles, session path, load, Application
    extra: is plan stats telemetry token, looks like code ident

T2
    visto: save, load
    extra: session path, create directories

T3
    visto: save workspace session
    extra: run custom event drain, apply pending connection

entre abiertas:
  T1=>T2  mismo objeto: load
  T1=>T3  mismo objeto: save workspace session
  T2=>T3  sin camino
hacia el resto:
  T1 → is plan stats telemetry token
  T1 → looks like code ident
  T2 → create directories
  T2 → write terminal init script
  T3 → run custom event drain
  T3 → apply pending connection

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
Cerrado de T1:
encontré el mecanismo de guardado y restauración del estado del editor tabs abiertos y tab activo al cambiar de proyecto. El estado se persiste en session md ruta derivada de workspace root mediante save y se restaura mediante load . El disparo de guardado ocurre en set workspace antes de limpiar tabs y en eventos de cierre; la restauración se invoca en el constructor Application tras detectar el workspace.

T2 preguntó:
dónde se guarda y restaura la posición del cursor en el archivo al cerrar y abrir un proyecto
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardar restaurar la posición del cursor en el archivo al cerrar y abrir un proyecto; lo leído en load solo serializa open tabs y active tab path, sin cursor position ni line column

T3 preguntó:
dónde se guarda y restaura la visibilidad de los paneles laterales al cerrar y abrir un proyecto
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: save workspace session

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase. Lo leído no era el disparo. No pases como si afirmara. no_pasar o cierra ese mapeo. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

El último Cerrado negó el mapeo. Cierra ESE mapeo (hueco o sin camino), no selles un polo que sí se leyó.
