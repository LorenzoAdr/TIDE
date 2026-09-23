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
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
keep: M7 M11
leído: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert
Cerrado:
encontré el objeto de la consulta: la captura de Escape y clic fuera para cancelar la generación de código está en handle ai console keys tecla Escape y handle console panel mouse clic fuera . Ambos invocan a clear pending insert para limpiar el estado de inserción pendiente, que es el efecto de cancelación.
Abierto:
- handle route
- cancel current

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se limpia el archivo a medias al cancelar la generación de la IA
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: qué hace clear pending insert para evitar que el archivo quede a medias
keep: M1 M2
leído: clear pending insert, begin insert at, handle user input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo: clear pending insert evita que el archivo quede a medias o en un estado inconsistente limpiando el estado de intención pending insert e insert anchor y notificando a la UI wake, lo que impide que se ejecute una inserción fantasma o duplicada si el usuario cancela o si el flujo se interrumpe. La protección no es a nivel de escritura de archivo no hay rollback de disco, sino a nivel de estado del controlador: al resetear pending insert a false y vaciar insert anchor, se asegura que handle user input no en la rama de ejecución de inserción run insert async con datos obsoletos o nulos.
Abierto:
- handle route
- run insert async


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert
    extra: ai tab active, event is ctrl c

T2
    visto: (nada)

T3
    visto: clear pending insert, begin insert at, handle user input
    extra: cancel current, handle route

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: clear pending insert
  T1=>T3  handle ai console keys → handle user input → handle route → run level1 async → begin insert at
  T2=>T3  sin camino
hacia el resto:
  T1 → ai tab active
  T1 → event is ctrl c
  T3 → cancel current
  T3 → handle route

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
Cerrado de T1:
encontré el objeto de la consulta: la captura de Escape y clic fuera para cancelar la generación de código está en handle ai console keys tecla Escape y handle console panel mouse clic fuera . Ambos invocan a clear pending insert para limpiar el estado de inserción pendiente, que es el efecto de cancelación.

T2 preguntó:
dónde se limpia el archivo a medias al cancelar la generación de la IA
Cerrado de T2:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
(vacío)

T3 preguntó:
qué hace clear pending insert para evitar que el archivo quede a medias
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo: clear pending insert evita que el archivo quede a medias o en un estado inconsistente limpiando el estado de intención pending insert e insert anchor y notificando a la UI wake, lo que impide que se ejecute una inserción fantasma o duplicada si el usuario cancela o si el flujo se interrumpe. La protección no es a nivel de escritura de archivo no hay rollback de disco, sino a nivel de estado del controlador: al resetear pending insert a false y vaciar insert anchor, se asegura que handle user input no en la rama de ejecución de inserción run insert async con datos obsoletos o nulos.

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
exploradores agotados. Lo leído no era el disparo. Cierra ese mapeo; no cites esos nombres como el mecanismo del ancla. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

El último Cerrado negó el mapeo. Cierra ESE mapeo (hueco o sin camino), no selles un polo que sí se leyó.
