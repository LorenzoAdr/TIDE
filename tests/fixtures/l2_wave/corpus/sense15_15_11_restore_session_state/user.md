Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    restaurar sesión ~ (sin olor)
    guardar estado editor ~ ui (c=0.56, twin editor)
    panel lateral visible ~ ui (c=0.79)
  entre: (sin arista de barrio)
  nota: un barrio concentra estos olores

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
consulta: dónde se guarda y restaura el estado de la sesión al abrir un proyecto
keep: M1 M7
leído: save state, load state, state path, write file, read file, dir for
Cerrado:
encontré el mecanismo de guardado y restauración del estado de la sesión: se serializa a JSON en save state y se lee en load state . La ruta se construye en state path usando dir for que apunta a state json .
Abierto:
- handle route
- handle level2 harness
- run level1 async

### Trabajo 2
consulta: qué datos del editor y los paneles se incluyen en el estado que se guarda y restaura
keep: M1 M2 M5
leído: save state, load state
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré datos del editor ni de los paneles en el estado guardado; lo leído load state serializa exclusivamente el estado de la sesión de IA turn, phase, workflow, pending hunks, counters, etc., sin campos de UI como scroll, selección o paneles
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: save state, load state, state path, write file
    extra: contains, is array

T2
    visto: save state, load state
    extra: state path, write file

entre abiertas:
  T1=>T2  mismo objeto: save state
hacia el resto:
  T1 → contains
  T1 → is array
  T2 → contains
  T2 → is array

# examen
Ancla:
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

T1 preguntó:
dónde se guarda y restaura el estado de la sesión al abrir un proyecto
Cerrado de T1:
encontré el mecanismo de guardado y restauración del estado de la sesión: se serializa a JSON en save state y se lee en load state . La ruta se construye en state path usando dir for que apunta a state json .

T2 preguntó:
qué datos del editor y los paneles se incluyen en el estado que se guarda y restaura
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré datos del editor ni de los paneles en el estado guardado; lo leído load state serializa exclusivamente el estado de la sesión de IA turn, phase, workflow, pending hunks, counters, etc., sin campos de UI como scroll, selección o paneles

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: bosquejar, explorar, plan, cerrar
Este es el estado. Tú diriges: atomiza el trabajo (un hijo, un trozo). El plan no es un contrato: tras un Cerrado puedes soltarlo (explorar o plan nuevo). Esta pregunta ya tiene respuesta (no hay objeto). No la relances ni en otro keep. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero. 'mismo objeto' en entre abiertas es que la caza no se movió. Si queda un tirón, explorar; si el trabajo no cabe en uno, plan. consulta = prompt de investigación (fenómeno y verbo; el hijo no ve el ancla); why = tu estrategia (el hijo no la lee). Lo que no se caza no se nombra. hacia opcional: conceptos de cerca, no ids. evita opcional. Un explorar es una sola rama; atomiza, no empaquetes dos cazas en un hijo. tú eliges cómo seguir. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
