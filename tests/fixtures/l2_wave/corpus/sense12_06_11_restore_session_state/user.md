Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=9  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
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
M11  kind=hole  ov=5  stems: ai controller level2 autonomous loop level0 intent index compile commands lookup
owns: ai controller
gap: no state
peek: level2 debrief

Fichas ampliadas (pack+inspect. El inspect elige barrio; no copies símbolos; el fenómeno del ancla sí):
n=3  (owns+nucleus+peek+circuito; sin inspect gordo)
M1  kind=other  ov=6
owns: editor state
peek: editor state CursorPos
M2  kind=object  ov=0
owns: main layout
peek: main layout AiController
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
## M2
kind=object
owns: main layout
nuclei:
anchors:
- G1: main layout AiController[q1]
closure: 0/1 groups, hub-free=0
roles:
skeleton missing: state trigger effect
causal edges:
mini-cards:
- main layout AiController | AiController(?) | roles=unknown
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

Trabajos ya hechos:
(ninguno)

Legal ahora: explorar, ampliar, plan, cerrar
Abiertas están completas (inspect). entre abiertas son ports, no cosine. Atomiza: cada encargo es un trozo que un hijo cierra. Si el ancla enumera dos disparos, la consulta nombra UNO de lo visto. Si junta un gesto y un efecto, no asumas el arco: parte o pregunta si hay camino después de leer los dos polos. Si cabe en un tirón, explorar: consulta = fenómeno y verbo del usuario (apagar, capturar, detener); el inspect elige el barrio, no el caller del writer. No una plantilla ('invoca la función que…') ni el ancla copiado. Nombrar el fenómeno no es recap; compartir palabras con el ancla tampoco. Si el trabajo cruza fronteras que un hijo no une, plan: cada locator y el puente traen consulta tuya; hacia es cerca, no el briefing. El runtime no redacta. why = por qué ese grano; el hijo no la lee. hacia opcional: 1–3 palabras, no el texto de un port (si es largo se recorta). Ampliar solo ids que aún no estén abiertos. No copies ::símbolos ni stems. Chrome no es el disparo. kind=hole que solo rima con el ancla no es el objeto. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

El plan son 2–4 fases (locator / puente / seguir), no una lista de pasos.
