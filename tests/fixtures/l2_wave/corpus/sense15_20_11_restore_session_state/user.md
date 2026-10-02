Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    editor state ~ ui (c=0.48)
    main layout ~ ui (c=0.88)
    panel ~ ui (c=1.00)
  entre: (sin arista de barrio)
  nota: un barrio concentra estos olores

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

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
keep: M1 M9
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda la posición del cursor del editor al cerrar el proyecto
keep: M1 M7
leído: reset to single cursor
Cerrado:
leído: reset to single cursor
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la lista de archivos abiertos al abrir un proyecto
keep: M1 M5
leído: reopen workspace documents, run, restore workspace session
Cerrado:
encontré el objeto de la consulta: la restauración de la lista de archivos abiertos al abrir un proyecto ocurre en restore workspace session 1395 1416 . Esta función carga un Workspace Session desde el disco load itera sobre session open tabs y llama a workspace open file para cada archivo válido. También restaura la pestaña activa si existe. El flujo de disparo se confirma en los callers: es invocada por set workspace y por el constructor Application lo que cubre tanto la apertura explícita de un proyecto como la restauración de sesión al inicio.
Abierto:
- restart lsp for workspace
- open file


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: reset to single cursor
    extra: set pos, handle shortcuts settings keys

T3
    visto: reopen workspace documents, run, restore workspace session
    extra: restart lsp for workspace, on lsp missing install

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  Make Main Layout → Make Editor Panel → replace text range with caret → reset to single cursor
hacia el resto:
  T2 → set pos
  T2 → handle shortcuts settings keys
  T3 → restart lsp for workspace
  T3 → on lsp missing install

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
Cerrado de T1:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
(vacío)

T2 preguntó:
dónde se guarda la posición del cursor del editor al cerrar el proyecto
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: reset to single cursor

T3 preguntó:
dónde se restaura la lista de archivos abiertos al abrir un proyecto
Cerrado de T3:
encontré el objeto de la consulta: la restauración de la lista de archivos abiertos al abrir un proyecto ocurre en restore workspace session 1395 1416 . Esta función carga un Workspace Session desde el disco load itera sobre session open tabs y llama a workspace open file para cada archivo válido. También restaura la pestaña activa si existe. El flujo de disparo se confirma en los callers: es invocada por set workspace y por el constructor Application lo que cubre tanto la apertura explícita de un proyecto como la restauración de sesión al inicio.

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.
Un leído: de otra caza no afirma el disparo; no selles el ancla con esos nombres.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
