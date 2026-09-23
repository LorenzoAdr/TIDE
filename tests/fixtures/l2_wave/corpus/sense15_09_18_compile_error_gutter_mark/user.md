Consulta del usuario (ancla, claim; no la copies entera a un hijo):
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    error consola ~ (sin olor)
    línea margen ~ (sin olor)
    marcar editor ~ ui (c=0.52, twin editor)
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

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1 M12
leído: push active file diagnostics from cache, line gutter marker, apply lsp diagnostics to panel, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el mecanismo: line gutter marker 1005 decide el carácter del margen para error, W para warning consultando line diagnostic marker from map, que a su vez usa diagnostics for editor line para leer el mapa panel- diagnostics by line Este mapa se llena en push active file diagnostics from cache 1333 a partir de la caché de diagnostics del LSP.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se extraen los errores de compilación del texto de la consola inferior
keep: M9 M12
leído: Make Diagnostics Panel, build rows, diagnostics for translation unit, Make Console Panel
Cerrado:
leído: Make Diagnostics Panel, build rows, diagnostics for translation unit, Make Console Panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se parsean los errores de compilación del texto de la consola para generar diagnostics
keep: M12 M2
leído: parse diagnostic
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el parsing de errores de compilación desde texto de consola; lo leído es el parsing de JSON estructurado de LSP parse diagnostic, que no procesa texto plano de stdout stderr
Abierto:
- compiler error parser
- parse compile errors


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: push active file diagnostics from cache, line gutter marker, apply lsp diagnostics to panel, line diagnostic marker from map
    extra: ISymbol Provider, invalidate editor view

T2
    visto: Make Diagnostics Panel, build rows, diagnostics for translation unit, Make Console Panel
    extra: Make Main Layout, clamp scroll viewport

T3
    visto: parse diagnostic
    extra: on lsp notification, parse publish diagnostics

entre abiertas:
  T1=>T2  extra toca visto: build rows
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → ISymbol Provider
  T1 → invalidate editor view
  T2 → Make Main Layout
  T2 → clamp scroll viewport
  T3 → on lsp notification
  T3 → parse publish diagnostics

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se marcan los errores de compilación en el margen del editor
Cerrado de T1:
encontré el mecanismo: line gutter marker 1005 decide el carácter del margen para error, W para warning consultando line diagnostic marker from map, que a su vez usa diagnostics for editor line para leer el mapa panel- diagnostics by line Este mapa se llena en push active file diagnostics from cache 1333 a partir de la caché de diagnostics del LSP.

T2 preguntó:
dónde se extraen los errores de compilación del texto de la consola inferior
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: Make Diagnostics Panel, build rows, diagnostics for translation unit, Make Console Panel

T3 preguntó:
dónde se parsean los errores de compilación del texto de la consola para generar diagnostics
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el parsing de errores de compilación desde texto de consola; lo leído es el parsing de JSON estructurado de LSP parse diagnostic, que no procesa texto plano de stdout stderr

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
exploradores agotados. Lo leído no era el disparo. Cierra ese mapeo; un leído: de otra caza no afirma el ancla; no lo parafrasees como el mecanismo. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
