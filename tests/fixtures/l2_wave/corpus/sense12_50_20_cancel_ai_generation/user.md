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
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de captura de la tecla Escape para cancelar la generación de código; lo leído es una función de escape de strings JSON ai trace escape no un handler de eventos de teclado
Abierto:
- handle ai console keys
- cancel generation

### Trabajo 2
consulta: dónde se captura la tecla Escape en el panel de consola para cancelar la generación
keep: M5
leído: handle ai console keys, clear pending insert, has pending insert, cancel current
Cerrado:
leído: handle ai console keys, clear pending insert, has pending insert, cancel current
Abierto:
- handle route
- handle user input

### Trabajo 3
consulta: dónde se detecta la tecla Escape en el manejo de teclas de la consola para cancelar
keep: M5
leído: handle ai console keys, 3060 3100
Cerrado:
encontré el mecanismo de captura de la tecla Escape para cancelar la generación de código; lo leído es el cuerpo de handle ai console keys en console panel cpp que detecta Escape y llama a ai- clear pending insert cuando hay input activo
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: handle ai console keys, clear pending insert, has pending insert, cancel current
    extra: ai tab active, event is ctrl c

T3
    visto: handle ai console keys, 3060 3100
    extra: clear pending insert, has pending insert

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: handle ai console keys
hacia el resto:
  T2 → ai tab active
  T2 → event is ctrl c
  T3 → ai tab active
  T3 → event is ctrl c

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
Cerrado de T1:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
no encontré el mecanismo de captura de la tecla Escape para cancelar la generación de código; lo leído es una función de escape de strings JSON ai trace escape no un handler de eventos de teclado

T2 preguntó:
dónde se captura la tecla Escape en el panel de consola para cancelar la generación
Cerrado de T2:
leído: handle ai console keys, clear pending insert, has pending insert, cancel current

T3 preguntó:
dónde se detecta la tecla Escape en el manejo de teclas de la consola para cancelar
Cerrado de T3:
encontré el mecanismo de captura de la tecla Escape para cancelar la generación de código; lo leído es el cuerpo de handle ai console keys en console panel cpp que detecta Escape y llama a ai- clear pending insert cuando hay input activo

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
