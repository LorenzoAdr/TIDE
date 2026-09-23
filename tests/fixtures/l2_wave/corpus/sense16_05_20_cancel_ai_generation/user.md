Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=12  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M1  kind=other  ov=0  stems: search replace
owns: search replace
gap: state,trigger,effect
peek: search replace find unique span
M2  kind=caller  ov=6  stems: ai trace
owns: ai trace
gap: state,trigger,effect
peek: ai trace ai trace escape
port: M2=>M3 level2 session json escape -call-> ai trace ai trace escape
M3  kind=caller  ov=6  stems: level2 session level2 session
owns: level2 session
nucleus: phase, err out
peek: level2 session reopen for followup, level2 session apply tool
port: M3=>M8 ai controller clear ai session -call-> ai controller clear
M4  kind=caller  ov=6  stems: file picker file picker
owns: file picker
nucleus: ctrl chord active
peek: file picker MakeFilePickerOverlay, file picker cancel ctrl chord
port: M4=>M6 application open quick file picker -call-> ai controller clear
M5  kind=caller  ov=0  stems: console panel console panel
owns: console panel
nucleus: b row
peek: console panel copy terminal selection, console panel terminal selected text
port: M5=>M6 console panel handle ai console keys -call-> ai controller clear
M6  kind=caller  ov=0  stems: level1 agent level1 agent
owns: level1 agent
nucleus: ctx
peek: level1 agent filter distilled ignore, level1 agent run
port: M6=>M8 ai controller cancel level1 -call-> workspace config load
M7  kind=chrome  ov=4  stems: editor panel editor panel
owns: editor panel
nucleus: scroll
peek: editor panel MakeEditorPanel, editor panel handle breadcrumb click
port: M7=>M6 level1 agent run -call-> editor text clear
M8  kind=latch  ov=6  stems: ai controller ai controller
owns: ai controller
nucleus: pending insert 
peek: ai controller begin insert at, ai controller cancel level1
port: M8=>M6 ai controller cancel level1 -call-> workspace config load
M9  kind=hole  ov=6  stems: app session shell session raw pty screen app session
owns: app session
gap: no state
peek: raw pty screen skip escape, raw pty screen handle csi
peek-edge: raw pty screen skip escape -call-> raw pty screen handle csi
M10  kind=hole  ov=6  stems: gdb launcher thread safe queue git service gdb launcher
owns: gdb launcher
gap: no state
peek: git service unbind remote cancel, thread safe queue reset
peek-edge: git service unbind remote cancel -call-> thread safe queue reset
M11  kind=hole  ov=6  stems: git service git service
owns: git service
gap: no state
peek: git service cancel remote, cancel
peek-edge: git service cancel remote -write-> cancel
M12  kind=hole  ov=6  stems: git command git command
owns: git command
gap: no state
peek: git command request git cancel
bridges: T1[M6,M8] T2[M6,M3] T5[M6,M8]
holes: raw pty screen skip escape git service unbind remote cancel git service cancel remote git command request git cancel

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se cancela la generación de código al pulsar Escape o hacer clic fuera
keep: M1 M7
leído: cancel completion fetch, cancel live lsp on cursor move, handle completion keys, handle editor keys
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el objeto de la consulta: la cancelación al pulsar Escape se gestiona en handle completion keys 5213 llamando a completion close . No encontré evidencia de cancelación al hacer clic fuera; el sistema parece no implementar ese disparo o usa un mecanismo de foco no rastreado en los peeks actuales.
Abierto:
- click outside handler
- focus lost handler

### Trabajo 2
consulta: dónde se limpia el archivo para no dejarlo a medias tras cancelar la generación
keep: M5 M9
leído: is generation, open, cancel debug launch
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de limpieza del archivo tras cancelar la generación; lo leído es la cancelación de la sesión de debug cancel debug launch que mata el proceso pero no borra archivos temporales de generación de código
Abierto:
- debug launch generation active
- cleanup generated file

### Trabajo 3
consulta: dónde se revierte la escritura del archivo cuando se cancela la generación de código
keep: M5 M12
leído: cancel current, clear pending insert, begin insert at, cancel level1, handle user input, run insert async, cancel all
Cerrado:
leído: cancel current, clear pending insert, begin insert at, cancel level1, handle user input, run insert async, cancel all
Abierto:
- handle route
- begin thinking
- end thinking


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel completion fetch, cancel live lsp on cursor move, handle completion keys, handle editor keys
    extra: completion lsp tick, ISymbol Provider

T2
    visto: is generation, open, cancel debug launch
    extra: Tab Hover, Tab Pressed

T3
    visto: cancel current, clear pending insert, begin insert at, cancel level1
    extra: handle route, on ai tab opened
    entre interno: begin insert at → run insert async

entre abiertas:
  T1=>T2  cancel debug launch → exit debug mode → cancel live lsp on cursor move → cancel completion fetch
  T1=>T3  sin camino
  T2=>T3  clear pending insert → pending insert → run insert async → bootstrap level2 session → make l2 deps → any modal open
hacia el resto:
  T1 → completion lsp tick
  T1 → ISymbol Provider
  T2 → Tab Hover
  T2 → Tab Pressed
  T3 → handle route
  T3 → on ai tab opened

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se cancela la generación de código al pulsar Escape o hacer clic fuera
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el objeto de la consulta: la cancelación al pulsar Escape se gestiona en handle completion keys 5213 llamando a completion close . No encontré evidencia de cancelación al hacer clic fuera; el sistema parece no implementar ese disparo o usa un mecanismo de foco no rastreado en los peeks actuales.

T2 preguntó:
dónde se limpia el archivo para no dejarlo a medias tras cancelar la generación
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de limpieza del archivo tras cancelar la generación; lo leído es la cancelación de la sesión de debug cancel debug launch que mata el proceso pero no borra archivos temporales de generación de código

T3 preguntó:
dónde se revierte la escritura del archivo cuando se cancela la generación de código
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: cancel current, clear pending insert, begin insert at, cancel level1, handle user input, run insert async, cancel all

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
exploradores agotados. Lo leído no era el disparo. Cierra ese mapeo; no cites esos nombres como el mecanismo del ancla. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

El último Cerrado negó el mapeo. Cierra ESE mapeo (hueco o sin camino), no selles un polo que sí se leyó.
