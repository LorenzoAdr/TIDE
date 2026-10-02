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

Plan de búsqueda (romper):
1. [en_curso] A locator: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
2. [pendiente] B locator: dónde se limpia el archivo a medias al cancelar la generación de la IA
3. [pendiente] P puente: hay camino capturar Escape clic y limpiar el archivo a medias (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré la captura de la tecla Escape ni del clic fuera para cancelar la generación; lo leído es la lógica de cancelación por texto cancel en el controlador de IA y funciones de escape de strings para trazas
Abierto:
- handle ai console keys
- handle key event
- on mouse click

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: cancel current, clear pending insert, cancel all
Cerrado:
leído: cancel current, clear pending insert, cancel all
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: cancel current, clear pending insert
Cerrado:
leído: cancel current, clear pending insert
Abierto:
- handle user input
- handle route

### Trabajo 4
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M8
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: cancel all, Ai Controller

T2
    visto: cancel current, clear pending insert, cancel all
    extra: handle user input, handle route

T3
    visto: cancel current, clear pending insert
    extra: handle user input, handle route

T4
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  mismo objeto: cancel current
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T1=>T4  sin camino
  T2=>T3  mismo objeto: cancel current
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → Ai Controller
  T1 → run insert async
  T2 → Ai Controller
  T2 → Symbol Filter Runner

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré la captura de la tecla Escape ni del clic fuera para cancelar la generación; lo leído es la lógica de cancelación por texto cancel en el controlador de IA y funciones de escape de strings para trazas

T2 preguntó:
dónde se limpia el archivo a medias al cancelar la generación de la IA
Cerrado de T2:
leído: cancel current, clear pending insert, cancel all

T3 preguntó:
dónde se limpia el archivo a medias al cancelar la generación de la IA
Cerrado de T3:
leído: cancel current, clear pending insert

T4 preguntó:
dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
Cerrado de T4:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
(vacío)

Esta pregunta ya tiene respuesta (no hay objeto). No la relances. PROHIBIDO plan u otro explorar con el mismo claim. do=cerrar citando lo leído.

Legal ahora: no_pasar, revisar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
