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
1. [en_curso] A locator: dónde se invoca la restauración del archivo al cancelar la generación de la IA
2. [pendiente] B locator: dónde se captura el clic fuera para cancelar la generación de la IA
3. [pendiente] P puente: hay camino la captura de Escape y la restauración del archivo (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2
leído: handle ai console keys, cancel current, clear pending insert
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
encontré el objeto de la consulta: la pulsación de Escape se captura en handle ai console keys línea 3040 del peek, bloque if tras la validación de selección y antes de ai console keys active . El efecto de cancelación se ejecuta vía cancel current llamado desde handle user input o handle route que invoca clear pending insert para limpiar el estado pendiente. No se encontró captura de clic fuera en los loci leídos; el mecanismo de cancelación por UI parece estar acoplado a la tecla Escape en el panel de consola.
Abierto:
- handle user input
- handle route

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: clear pending insert, cancel current, cancel all, wake, cancel level1, begin insert at
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré dónde se limpia el archivo a medias; lo leído muestra que cancel current solo limpia estado interno pending insert, agent cancel y despierta la UI, sin truncar ni restaurar el buffer del editor
Abierto:
- handle route
- handle user input
- truncate buffer
- restore snapshot
- apply insert

### Trabajo 3
consulta: dónde se restaura el contenido del archivo a su estado anterior al iniciar la generación de la IA
keep: M7
leído: snapshot lines, rollback pending
Cerrado:
encontré el objeto de la consulta: la restauración del contenido del archivo a su estado anterior ocurre en rollback pending línea 8119 de level2 session que itera sobre st pending y escribe p before en p abs path mediante write text file .
Abierto:
- load state
- save state

### Trabajo 4
consulta: dónde se invoca la restauración del archivo al cancelar la generación de la IA
keep: M8
leído: clear pending insert, rollback pending, cancel current, cancel level1, cancel all
Cerrado:
leído: clear pending insert, rollback pending, cancel current, cancel level1, cancel all
Abierto:
- handle route
- handle user input


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, cancel current, clear pending insert
    extra: has pending insert, ai tab active

T2
    visto: clear pending insert, cancel current, cancel all, wake
    extra: handle user input, handle route

T3
    visto: snapshot lines, rollback pending
    extra: write text file, load state

T4
    visto: clear pending insert, rollback pending, cancel current, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current → cancel all
  T1=>T3  handle ai console keys → snapshot lines
  T1=>T4  mismo objeto: cancel current
  T1=>T4  handle ai console keys → handle user input → handle route → cancel current → cancel all → cancel level1
  T2=>T3  sin camino
  T2=>T4  mismo objeto: clear pending insert
  T2=>T4  rollback pending → pack span hint
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
  T2 → handle user input
  T2 → handle route
  T3 → write text file
  T3 → load state

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
encontré el objeto de la consulta: la pulsación de Escape se captura en handle ai console keys línea 3040 del peek, bloque if tras la validación de selección y antes de ai console keys active . El efecto de cancelación se ejecuta vía cancel current llamado desde handle user input o handle route que invoca clear pending insert para limpiar el estado pendiente. No se encontró captura de clic fuera en los loci leídos; el mecanismo de cancelación por UI parece estar acoplado a la tecla Escape en el panel de consola.

T2 preguntó:
dónde se limpia el archivo a medias al cancelar la generación de la IA
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré dónde se limpia el archivo a medias; lo leído muestra que cancel current solo limpia estado interno pending insert, agent cancel y despierta la UI, sin truncar ni restaurar el buffer del editor

T3 preguntó:
dónde se restaura el contenido del archivo a su estado anterior al iniciar la generación de la IA
Cerrado de T3:
encontré el objeto de la consulta: la restauración del contenido del archivo a su estado anterior ocurre en rollback pending línea 8119 de level2 session que itera sobre st pending y escribe p before en p abs path mediante write text file .

T4 preguntó:
dónde se invoca la restauración del archivo al cancelar la generación de la IA
Cerrado de T4:
leído: clear pending insert, rollback pending, cancel current, cancel level1, cancel all

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: pasar, no_pasar, revisar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

explorar agotado. Si falta un eslabón, plan (cada fase trae consulta tuya) o cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
