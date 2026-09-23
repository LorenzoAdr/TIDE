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

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M8
leído: editor text hpp, load working lines from disk, reset to single cursor
Cerrado:
leído: editor text hpp, load working lines from disk, reset to single cursor
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la lista de archivos abiertos al volver a abrir un proyecto
keep: M1 M6
leído: reopen workspace documents, load
Cerrado:
encontré el mecanismo: la restauración de la lista de archivos abiertos al volver a abrir un proyecto ocurre en reopen workspace documents . Esta función itera sobre workspace tabs que contiene los archivos del proyecto y llama a symbols document opened para cada uno, restaurando así el estado de los documentos. La función load lee la lista de proyectos recientes, no los archivos abiertos de un proyecto específico.
Abierto:
- restart lsp for workspace

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M7
leído: load working lines from disk, load, reopen workspace documents
Cerrado:
leído: load working lines from disk, load, reopen workspace documents
Abierto:
- restart lsp for workspace


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: editor text hpp, load working lines from disk, reset to single cursor
    extra: open git diff view, open git diff tab

T2
    visto: reopen workspace documents, load
    extra: restart lsp for workspace, on lsp missing install

T3
    visto: load working lines from disk, load, reopen workspace documents
    extra: open git diff view, open git diff tab

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: load working lines from disk
  T2=>T3  mismo objeto: reopen workspace documents
hacia el resto:
  T1 → open git diff view
  T1 → open git diff tab
  T2 → restart lsp for workspace
  T2 → on lsp missing install
  T3 → open git diff view
  T3 → open git diff tab

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se guarda y restaura la posición del cursor al abrir un proyecto
Cerrado de T1:
leído: editor text hpp, load working lines from disk, reset to single cursor

T2 preguntó:
dónde se restaura la lista de archivos abiertos al volver a abrir un proyecto
Cerrado de T2:
encontré el mecanismo: la restauración de la lista de archivos abiertos al volver a abrir un proyecto ocurre en reopen workspace documents . Esta función itera sobre workspace tabs que contiene los archivos del proyecto y llama a symbols document opened para cada uno, restaurando así el estado de los documentos. La función load lee la lista de proyectos recientes, no los archivos abiertos de un proyecto específico.

T3 preguntó:
dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
Cerrado de T3:
leído: load working lines from disk, load, reopen workspace documents

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
