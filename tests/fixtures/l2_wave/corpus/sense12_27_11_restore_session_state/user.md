Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=9  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
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
peek: level2 debrief collect from session observations, level2 deb

Fichas ampliadas (pack+inspect. El inspect elige barrio; no copies símbolos; el fenómeno del ancla sí):
n=3  (owns+nucleus+peek+circuito; sin inspect gordo)
M1  kind=other  ov=6
owns: editor state
peek: editor state CursorPos
M5  kind=object  ov=5
owns: editor panel
peek: editor panel end mouse selection
M6  kind=caller  ov=5
owns: panel
peek: panel ModalInputLine
port: M6=>M7 panel ModalInputLine -call-> raw pty screen text
entre abiertas:
(ningún port)
hacia el resto:
M6=>M7 panel ModalInputLine -call-> raw pty screen text
M6=>M7 file picker MakeFilePickerOverlay -call-> panel ModalInputLine
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
## M6
kind=caller
owns: panel
nuclei:
anchors:
- G1: panel ModalInputLine[q4]
closure: 0/1 groups, hub-free=0
roles:
skeleton missing: state trigger effect
ports:
- M6=>M7 panel ModalInputLine -call-> raw pty screen text
- M6=>M7 file picker MakeFilePickerOverlay -call-> panel ModalInputLine
causal edges:
- panel ModalInputLine -call-> raw pty screen text [port]
- file picker MakeFilePickerOverlay -call-> panel ModalInputLine [port]
mini-cards:
- panel ModalInputLine | inline Element ModalInputLine(const std string& text line) | roles=mutator | writes=row | reads=EQUAL,HEIGHT,TabIdle
· 80: const bool has cursor = !content.empty() && content.back() == ' ';
· 82: content.pop back();
· 86: parts.push back(text(" " + content) | color(theme WatchInput()));
· 88: parts.push back(text(" ") | cursor blink cell decorator());
risks: no state nucleus
## uncovered seeds
- q3 ai controller make store progress
- q5 level1 agent rank index needle candidates
- q7 level2 debrief collect from session observations
- q8 level2 autonomous loop slim ranked map for prompt
- q15 visual highlight mark visual highlight cursor dirty

Plan de búsqueda (romper):
1. [paso] A locator: dónde se restaura la posición del cursor al abrir un proyecto
2. [paso] B locator: dónde se restauran los archivos abiertos al abrir un proyecto
3. [fallo] C locator: dónde se restauran los paneles laterales al abrir un proyecto
4. [pendiente] P puente: hay un mecanismo común que restaura el estado del editor al abrir un proyecto (need A B C)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M1
leído: Cursor Pos, open file at impl, open file at confirmed, reset to single cursor
Cerrado:
encontré el mecanismo de restauración de la posición del cursor al abrir un proyecto: la cadena es open file at confirmed → open file at impl → reset to single cursor line, col → set pos line, col . La posición se restaura en reset to single cursor, que es llamado desde open file at impl con los argumentos line y col recibidos en la apertura.
Abierto:
- open file at confirmed caller

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1
leído: restore workspace session, open file
Cerrado:
encontré el mecanismo: restore workspace session itera session open tabs y llama a workspace open file path para cada archivo válido, y luego activa el active tab path
Abierto:
- handle navigation

### Trabajo 3
consulta: dónde se restauran los paneles laterales al abrir un proyecto
keep: M2
leído: restore workspace session, open file, Ai Controller
Cerrado:
leído: restore workspace session, open file, Ai Controller
Abierto:
- handle navigation


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, open file at impl, open file at confirmed, reset to single cursor
    extra: cursor selection span on line, ensure cursors

T2
    visto: restore workspace session, open file
    extra: Application, set workspace

T3
    visto: restore workspace session, open file, Ai Controller
    extra: Application, set workspace

entre abiertas:
  T1=>T2  open file at impl → open file impl → open new tab from disk → open file
  T1=>T3  open file at impl → open file impl → open new tab from disk → open file
  T2=>T3  mismo objeto: restore workspace session
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → Application
  T2 → set workspace
  T3 → Application
  T3 → set workspace

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se restaura la posición del cursor al abrir un proyecto
Cerrado de T1:
encontré el mecanismo de restauración de la posición del cursor al abrir un proyecto: la cadena es open file at confirmed → open file at impl → reset to single cursor line, col → set pos line, col . La posición se restaura en reset to single cursor, que es llamado desde open file at impl con los argumentos line y col recibidos en la apertura.

T2 preguntó:
dónde se restauran los archivos abiertos al abrir un proyecto
Cerrado de T2:
encontré el mecanismo: restore workspace session itera session open tabs y llama a workspace open file path para cada archivo válido, y luego activa el active tab path

T3 preguntó:
dónde se restauran los paneles laterales al abrir un proyecto
Cerrado de T3:
leído: restore workspace session, open file, Ai Controller

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Lee Cerrado. Cierra si el claim ya se confirma o se refuta, o suelta el plan y sigue un port.
JSON ahora. Primer carácter `{`.

explorar agotado. Si falta un eslabón, plan (cada fase trae consulta tuya) o cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
