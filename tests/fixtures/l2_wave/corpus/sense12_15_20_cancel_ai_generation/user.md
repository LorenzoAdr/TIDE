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
encontré el objeto de la consulta: la captura de Escape para cancelar la generación ocurre en handle ai console keys líneas truncadas 3050-3060, donde Escape invoca a ai clear pending insert si hay una inserción pendiente; el clic fuera no se captura explícitamente en este handler, pero el foco se gestiona vía Take Focus en eventos custom. El efecto de cancelación se ejecuta en clear pending insert y cancel current .
Abierto:
- clear pending insert
- has pending insert

### Trabajo 2
consulta: qué hace clear pending insert para limpiar el estado de la generación
keep: M8
leído: clear pending insert
Cerrado:
encontré el mecanismo: clear pending insert limpia el estado estableciendo pending insert false, reseteando insert anchor a su valor por defecto y llamando a wake true para notificar el cambio
Abierto:
- handle route

### Trabajo 3
consulta: qué hace cancel current para detener la generación activa
keep: M8
leído: cancel current, cancel all, clear pending insert, busy, cancel level1
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo, o ampliar otro polo de existencia (no el mismo disparo).
encontré el mecanismo: cancel current distingue dos ramas según el estado. Si hay una inserción pendiente y no está busy, limpia el estado de inserción clear pending insert y despierta. Si está busy generación activa, delega en cancel all, que a su vez llama a cancel level1 que setea el flag agent cancel para abortar el agente L1 y cancela las tareas pendientes tasks cancel . Finalmente, despierta el sistema.
Abierto:
- handle user input
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: ensure coding stem index ready, cancel all

T2
    visto: clear pending insert
    extra: cancel current, handle user input

T3
    visto: cancel current, cancel all, clear pending insert, busy
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  mismo objeto: cancel level1
  T1=>T3  cancel all → ai trace escape
  T2=>T3  mismo objeto: clear pending insert
hacia el resto:
  T1 → ensure coding stem index ready
  T1 → Ai Controller
  T3 → Ai Controller
  T3 → Symbol Filter Runner

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
Cerrado de T1:
encontré el objeto de la consulta: la captura de Escape para cancelar la generación ocurre en handle ai console keys líneas truncadas 3050-3060, donde Escape invoca a ai clear pending insert si hay una inserción pendiente; el clic fuera no se captura explícitamente en este handler, pero el foco se gestiona vía Take Focus en eventos custom. El efecto de cancelación se ejecuta en clear pending insert y cancel current .

T2 preguntó:
qué hace clear pending insert para limpiar el estado de la generación
Cerrado de T2:
encontré el mecanismo: clear pending insert limpia el estado estableciendo pending insert false, reseteando insert anchor a su valor por defecto y llamando a wake true para notificar el cambio

T3 preguntó:
qué hace cancel current para detener la generación activa
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo, o ampliar otro polo de existencia (no el mismo disparo).
encontré el mecanismo: cancel current distingue dos ramas según el estado. Si hay una inserción pendiente y no está busy, limpia el estado de inserción clear pending insert y despierta. Si está busy generación activa, delega en cancel all, que a su vez llama a cancel level1 que setea el flag agent cancel para abortar el agente L1 y cancela las tareas pendientes tasks cancel . Finalmente, despierta el sistema.

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo, o ampliar otro polo de existencia (no el mismo disparo).

Legal ahora: plan, cerrar
explorar agotado. Si falta un eslabón, plan (cada fase trae consulta tuya) o cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
