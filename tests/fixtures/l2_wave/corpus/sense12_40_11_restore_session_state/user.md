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
1. [fallo] A locator: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
2. [pendiente] P puente: hay camino restaurar el estado del editor y restaurar la visibilidad de los paneles laterales (need A)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al abrir un proyecto
keep: M1
leído: editor state cpp, editor state hpp
Cerrado:
leído: editor state cpp, editor state hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M1
leído: editor state cpp, editor state hpp, set primary, reset to single cursor
Cerrado:
leído: editor state cpp, editor state hpp, set primary, reset to single cursor
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1
leído: editor state hpp, set primary, reset to single cursor
Cerrado:
leído: editor state hpp, set primary, reset to single cursor
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M2 M5
leído: set primary, reset to single cursor
Cerrado:
leído: set primary, reset to single cursor
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: editor state cpp, editor state hpp

T2
    visto: editor state cpp, editor state hpp, set primary, reset to single cursor
    extra: ensure cursors, set pos

T3
    visto: editor state hpp, set primary, reset to single cursor
    extra: ensure cursors, set pos

T4
    visto: set primary, reset to single cursor
    extra: ensure cursors, set pos

entre abiertas:
  T1=>T2  mismo objeto: editor state cpp
  T1=>T3  mismo objeto: editor state hpp
  T1=>T4  sin camino
  T2=>T3  mismo objeto: editor state hpp
  T2=>T4  mismo objeto: set primary
  T3=>T4  mismo objeto: set primary
hacia el resto:
  T2 → ensure cursors
  T2 → set pos
  T3 → ensure cursors
  T3 → set pos
  T4 → ensure cursors
  T4 → set pos

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se guarda y restaura el estado del editor al abrir un proyecto
Cerrado de T1:
leído: editor state cpp, editor state hpp

T2 preguntó:
dónde se restaura la posición del cursor al abrir un proyecto
Cerrado de T2:
leído: editor state cpp, editor state hpp, set primary, reset to single cursor

T3 preguntó:
dónde se restauran los archivos abiertos al abrir un proyecto
Cerrado de T3:
leído: editor state hpp, set primary, reset to single cursor

T4 preguntó:
dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
Cerrado de T4:
leído: set primary, reset to single cursor

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Lee Cerrado. Cierra si el claim ya se confirma o se refuta, o suelta el plan y sigue un port.
JSON ahora. Primer carácter `{`.
