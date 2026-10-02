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
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input, handle ai console keys
Cerrado:
encontré el objeto de la consulta: la captura de Escape para cancelar la generación ocurre en handle ai console keys líneas 3048-3055 del peek, donde Escape invoca a ai clear pending insert si hay una inserción pendiente, o ai cancel current si el agente está ocupado. La consulta también menciona clic fuera, pero no hay evidencia de captura de clics para cancelar en el código leído; solo se maneja la tecla Escape y comandos de texto como cancel en is cancel input .
Abierto:
- clic fuera cancelacion

### Trabajo 2
consulta: dónde se limpia el archivo o se revierte la inserción pendiente al cancelar la generación
keep: M8
leído: clear pending insert, cancel current
Cerrado:
encontré el mecanismo de limpieza: al cancelar una inserción pendiente pending insert, se invoca clear pending insert desde cancel current, lo cual resetea el flag y el anchor, pero NO borra el archivo ni revierte texto en el buffer; la reversión de inserción es lógica de estado, no de contenido.
Abierto:
- handle route
- handle user input
- cancel all

### Trabajo 3
consulta: dónde se revierte el contenido del buffer de texto al cancelar la inserción pendiente
keep: M8
leído: clear pending insert, cancel current
Cerrado:
encontré el mecanismo de reversión: en clear pending insert se resetea el estado pending insert false insert anchor y se despierta el sistema wake sin manipulación directa del buffer de texto visible en el cuerpo leído
Abierto:
- handle route
- handle user input
- insert anchor


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: cancel all, Ai Controller

T2
    visto: clear pending insert, cancel current
    extra: handle user input, handle route

T3
    visto: clear pending insert, cancel current
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  mismo objeto: cancel current
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T2=>T3  mismo objeto: clear pending insert
hacia el resto:
  T1 → cancel all
  T1 → Ai Controller
  T2 → cancel all
  T3 → cancel all

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
Cerrado de T1:
encontré el objeto de la consulta: la captura de Escape para cancelar la generación ocurre en handle ai console keys líneas 3048-3055 del peek, donde Escape invoca a ai clear pending insert si hay una inserción pendiente, o ai cancel current si el agente está ocupado. La consulta también menciona clic fuera, pero no hay evidencia de captura de clics para cancelar en el código leído; solo se maneja la tecla Escape y comandos de texto como cancel en is cancel input .

T2 preguntó:
dónde se limpia el archivo o se revierte la inserción pendiente al cancelar la generación
Cerrado de T2:
encontré el mecanismo de limpieza: al cancelar una inserción pendiente pending insert, se invoca clear pending insert desde cancel current, lo cual resetea el flag y el anchor, pero NO borra el archivo ni revierte texto en el buffer; la reversión de inserción es lógica de estado, no de contenido.

T3 preguntó:
dónde se revierte el contenido del buffer de texto al cancelar la inserción pendiente
Cerrado de T3:
encontré el mecanismo de reversión: en clear pending insert se resetea el estado pending insert false insert anchor y se despierta el sistema wake sin manipulación directa del buffer de texto visible en el cuerpo leído

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: plan, cerrar
explorar agotado. Si falta un eslabón, plan (cada fase trae consulta tuya) o cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
