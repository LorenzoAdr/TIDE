Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=10  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
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
M11  kind=hole  ov=5  stems: ai controller lev

Fichas ampliadas (pack+inspect. El inspect elige barrio; no copies símbolos; el fenómeno del ancla sí):
n=2  (owns+nucleus+peek+circuito; sin inspect gordo)
M1  kind=other  ov=6
owns: editor state
peek: editor state CursorPos
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
1. [paso] A locator: dónde se guarda la posición del cursor al cerrar el proyecto
2. [paso] B locator: dónde se restaura la posición del cursor al abrir el proyecto
3. [fallo] P puente: hay camino guardar y restaurar la posición del cursor (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el proyecto
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el proyecto
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: hay camino guardar y restaurar la posición del cursor
keep: M1
leído: Cursor Pos, editor state hpp
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré el objeto de la consulta; lo leído son getters setters de coordenadas primary line, set primary y lógica de selección, sin métodos de serialización guardar restaurar ni camino ellos
Abierto:
- save cursor state
- restore cursor state
- serialize cursor


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, set pos, set primary
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  mismo objeto: Cursor Pos
  T2=>T3  mismo objeto: Cursor Pos
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → cursor selection span on line
  T3 → ensure cursors

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se guarda la posición del cursor al cerrar el proyecto
Cerrado de T1:
leído: Cursor Pos

T2 preguntó:
dónde se restaura la posición del cursor al abrir el proyecto
Cerrado de T2:
leído: Cursor Pos

T3 preguntó:
hay camino guardar y restaurar la posición del cursor
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré el objeto de la consulta; lo leído son getters setters de coordenadas primary line, set primary y lógica de selección, sin métodos de serialización guardar restaurar ni camino ellos

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.

Legal ahora: plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Lee Cerrado. Cierra si el claim ya se confirma o se refuta, o suelta el plan y sigue un port.
JSON ahora. Primer carácter `{`.
