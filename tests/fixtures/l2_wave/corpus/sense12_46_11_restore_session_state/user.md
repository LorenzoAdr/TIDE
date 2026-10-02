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
1. [en_curso] C locator: dónde se guardan los archivos abiertos al cerrar el proyecto
2. [pendiente] D locator: dónde se restauran los archivos abiertos al abrir el proyecto
3. [pendiente] Q puente: hay camino guardar y restaurar los archivos abiertos (need C D)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el proyecto
keep: M1
leído: Cursor Pos, editor state hpp, editor text hpp
Cerrado:
leído: Cursor Pos, editor state hpp, editor text hpp
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
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré camino de guardar y restaurar la posición del cursor; lo leído son métodos de normalización de rango, selección y acceso a campos col, set primary sin serialización ni persistencia
Abierto:
- save cursor pos
- restore cursor pos
- serialize cursor

### Trabajo 4
consulta: dónde se guardan los archivos abiertos al cerrar el proyecto
keep: M1
leído: save workspace session, restore workspace session, flush active tab, open file
Cerrado:
leído: save workspace session, restore workspace session, flush active tab, open file
Abierto:
- run custom event drain
- handle navigation


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp, editor text hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T4
    visto: save workspace session, restore workspace session, flush active tab, open file
    extra: run custom event drain, apply pending connection

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  mismo objeto: Cursor Pos
  T1=>T4  sin camino
  T2=>T3  mismo objeto: Cursor Pos
  T2=>T4  sin camino
  T3=>T4  sin camino
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
leído: Cursor Pos, editor state hpp, editor text hpp

T2 preguntó:
dónde se restaura la posición del cursor al abrir el proyecto
Cerrado de T2:
leído: Cursor Pos

T3 preguntó:
hay camino guardar y restaurar la posición del cursor
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré camino de guardar y restaurar la posición del cursor; lo leído son métodos de normalización de rango, selección y acceso a campos col, set primary sin serialización ni persistencia

T4 preguntó:
dónde se guardan los archivos abiertos al cerrar el proyecto
Cerrado de T4:
leído: save workspace session, restore workspace session, flush active tab, open file

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: pasar, no_pasar, revisar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
