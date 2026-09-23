Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    restaurar sesión ~ (sin olor)
    guardar estado ~ (sin olor)
    layout ~ editor (c=0.55, twin ui)
    cursor ~ terminal (c=0.46)
    paneles ~ ui (c=1.00)
  entre: ui→terminal ui→editor editor→terminal
  nota: el mapa no une; un explorar cubre un barrio

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=9  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M2  kind=object  ov=0  stems: main layout
owns: main layout
gap: state,trigger,effect
peek: main layout AiController
M4  kind=other  ov=6  stems: app session raw pty screen
owns: app session
gap: state,effect
peek: shell session cursor col
peek-edge: app session cursor col -call-> raw pty screen cursor col
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
owns: ai controlle

Fichas ampliadas (pack+inspect. El inspect elige barrio; no copies símbolos; el fenómeno del ancla sí):
n=3  (owns+nucleus+peek+circuito; sin inspect gordo)
M1  kind=other  ov=6
owns: editor state
peek: editor state CursorPos
M3  kind=other  ov=6
owns: app session
peek: shell session cursor row
M5  kind=object  ov=5
owns: editor panel
peek: editor panel end mouse selection
entre abiertas:
(ningún port)
## M1
kind=other
owns: editor state
nuclei:
anchors:
- G1: editor state CursorPos[q0]
closure: 0/1 groups, hub-free=0
roles:
skeleton missing: state trigger effect
causal edges:
mini-cards:
- editor state CursorPos | CursorPos(?) | roles=unknown
risks: no state nucleus
## M3
kind=other
owns: app session
nuclei:
anchors:
- G1: shell session cursor row[q12] -> raw pty screen cursor row[q10] -> app session cursor row[q9]
closure: 0/1 groups, hub-free=0
roles:
mechanism:
- trigger: app session cursor row -call-> raw pty screen cursor row
skeleton missing: state effect
causal edges:
- app session cursor row -call-> raw pty screen cursor row [trigger]
- shell session cursor row -call-> raw pty screen cursor row [support]
mini-cards:
- shell session cursor row | int ShellSession cursor row() | roles=lock | hot=lock
· 413: std lock guard<std mutex> lock(terminal mutex );
· 414: return terminal .cursor row();
- raw pty screen cursor row | cursor row(?) | roles=unknown
- app session cursor row | int AppSession cursor row() | roles=lock | hot=lock
· 327: std lock guard<std mutex> lock(terminal mutex );
· 328: return terminal .cursor row();
risks: no state nucleus
## M5
kind=object
owns: editor panel
nuclei:
anchors:
- G1: editor panel end mouse selection[q2]
closure: 0/1 groups, hub-free=0
roles:
skeleton missing: state trigger effect
causal edges:
mini-cards:
- editor panel end mouse selection | void end mouse selection(EditorPanelState* panel) | roles=mutator | writes=panel.line select anchor,panel.line select drag,panel.mouse selecting | hot=symptom edge
· 4437: panel->mouse selecting = false;
· 4438: panel->line select drag = false;
· 4439: panel->line select anchor = -1;
· 4443: panel->captured mouse.reset();
risks: no state nucleus
## uncovered seeds
- q3 ai controller make store progress
- q5 level1 agent rank index needle candidates
- q7 level2 debrief collect from session observations
- q8 level2 autonomous loop slim ranked map for prompt
- q15 visual highlight mark visual highlight cursor dirty

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el proyecto
keep: M2 M4
leído: save, remember
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que guarda la posición del cursor al cerrar el proyecto; lo leído confirma que save persiste configuración de entorno temas ai pero NO estado del editor, y remember solo guarda la ruta del workspace. El sistema cursor history solo gestiona navegación back forward en memoria, sin persistencia a disco.
Abierto:
- save state
- serialize cursor
- persist position

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el proyecto
keep: M8 M11
leído: restore workspace session, open file
Cerrado:
leído: restore workspace session, open file
Abierto:
- handle navigation

### Trabajo 3
consulta: dónde se guarda el estado del editor al cerrar el proyecto
keep: M1 M12
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda el estado del editor al cerrar el proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save, remember
    extra: make intercept tool name, theme name

T2
    visto: restore workspace session, open file
    extra: Application, set workspace

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → make intercept tool name
  T1 → theme name
  T2 → Application
  T2 → set workspace

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se guarda la posición del cursor al cerrar el proyecto
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que guarda la posición del cursor al cerrar el proyecto; lo leído confirma que save persiste configuración de entorno temas ai pero NO estado del editor, y remember solo guarda la ruta del workspace. El sistema cursor history solo gestiona navegación back forward en memoria, sin persistencia a disco.

T2 preguntó:
dónde se restaura la posición del cursor al abrir el proyecto
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: restore workspace session, open file

T3 preguntó:
dónde se guarda el estado del editor al cerrar el proyecto
Cerrado de T3:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
(vacío)

Esta pregunta ya tiene respuesta (no hay objeto). No la relances ni en otro keep. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
Un leído: de otra caza no afirma el ancla; no lo parafrasees como el mecanismo.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
