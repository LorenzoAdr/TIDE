Consulta del usuario (ancla, claim; no la copies entera a un hijo):
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    visual highlight ~ ui (c=0.50, twin editor)
    binary symbols panel ~ ui (c=0.79)
    editor decoration ~ ui (c=0.46, twin editor)
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
1. [en_curso] A locator: dónde se parsean los errores de compilación de la salida de la consola
2. [pendiente] B locator: dónde se aplican los diagnósticos de errores al margen del editor
3. [pendiente] P puente: hay camino parsear errores de consola y aplicar diagnósticos al editor (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M8 M2
leído: push active file diagnostics from cache, apply lsp diagnostics to panel
Cerrado:
leído: push active file diagnostics from cache, apply lsp diagnostics to panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la salida de la consola
keep: M3 M7
leído: console panel cpp, main layout cpp, parse pty filter tokens, ascii lower
Cerrado:
leído: console panel cpp, main layout cpp, parse pty filter tokens, ascii lower
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se parsean los errores de compilación de la salida de la consola
keep: M8 M11
leído: parse pty filter tokens, problem frame wants anchor hypotheses
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que parsea errores de compilación de la salida de la consola; lo leído son utilidades de filtrado de tokens de la terminal parse pty filter tokens y lógica de confianza de anclas problem frame wants anchor hypotheses, que no procesan diagnósticos del compilador
Abierto:
- parse compiler output
- diagnostic parser
- build log parser


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: push active file diagnostics from cache, apply lsp diagnostics to panel
    extra: ISymbol Provider, invalidate editor view

T2
    visto: console panel cpp, main layout cpp, parse pty filter tokens, ascii lower
    extra: build console panel view, render app terminal

T3
    visto: parse pty filter tokens, problem frame wants anchor hypotheses
    extra: build console panel view, render app terminal

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: parse pty filter tokens
  T2=>T3  problem frame wants anchor hypotheses → ascii lower
hacia el resto:
  T1 → ISymbol Provider
  T1 → invalidate editor view
  T2 → build console panel view
  T2 → render app terminal
  T3 → build console panel view
  T3 → render app terminal

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se marcan los errores de compilación en el margen del editor
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: push active file diagnostics from cache, apply lsp diagnostics to panel

T2 preguntó:
dónde se leen los errores de compilación de la salida de la consola
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: console panel cpp, main layout cpp, parse pty filter tokens, ascii lower

T3 preguntó:
dónde se parsean los errores de compilación de la salida de la consola
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que parsea errores de compilación de la salida de la consola; lo leído son utilidades de filtrado de tokens de la terminal parse pty filter tokens y lógica de confianza de anclas problem frame wants anchor hypotheses, que no procesan diagnósticos del compilador

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase. Lo leído no era el disparo. No pases como si afirmara. no_pasar o cierra ese mapeo. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
