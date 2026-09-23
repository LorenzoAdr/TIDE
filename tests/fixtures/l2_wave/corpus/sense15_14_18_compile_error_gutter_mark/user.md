Consulta del usuario (ancla, claim; no la copies entera a un hijo):
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    decoración margen ~ (sin olor)
    parseo errores consola ~ (sin olor)
    diagnóstico editor ~ ui (c=0.56, twin editor)
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
1. [fallo] A locator: dónde se parsean los errores de compilación de la consola inferior
2. [fallo] B locator: dónde se dibuja la línea roja en el margen izquierdo del editor
3. [pendiente] P puente: hay camino parsear el error y dibujar la línea roja (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se parsean los errores de compilación de la consola inferior
keep: M9 M11
leído: Make Diagnostics Panel, build rows, supports diagnostics, diagnostics display allowed, diagnostics reveal allowed
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que parsea los errores de compilación desde la consola inferior; lo leído confirma que el panel de diagnósticos consume datos estructurados vía LSP ISymbol Provider no texto plano de la terminal. La consulta asume un parsing de stdout stderr de la consola, pero el sistema usa LSP para obtener diagnósticos.
Abierto:
- parse output
- on data

### Trabajo 2
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor
keep: M1 M6
leído: line gutter marker, gutter buffer line at row, line diagnostic marker from map, diagnostics for editor line
Cerrado:
leído: line gutter marker, gutter buffer line at row, line diagnostic marker from map, diagnostics for editor line
Abierto:
- handle editor mouse


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: Make Diagnostics Panel, build rows, supports diagnostics, diagnostics display allowed
    extra: Make Console Panel, Make Main Layout

T2
    visto: line gutter marker, gutter buffer line at row, line diagnostic marker from map, diagnostics for editor line
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → Make Console Panel
  T1 → Make Main Layout
  T2 → Make Editor Panel
  T2 → Make Main Layout

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se parsean los errores de compilación de la consola inferior
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que parsea los errores de compilación desde la consola inferior; lo leído confirma que el panel de diagnósticos consume datos estructurados vía LSP ISymbol Provider no texto plano de la terminal. La consulta asume un parsing de stdout stderr de la consola, pero el sistema usa LSP para obtener diagnósticos.

T2 preguntó:
dónde se dibuja la línea roja en el margen izquierdo del editor
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: line gutter marker, gutter buffer line at row, line diagnostic marker from map, diagnostics for editor line

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: explorar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Lee Cerrado. Cierra si el claim ya se confirma o se refuta, o suelta el plan y sigue un port.
JSON ahora. Primer carácter `{`.
