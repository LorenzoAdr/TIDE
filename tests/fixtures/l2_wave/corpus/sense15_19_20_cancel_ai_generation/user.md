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
consulta: dónde se captura la tecla Escape para cancelar la generación de código de la IA
keep: M2 M1
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de captura de la tecla Escape para cancelar la generación; lo leído ai trace escape es una función de escape de caracteres para strings, no un handler de eventos de teclado
Abierto:
- key handler
- on key press
- cancel generation

### Trabajo 2
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código de la IA
keep: M8 M1
leído: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert, cancel current, handle user input, Event
Cerrado:
encontré el mecanismo: la tecla Escape se captura en handle ai console keys rama Escape dentro del bloque ai workflow menu open o selección y el clic fuera se gestiona en handle console panel mouse detectando clics fuera de la zona de input terminal para limpiar selecciones o desactivar menús . Ambos invocan a cancel current o clear pending insert para abortar la generación.
Abierto:
- handle route
- ai workflow menu open

### Trabajo 3
consulta: qué hace cancel current o clear pending insert al abortar la generación de la IA
keep: M1 M6
leído: clear pending insert, cancel current, cancel all, busy, cancel level1
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo: cancel current bifurca según el estado. Si hay una inserción pendiente y no está ocupado, llama a clear pending insert que resetea pending insert e insert anchor y despierta la UI . Si está ocupado, llama a cancel all, que a su vez llama a cancel level1 setea agent cancel a true y cancela las tareas activas en tasks
Abierto:
- handle route
- handle user input


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert
    extra: ai tab active, event is ctrl c

T3
    visto: clear pending insert, cancel current, cancel all, busy
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  handle ai console keys → handle user input → ai trace escape
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T2=>T3  mismo objeto: clear pending insert
  T2=>T3  handle ai console keys → handle user input → handle route → cancel current → cancel all
hacia el resto:
  T2 → ai tab active
  T2 → event is ctrl c
  T3 → handle route
  T3 → Ai Controller

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se captura la tecla Escape para cancelar la generación de código de la IA
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de captura de la tecla Escape para cancelar la generación; lo leído ai trace escape es una función de escape de caracteres para strings, no un handler de eventos de teclado

T2 preguntó:
dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código de la IA
Cerrado de T2:
encontré el mecanismo: la tecla Escape se captura en handle ai console keys rama Escape dentro del bloque ai workflow menu open o selección y el clic fuera se gestiona en handle console panel mouse detectando clics fuera de la zona de input terminal para limpiar selecciones o desactivar menús . Ambos invocan a cancel current o clear pending insert para abortar la generación.

T3 preguntó:
qué hace cancel current o clear pending insert al abortar la generación de la IA
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo: cancel current bifurca según el estado. Si hay una inserción pendiente y no está ocupado, llama a clear pending insert que resetea pending insert e insert anchor y despierta la UI . Si está ocupado, llama a cancel all, que a su vez llama a cancel level1 setea agent cancel a true y cancela las tareas activas en tasks

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
exploradores agotados. Lo leído no era el disparo. Cierra ese mapeo; no cites esos nombres como el mecanismo del ancla. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

El último Cerrado negó el mapeo. Cierra ESE mapeo (hueco o sin camino), no selles un polo que sí se leyó.
