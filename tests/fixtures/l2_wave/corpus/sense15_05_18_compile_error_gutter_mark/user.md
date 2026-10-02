Consulta del usuario (ancla, claim; no la copies entera a un hijo):
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    decoración margen ~ (sin olor)
    diagnóstico editor ~ ui (c=0.52, twin editor)
    línea roja error ~ (sin olor)
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
consulta: dónde se dibuja la línea roja en el margen del editor para marcar un error
keep: M1 M4
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de dibujo visual de la línea roja en el margen; lo leído son funciones de reconstrucción de datos rebuild diagnostics by line que mapean diagnósticos a líneas, pero no contienen código de renderizado UI draw . Las búsquedas de símbolos visuales draw diagnostic gutter draw etc. dieron 0 hits.
Abierto:
- draw diagnostics
- render gutter
- paint margin

### Trabajo 2
consulta: dónde se aplican los diagnósticos de compilación al panel del editor para mostrar la línea roja
keep: M1 M5
leído: apply lsp diagnostics to panel, diagnostics for editor line
Cerrado:
leído: apply lsp diagnostics to panel, diagnostics for editor line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se convierten los errores de la consola en diagnósticos para el editor
keep: M5 M6
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se convierten los errores de la consola en diagnósticos para el editor
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: rebuild diagnostics by line if changed, rebuild diagnostics by line
    extra: apply lsp diagnostics to panel, push active file diagnostics from cache

T2
    visto: apply lsp diagnostics to panel, diagnostics for editor line
    extra: ISymbol Provider, allows lsp ui

T3
    visto: (nada)

entre abiertas:
  T1=>T2  apply lsp diagnostics to panel → push active file diagnostics from cache → rebuild diagnostics by line if changed
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → push active file diagnostics from cache
  T1 → ISymbol Provider
  T2 → ISymbol Provider
  T2 → allows lsp ui

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se dibuja la línea roja en el margen del editor para marcar un error
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de dibujo visual de la línea roja en el margen; lo leído son funciones de reconstrucción de datos rebuild diagnostics by line que mapean diagnósticos a líneas, pero no contienen código de renderizado UI draw . Las búsquedas de símbolos visuales draw diagnostic gutter draw etc. dieron 0 hits.

T2 preguntó:
dónde se aplican los diagnósticos de compilación al panel del editor para mostrar la línea roja
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: apply lsp diagnostics to panel, diagnostics for editor line

T3 preguntó:
dónde se convierten los errores de la consola en diagnósticos para el editor
Cerrado de T3:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
(vacío)

Esta pregunta ya tiene respuesta (no hay objeto). No la relances ni en otro keep. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
Un leído: de otra caza no afirma el ancla; no lo parafrasees como el mecanismo.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
