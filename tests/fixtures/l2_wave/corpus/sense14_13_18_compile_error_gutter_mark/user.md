Consulta del usuario (ancla, claim; no la copies entera a un hijo):
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    error consola ~ (sin olor)
    margen editor ~ ui (c=0.52, twin editor)
    línea roja ~ (sin olor)
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
1. [en_curso] A locator: dónde se crea el marcador de línea en el margen del editor para un error de diagnóstico
2. [pendiente] B locator: dónde se asocia un diagnóstico de compilación con una línea del editor
3. [pendiente] P puente: hay camino el diagnóstico de compilación y el marcador de línea en el margen (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se detectan los errores de compilación en la salida de la consola
keep: M3 M12
leído: parse pty filter tokens, diagnostics for translation unit, diagnostics on line
Cerrado:
leído: parse pty filter tokens, diagnostics for translation unit, diagnostics on line
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M1 M2
leído: line gutter marker
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la pintura de la línea roja; lo leído es un selector de carácter ASCII G, W para el gutter, no la lógica de renderizado de color
Abierto:
- paint gutter
- draw line marker
- gutter color

### Trabajo 3
consulta: dónde se crea el marcador de línea en el margen del editor para un error de diagnóstico
keep: M1 M2
leído: line diagnostic marker, line gutter marker, Make Editor Panel
Cerrado:
leído: line diagnostic marker, line gutter marker, Make Editor Panel
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: parse pty filter tokens, diagnostics for translation unit, diagnostics on line
    extra: build console panel view, render app terminal

T2
    visto: line gutter marker
    extra: Make Editor Panel, Make Main Layout

T3
    visto: line diagnostic marker, line gutter marker, Make Editor Panel
    extra: Make Main Layout, git line changed

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  Make Editor Panel → sync diagnostic cache → diagnostics for translation unit
  T2=>T3  mismo objeto: line gutter marker
hacia el resto:
  T1 → build console panel view
  T1 → render app terminal
  T2 → Make Main Layout
  T2 → git line changed
  T3 → Make Main Layout
  T3 → git line changed

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se detectan los errores de compilación en la salida de la consola
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: parse pty filter tokens, diagnostics for translation unit, diagnostics on line

T2 preguntó:
dónde se pinta la línea roja en el margen izquierdo del editor
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la pintura de la línea roja; lo leído es un selector de carácter ASCII G, W para el gutter, no la lógica de renderizado de color

T3 preguntó:
dónde se crea el marcador de línea en el margen del editor para un error de diagnóstico
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: line diagnostic marker, line gutter marker, Make Editor Panel

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase. Lo leído no era el disparo. No pases como si afirmara. no_pasar o cierra ese mapeo. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
