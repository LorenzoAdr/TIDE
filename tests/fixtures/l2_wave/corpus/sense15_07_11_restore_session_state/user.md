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
1. [fallo] A locator: dónde se restaura la posición del cursor al abrir un archivo o proyecto
2. [en_curso] B locator: dónde se restauran los archivos abiertos al volver a abrir un proyecto
3. [pendiente] C locator: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor posición del cursor y archivos abiertos al cerrar y volver a abrir un proyecto
keep: M1 M11
leído: save state, serialize ai settings, serialize ui colors
Cerrado:
leído: save state, serialize ai settings, serialize ui colors
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un archivo o proyecto
keep: M2 M6
leído: open file at impl, open file impl, open file at confirmed, reset to single cursor
Cerrado:
encontré el mecanismo de restauración de posición al abrir un archivo: open file at impl en workspace model llama a buffer reset to single cursor tras cargar el archivo, y reset to single cursor en editor state aplica la posición con cursors front . La consulta se refiere a la restauración al abrir; el flujo leído cubre la apertura con coordenadas explícitas. No he leído el mecanismo de restauración automática desde historial si existe al abrir sin coordenadas, pero el objeto de la consulta restauración al abrir está anclado en estas dos funciones.
Abierto:
- open file impl

### Trabajo 3
consulta: dónde se restauran los archivos abiertos al volver a abrir un proyecto
keep: M1 M7
leído: load
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la restauración de archivos abiertos; lo leído load solo restaura la lista de rutas de proyectos recientes, no el estado de los archivos abiertos en el editor
Abierto:
- open file impl
- open file at impl
- serialize ai settings


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save state, serialize ai settings, serialize ui colors
    extra: state path, write file

T2
    visto: open file at impl, open file impl, open file at confirmed, reset to single cursor
    extra: flush active tab, normalize path

T3
    visto: load
    extra: open host pty, ensure wake fd

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → state path
  T1 → write file
  T2 → flush active tab
  T2 → normalize path
  T3 → open host pty
  T3 → ensure wake fd

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se guarda y restaura el estado del editor posición del cursor y archivos abiertos al cerrar y volver a abrir un proyecto
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: save state, serialize ai settings, serialize ui colors

T2 preguntó:
dónde se restaura la posición del cursor al abrir un archivo o proyecto
Cerrado de T2:
encontré el mecanismo de restauración de posición al abrir un archivo: open file at impl en workspace model llama a buffer reset to single cursor tras cargar el archivo, y reset to single cursor en editor state aplica la posición con cursors front . La consulta se refiere a la restauración al abrir; el flujo leído cubre la apertura con coordenadas explícitas. No he leído el mecanismo de restauración automática desde historial si existe al abrir sin coordenadas, pero el objeto de la consulta restauración al abrir está anclado en estas dos funciones.

T3 preguntó:
dónde se restauran los archivos abiertos al volver a abrir un proyecto
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la restauración de archivos abiertos; lo leído load solo restaura la lista de rutas de proyectos recientes, no el estado de los archivos abiertos en el editor

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase. Lo leído no era el disparo. No pases como si afirmara. no_pasar o cierra ese mapeo. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
