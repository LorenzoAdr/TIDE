Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    sesión ~ terminal (c=0.38, twin ui)
    persistencia ~ (sin olor)
    layout ~ editor (c=0.55, twin ui)
    panel ~ ui (c=1.00)
  entre: editor→ui editor→terminal terminal→ui
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

Plan de búsqueda (romper):
1. [fallo] A locator: dónde se guarda la posición del cursor al cerrar el editor
2. [paso] B locator: dónde se guarda la lista de archivos abiertos al cerrar el editor
3. [en_curso] C locator: dónde se guarda el estado de los paneles laterales al cerrar el editor
4. [pendiente] P puente: hay camino guardar el estado al cerrar y restaurarlo al abrir (need A B C)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1 M9
leído: on closed
Cerrado:
leído: on closed
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda la lista de archivos abiertos al cerrar el editor
keep: M1
leído: clear tabs, open tabs mru, workspace model hpp
Cerrado:
encontré dónde se guarda la lista: en los miembros vector Editor Tab tabs y string tab mru de la estructura Workspace Model declarados en workspace model . La función clear tabs confirma que estos son los contenedores que se vacían al cerrar.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda el estado de los paneles laterales al cerrar el editor
keep: M7
leído: workspace model hpp, clear tabs
Cerrado:
leído: workspace model hpp, clear tabs
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on closed
    extra: jump to selected, cancel ctrl chord

T2
    visto: clear tabs, open tabs mru, workspace model hpp
    extra: set welcome buffer, normalize path

T3
    visto: workspace model hpp, clear tabs
    extra: set welcome buffer

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: clear tabs
hacia el resto:
  T1 → jump to selected
  T1 → cancel ctrl chord
  T2 → set welcome buffer
  T2 → normalize path
  T3 → set welcome buffer

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se guarda la posición del cursor al cerrar el editor
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: on closed

T2 preguntó:
dónde se guarda la lista de archivos abiertos al cerrar el editor
Cerrado de T2:
encontré dónde se guarda la lista: en los miembros vector Editor Tab tabs y string tab mru de la estructura Workspace Model declarados en workspace model . La función clear tabs confirma que estos son los contenedores que se vacían al cerrar.

T3 preguntó:
dónde se guarda el estado de los paneles laterales al cerrar el editor
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: workspace model hpp, clear tabs

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase. Lo leído no era el disparo. No pases como si afirmara. no_pasar o cierra ese mapeo. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
