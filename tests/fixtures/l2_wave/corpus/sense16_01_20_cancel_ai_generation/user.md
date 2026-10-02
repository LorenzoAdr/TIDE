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
consulta: dónde se captura la tecla Escape o el clic fuera cuando la IA está generando código
keep: M5 M1
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera durante la generación; lo leído es una función de escape de strings para logs ai trace escape, que no tiene relación con eventos de UI
Abierto:
- key event handler
- mouse click handler

### Trabajo 2
consulta: dónde se cancela la generación de código de la IA para evitar dejar el archivo a medias
keep: M3 M10
leído: cancel current, cancel level1, clear pending insert, cancel all, handle user input, handle route, is cancel input, run insert async
Cerrado:
encontré el mecanismo de cancelación: la generación se cancela vía comando de texto cancel detectado en handle user input, que invoca cancel current; este llama a cancel all que setea agent cancel y cancela tasks o clear pending insert si es pendiente . No hay captura de Escape ni clic fuera en el código leído; la cancelación es textual. El efecto de no dejar el archivo a medias se logra abortando la tarea tasks cancel y limpiando el anchor clear pending insert, pero no se lee el código que escribe en el archivo para ver si hace rollback.
Abierto:
- tasks cancel
- agent cancel check in thread

### Trabajo 3
consulta: dónde se limpia el archivo o se deshace la escritura parcial cuando se cancela la generación
keep: M7 M12
leído: rollback pending, clear pending insert, cancel current, handle user input, cancel all, run insert async, cancel level1, handle route
Cerrado:
encontré el mecanismo de limpieza: al cancelar, cancel current invoca cancel all - cancel level1 setea agent cancel y el hilo de run insert async detecta esa bandera y llama a rollback pending que restaura el contenido previo p before de los archivos modificados en st pending y los escribe con write text file
Abierto:
- agent cancel check in run insert async thread body


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: cancel current, cancel level1, clear pending insert, cancel all
    extra: Ai Controller, handle level2 harness

T3
    visto: rollback pending, clear pending insert, cancel current, handle user input
    extra: write text file, load state

entre abiertas:
  T1=>T2  cancel current → ai trace escape
  T1=>T3  rollback pending → apply tool → ai trace escape
  T2=>T3  mismo objeto: cancel current
hacia el resto:
  T2 → Ai Controller
  T2 → handle level2 harness
  T3 → write text file
  T3 → load state

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se captura la tecla Escape o el clic fuera cuando la IA está generando código
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera durante la generación; lo leído es una función de escape de strings para logs ai trace escape, que no tiene relación con eventos de UI

T2 preguntó:
dónde se cancela la generación de código de la IA para evitar dejar el archivo a medias
Cerrado de T2:
encontré el mecanismo de cancelación: la generación se cancela vía comando de texto cancel detectado en handle user input, que invoca cancel current; este llama a cancel all que setea agent cancel y cancela tasks o clear pending insert si es pendiente . No hay captura de Escape ni clic fuera en el código leído; la cancelación es textual. El efecto de no dejar el archivo a medias se logra abortando la tarea tasks cancel y limpiando el anchor clear pending insert, pero no se lee el código que escribe en el archivo para ver si hace rollback.

T3 preguntó:
dónde se limpia el archivo o se deshace la escritura parcial cuando se cancela la generación
Cerrado de T3:
encontré el mecanismo de limpieza: al cancelar, cancel current invoca cancel all - cancel level1 setea agent cancel y el hilo de run insert async detecta esa bandera y llama a rollback pending que restaura el contenido previo p before de los archivos modificados en st pending y los escribe con write text file

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
