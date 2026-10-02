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
1. [paso] A locator: dónde se guarda la posición del cursor al cerrar el editor
2. [fallo] B locator: dónde se restauran los archivos abiertos al abrir un proyecto
3. [en_curso] C locator: dónde se controla la visibilidad de los paneles laterales
4. [pendiente] P puente: hay un mecanismo común que restaura el estado del editor al abrir un proyecto (need A B C)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: close, close tab, flush active tab
Cerrado:
leído: close, close tab, flush active tab
Abierto:
- run job

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1
leído: restore workspace session, reopen workspace documents, open file, open file impl, open new tab from disk
Cerrado:
encontré el mecanismo: restore workspace session carga Workspace Session y llama a workspace open file para cada pestaña y la activa; open file delega en open file impl y open new tab from disk para cargar el contenido y crear la pestaña en el modelo
Abierto:
- restart lsp for workspace
- handle navigation

### Trabajo 3
consulta: dónde se controla la visibilidad de los paneles laterales
keep: M5
leído: handle source panel event, secondary visible, On Event, Main Layout State
Cerrado:
leído: handle source panel event, secondary visible, On Event, Main Layout State
Abierto:
- handle gdb console keys


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: close, close tab, flush active tab
    extra: request start, run job

T2
    visto: restore workspace session, reopen workspace documents, open file, open file impl
    extra: Application, set workspace

T3
    visto: handle source panel event, secondary visible, On Event, Main Layout State
    extra: Character, handle source scrollbar mouse

entre abiertas:
  T1=>T2  restore workspace session → last launch program → open launch wizard → prepare connection wizard → exit debug mode
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → request start
  T1 → run job
  T2 → Application
  T2 → set workspace
  T3 → Character
  T3 → handle source scrollbar mouse

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se guarda la posición del cursor al cerrar el editor
Cerrado de T1:
leído: close, close tab, flush active tab

T2 preguntó:
dónde se restauran los archivos abiertos al abrir un proyecto
Cerrado de T2:
encontré el mecanismo: restore workspace session carga Workspace Session y llama a workspace open file para cada pestaña y la activa; open file delega en open file impl y open new tab from disk para cargar el contenido y crear la pestaña en el modelo

T3 preguntó:
dónde se controla la visibilidad de los paneles laterales
Cerrado de T3:
leído: handle source panel event, secondary visible, On Event, Main Layout State

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
