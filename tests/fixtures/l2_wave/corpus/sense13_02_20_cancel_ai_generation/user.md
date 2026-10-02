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
1. [paso] A locator: dónde se captura la tecla Escape para cancelar la generación de código (hacia key handler)
2. [en_curso] B locator: dónde se limpia el archivo a medias al cancelar la generación (hacia clear pending insert)
3. [pendiente] P puente: hay camino capturar Escape y limpiar el archivo a medias (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se cancela la generación de código y se limpia el archivo al pulsar Escape
keep: M2 M1
leído: cancel current, clear pending insert, handle user input, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación al pulsar Escape; lo leído muestra que el sistema solo reconoce los comandos de texto cancel, cancel o cancelar is cancel input, pero no hay evidencia de que la tecla Escape código de escape dispare esta lógica en handle user input
Abierto:
- handle route
- key handler
- on key press
- escape key

### Trabajo 2
consulta: dónde se captura la tecla Escape para cancelar la generación de código
keep: M6 M9
leído: handle ai console keys, cancel level1, 3020 3165, 3020 3139
Cerrado:
leído: handle ai console keys, cancel level1, 3020 3165, 3020 3139
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se limpia el archivo a medias al cancelar la generación
keep: M6
leído: clear pending insert, cancel current, cancel all, cancel level1
Cerrado:
leído: clear pending insert, cancel current, cancel all, cancel level1
Abierto:
- handle route
- handle user input


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel current, clear pending insert, handle user input, is cancel input
    extra: handle route, cancel all

T2
    visto: handle ai console keys, cancel level1, 3020 3165, 3020 3139
    extra: clear pending insert, has pending insert

T3
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current
  T1=>T3  mismo objeto: cancel current
  T1=>T3  handle user input → handle route → cancel current → cancel all
  T2=>T3  mismo objeto: cancel level1
  T2=>T3  handle ai console keys → clear pending insert
hacia el resto:
  T1 → handle route
  T1 → run insert async
  T2 → has pending insert
  T2 → ai tab active
  T3 → handle route
  T3 → Ai Controller

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se cancela la generación de código y se limpia el archivo al pulsar Escape
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación al pulsar Escape; lo leído muestra que el sistema solo reconoce los comandos de texto cancel, cancel o cancelar is cancel input, pero no hay evidencia de que la tecla Escape código de escape dispare esta lógica en handle user input

T2 preguntó:
dónde se captura la tecla Escape para cancelar la generación de código
Cerrado de T2:
leído: handle ai console keys, cancel level1, 3020 3165, 3020 3139

T3 preguntó:
dónde se limpia el archivo a medias al cancelar la generación
Cerrado de T3:
leído: clear pending insert, cancel current, cancel all, cancel level1

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. Un Cerrado que afirma SU consulta no sella el ancla. Si entre abiertas dice sin camino, no pegues esos Cerrados como un solo mecanismo.

Legal ahora: cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

El último Cerrado negó el mapeo. Cierra ESE mapeo (hueco o sin camino), no selles un polo que sí se leyó.
