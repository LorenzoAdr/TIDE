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
consulta: dónde se captura la tecla Escape o el clic fuera para interrumpir la generación de la IA
keep: M2 M7
leído: ai trace escape, handle console panel mouse
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para interrumpir la IA; lo leído es un escape de caracteres de string ai trace escape y un handler de ratón que gestiona selección y hover, pero no interrupción de generación
Abierto:
- stop generation
- handle key event

### Trabajo 2
consulta: dónde se cancela la generación de código y se limpia el archivo a medias
keep: M11 M12
leído: cancel current, cancel inflight completion, clear pending insert, cancel all, cancel level1, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el objeto de la consulta: la cancelación se dispara en handle user input handle route al detectar cancel o clic fuera si mapeado a esa ruta, ejecutando cancel current . Este método limpia el estado pendiente clear pending insert y, si hay generación activa, invoca cancel all que marca agent cancel y cancela tareas. La limpieza del archivo a medias corresponde a clear pending insert que resetea pending insert y insert anchor y la interrupción del agente vía cancel level1 tasks cancel . No se encontró lógica explícita de truncado de buffer de texto en el editor, solo la invalidación del estado de inserción pendiente.
Abierto:
- send completion request
- truncate buffer

### Trabajo 3
consulta: dónde se mapea la tecla Escape o el clic fuera a la ruta de cancelación de la generación
keep: M3 M7
leído: handle completion keys, handle editor keys, handle user input, handle route, is cancel input, cancel current
Cerrado:
encontré el objeto de la consulta: la tecla Escape se mapea a la cancelación en dos capas distintas. 1 En la UI del editor handle completion keys Escape cierra la lista de completados completion close . 2 En el controlador de IA handle user input la entrada de texto cancel detectada por is cancel input o la ruta Cancel Agent procesada en handle route invocan cancel current que a su vez llama a cancel all para detener la generación. El clic fuera no se mapea explícitamente a cancelación de IA en los símbolos leídos; handle completion keys solo maneja Escape para cerrar la UI de completado, y handle user input solo procesa comandos de texto.
Abierto:
- handle editor mouse
- handle mouse event


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, handle console panel mouse
    extra: Is Empty, app tab active

T2
    visto: cancel current, cancel inflight completion, clear pending insert, cancel all
    extra: completions at, async worker main

T3
    visto: handle completion keys, handle editor keys, handle user input, handle route
    extra: Character, event is completion

entre abiertas:
  T1=>T2  cancel current → ai trace escape
  T1=>T3  handle user input → ai trace escape
  T2=>T3  mismo objeto: cancel current
hacia el resto:
  T1 → Is Empty
  T1 → app tab active
  T2 → completions at
  T2 → async worker main
  T3 → Character
  T3 → event is completion

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se captura la tecla Escape o el clic fuera para interrumpir la generación de la IA
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para interrumpir la IA; lo leído es un escape de caracteres de string ai trace escape y un handler de ratón que gestiona selección y hover, pero no interrupción de generación

T2 preguntó:
dónde se cancela la generación de código y se limpia el archivo a medias
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el objeto de la consulta: la cancelación se dispara en handle user input handle route al detectar cancel o clic fuera si mapeado a esa ruta, ejecutando cancel current . Este método limpia el estado pendiente clear pending insert y, si hay generación activa, invoca cancel all que marca agent cancel y cancela tareas. La limpieza del archivo a medias corresponde a clear pending insert que resetea pending insert y insert anchor y la interrupción del agente vía cancel level1 tasks cancel . No se encontró lógica explícita de truncado de buffer de texto en el editor, solo la invalidación del estado de inserción pendiente.

T3 preguntó:
dónde se mapea la tecla Escape o el clic fuera a la ruta de cancelación de la generación
Cerrado de T3:
encontré el objeto de la consulta: la tecla Escape se mapea a la cancelación en dos capas distintas. 1 En la UI del editor handle completion keys Escape cierra la lista de completados completion close . 2 En el controlador de IA handle user input la entrada de texto cancel detectada por is cancel input o la ruta Cancel Agent procesada en handle route invocan cancel current que a su vez llama a cancel all para detener la generación. El clic fuera no se mapea explícitamente a cancelación de IA en los símbolos leídos; handle completion keys solo maneja Escape para cerrar la UI de completado, y handle user input solo procesa comandos de texto.

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
