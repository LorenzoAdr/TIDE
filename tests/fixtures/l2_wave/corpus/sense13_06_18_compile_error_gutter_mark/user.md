Consulta del usuario (ancla, claim; no la copies entera a un hijo):
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    error de compilación ~ (sin olor)
    margen editor ~ ui (c=0.52, twin editor)
    consola inferior ~ packet_monitor (c=1.00)
  entre: packet_monitor→ui
  nota: el mapa no une; un explorar cubre un barrio

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=12  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M1  kind=object  ov=6  stems: editor panel
owns: editor panel
gap: state,trigger,effect
peek: editor panel clear editor line paint caches
port: M1=>M4 editor panel clear editor line paint caches -call-> editor text clear
M2  kind=object  ov=6  stems: console expand overlay console expand overlay
owns: console expand overlay
nucleus: console expanded
peek: console expand overlay MakeConsoleExpandOverlay, console expand overlay collapse console expand
peek-edge: console expand overlay collapse console expand -write-> console expanded
M3  kind=other  ov=6  stems: visual highlight
owns: visual highlight
gap: state,trigger,effect
peek: visual highlight build git marks snapshot
M4  kind=caller  ov=6  stems: editor buffer source editor buffer source
owns: editor buffer source
nucleus: valid
peek: editor buffer source editor buffer rebuild joined, editor buffer source rebuild joined
port: M4=>M1 editor panel clear editor line paint caches -call-> editor text clear
M5  kind=other  ov=6  stems: editor context
owns: editor context
gap: state,trigger,effect
peek: editor context build breadcrumbs
M6  kind=other  ov=0  stems: raw pty screen
owns: raw pty screen
gap: state,trigger,effect
peek: raw pty screen build visible rows
M7  kind=caller  ov=0  stems: console panel console panel
owns: console panel
nucleus: terminal has selection, terminal line select drag, terminal selection kind
peek: console panel apply console line drag, console panel begin console drag selection
peek-edge: console panel apply console line drag -write-> row
M8  kind=caller  ov=10  stems: dap backend dap backend
owns: dap backend
nucleus: args
peek: dap backend push error, dap backend launch bashdb
peek-edge: dap backend launch debugpy -write-> args
M9  kind=hole  ov=0  stems: app session raw pty screen shell session app session
owns: app session
gap: no state
peek: shell session rebuild display, raw pty screen styled rows
peek-edge: shell session rebuild display -call-> shell session rebuild display locked
M10  kind=hole  ov=6  stems: embedding backend l2 effect registry ai missing toast open file confirm
owns: embedding backend / visual highlight apply visual highlight fold regions
gap: no state
peek: visual highlight apply visual highlight fold regions, ai controller clear
peek-edge: visual highlight apply visual highlight fold regions -call-> ai controller clear
M11  kind=hole  ov=5  stems: binary symbols panel binary symbols panel
owns: binary symbols panel
gap: no state
peek: binary symbols panel scan shell output for linker errors, binary path
peek-edge: binary symbols panel scan shell output for linker errors -write-> binary path
M12  kind=hole  ov=6  stems: main layout main layout
owns: main layout
gap: no state
peek: main layout apply editor navigation
holes: shell session rebuild display visual highlight apply visual highlight fold regions binary symbols panel scan shell output for linker errors main layout apply editor navigation

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M7 M12
leído: diagnostics on line, diagnostic severity tr, line gutter marker, handle gutter marker click, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el mecanismo: los errores se marcan en el gutter mediante el carácter devuelto por line diagnostic marker from map que consulta diagnostics for editor line el cual es usado por line gutter marker para decidir qué pintar en el margen.
Abierto:
- handle editor mouse
- render gutter

### Trabajo 2
consulta: dónde se procesan los errores de compilación que aparecen en la consola inferior para extraer su línea y mensaje
keep: M8
leído: diagnostics on line, line diagnostic marker from map, handle problems button click, problems tab active console, console panel tab active
Cerrado:
leído: diagnostics on line, line diagnostic marker from map, handle problems button click, problems tab active console, console panel tab active
Abierto:
- handle editor mouse
- handle gutter marker click
- handle editor chrome mouse

### Trabajo 3
consulta: dónde se extraen la línea y el mensaje de los errores de compilación del texto de la consola inferior
keep: M9
leído: diagnostics cpp, symbol provider hpp
Cerrado:
leído: diagnostics cpp, symbol provider hpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics on line, diagnostic severity tr, line gutter marker, handle gutter marker click
    extra: handle editor mouse, Make Editor Panel

T2
    visto: diagnostics on line, line diagnostic marker from map, handle problems button click, problems tab active console
    extra: handle gutter marker click, handle editor mouse

T3
    visto: diagnostics cpp, symbol provider hpp

entre abiertas:
  T1=>T2  mismo objeto: diagnostics on line
  T1=>T2  handle problems button click → make diagnostic modal → diagnostic severity tr
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → handle editor mouse
  T1 → Make Editor Panel
  T2 → handle editor mouse
  T2 → Make Main Layout

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se marcan los errores de compilación en el margen del editor
Cerrado de T1:
encontré el mecanismo: los errores se marcan en el gutter mediante el carácter devuelto por line diagnostic marker from map que consulta diagnostics for editor line el cual es usado por line gutter marker para decidir qué pintar en el margen.

T2 preguntó:
dónde se procesan los errores de compilación que aparecen en la consola inferior para extraer su línea y mensaje
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: diagnostics on line, line diagnostic marker from map, handle problems button click, problems tab active console, console panel tab active

T3 preguntó:
dónde se extraen la línea y el mensaje de los errores de compilación del texto de la consola inferior
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: diagnostics cpp, symbol provider hpp

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
