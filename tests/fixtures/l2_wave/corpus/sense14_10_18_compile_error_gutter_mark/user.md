Consulta del usuario (ancla, claim; no la copies entera a un hijo):
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    error de compilación ~ (sin olor)
    línea roja margen ~ (sin olor)
    consola inferior ~ packet_monitor (c=1.00)
  entre: (sin arista de barrio)
  nota: un barrio concentra estos olores

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

Plan de búsqueda (romper):
1. [fallo] A locator: dónde se pinta la línea roja en el margen izquierdo del editor para marcar un error
2. [en_curso] B locator: dónde se pinta la línea roja en el margen izquierdo del editor para marcar un error
3. [pendiente] P puente: hay camino leer los errores de la consola y pintar la línea roja en el margen (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor para marcar un error
keep: M1 M2
leído: line gutter marker, handle gutter marker click, Event
Cerrado:
leído: line gutter marker, handle gutter marker click, Event
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M1 M12
leído: make terminal panel
Cerrado:
leído: make terminal panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se pinta la línea roja en el margen izquierdo del editor para marcar un error
keep: M1 M2
leído: line gutter marker, handle gutter marker click, line diagnostic marker from map, git line changed, diagnostics for editor line
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de decisión del marcador la línea roja es el carácter devuelto por line gutter marker, pero no encontré la función que ejecuta la pintura gráfica en el margen el efecto visual . Lo leído son las funciones de consulta de datos y decisión de carácter.
Abierto:
- handle editor mouse
- paint gutter
- draw gutter
- render gutter


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, handle gutter marker click, Event
    extra: Make Editor Panel, Make Main Layout

T2
    visto: make terminal panel
    extra: Code Bg, Panel Title

T3
    visto: line gutter marker, handle gutter marker click, line diagnostic marker from map, git line changed
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: line gutter marker
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → Code Bg
  T2 → Panel Title
  T3 → Make Editor Panel
  T3 → Make Main Layout

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se pinta la línea roja en el margen izquierdo del editor para marcar un error
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: line gutter marker, handle gutter marker click, Event

T2 preguntó:
dónde se leen los errores de compilación de la consola inferior
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: make terminal panel

T3 preguntó:
dónde se pinta la línea roja en el margen izquierdo del editor para marcar un error
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de decisión del marcador la línea roja es el carácter devuelto por line gutter marker, pero no encontré la función que ejecuta la pintura gráfica en el margen el efecto visual . Lo leído son las funciones de consulta de datos y decisión de carácter.

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase. Lo leído no era el disparo. No pases como si afirmara. no_pasar o cierra ese mapeo. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
