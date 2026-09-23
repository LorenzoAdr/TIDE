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
1. [paso] A locator: dónde se guarda y restaura la posición del cursor al abrir un proyecto
2. [paso] B locator: dónde se guarda y restaura la lista de archivos abiertos al abrir un proyecto
3. [en_curso] C locator: dónde se guarda y restaura la visibilidad de los paneles laterales al abrir un proyecto

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M8
leído: seek, load working lines from disk
Cerrado:
leído: seek, load working lines from disk
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda y restaura la lista de archivos abiertos al abrir un proyecto
keep: M2 M5
leído: load, save, reopen workspace documents, process pending workspace load, remember, set workspace, save workspace session, restore workspace session
Cerrado:
encontré el mecanismo completo: la lista de archivos abiertos se guarda en save workspace session que serializa workspace tabs a open tabs y llama a session save y se restaura en restore workspace session que carga la sesión y abre los archivos vía workspace open file . Ambos son invocados por set workspace al abrir el proyecto.
Abierto:
- run background generation
- run custom event drain

### Trabajo 3
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M10
leído: workspace session hpp, save workspace session, restore workspace session
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la persistencia de la visibilidad de los paneles laterales; lo leído Workspace Session solo serializa pestañas abiertas, argumentos de lanzamiento y programas de depuración, sin ningún campo para el estado de los paneles visible oculto
Abierto:
- run custom event drain
- visibility state
- serialize panels
- panel visibility


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: seek, load working lines from disk
    extra: open git diff view, open git diff tab

T2
    visto: load, save, reopen workspace documents, process pending workspace load
    extra: ensure wake fd, open host pty

T3
    visto: workspace session hpp, save workspace session, restore workspace session
    extra: run custom event drain, apply pending connection

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: save workspace session
hacia el resto:
  T1 → open git diff view
  T1 → open git diff tab
  T2 → ensure wake fd
  T2 → open host pty
  T3 → run custom event drain
  T3 → apply pending connection

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se guarda y restaura la posición del cursor al abrir un proyecto
Cerrado de T1:
leído: seek, load working lines from disk

T2 preguntó:
dónde se guarda y restaura la lista de archivos abiertos al abrir un proyecto
Cerrado de T2:
encontré el mecanismo completo: la lista de archivos abiertos se guarda en save workspace session que serializa workspace tabs a open tabs y llama a session save y se restaura en restore workspace session que carga la sesión y abre los archivos vía workspace open file . Ambos son invocados por set workspace al abrir el proyecto.

T3 preguntó:
dónde se guarda y restaura la visibilidad de los paneles laterales al abrir un proyecto
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la persistencia de la visibilidad de los paneles laterales; lo leído Workspace Session solo serializa pestañas abiertas, argumentos de lanzamiento y programas de depuración, sin ningún campo para el estado de los paneles visible oculto

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). Esta pregunta ya tiene respuesta (no hay objeto). No la relances ni en otro keep. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

El último Cerrado negó el mapeo. Cierra ESE mapeo (hueco o sin camino), no selles un polo que sí se leyó.
