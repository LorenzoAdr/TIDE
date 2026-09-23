Consulta del usuario (ancla, claim; no la copies entera a un hijo):
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    línea margen ~ (sin olor)
    error consola ~ (sin olor)
    decoración editor ~ ui (c=0.56, twin editor)
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
keep: M1 M12
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line, line gutter marker, handle gutter marker click, line diagnostic marker from map
Cerrado:
encontré el mecanismo de decisión: la línea roja marcador de error se resuelve como el carácter en line diagnostic marker from map 978 993, que es llamado por line gutter marker 1005 1017 . El dibujo real delega a la infraestructura de UI no leída que consume este carácter para renderizar el icono en el gutter.
Abierto:
- render gutter icon
- draw char in gutter

### Trabajo 2
consulta: dónde se analizan los errores de compilación en la salida de la consola para extraer la línea y el mensaje
keep: M4 M11
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line
Cerrado:
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se leen los errores de compilación de la consola para crear marcadores en el editor
keep: M1 M3
leído: line diagnostic marker from map, line diagnostic marker, diagnostics for editor line
Cerrado:
leído: line diagnostic marker from map, line diagnostic marker, diagnostics for editor line
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: rebuild diagnostics by line if changed, rebuild diagnostics by line, line gutter marker, handle gutter marker click
    extra: apply lsp diagnostics to panel, push active file diagnostics from cache

T2
    visto: rebuild diagnostics by line if changed, rebuild diagnostics by line
    extra: apply lsp diagnostics to panel, push active file diagnostics from cache

T3
    visto: line diagnostic marker from map, line diagnostic marker, diagnostics for editor line
    extra: Make Editor Panel, line gutter marker

entre abiertas:
  T1=>T2  mismo objeto: rebuild diagnostics by line if changed
  T1=>T3  mismo objeto: line diagnostic marker from map
  T1=>T3  line gutter marker → line diagnostic marker from map → diagnostics for editor line
  T2=>T3  sin camino
hacia el resto:
  T1 → apply lsp diagnostics to panel
  T1 → push active file diagnostics from cache
  T2 → apply lsp diagnostics to panel
  T2 → push active file diagnostics from cache
  T3 → Make Editor Panel
  T3 → Make Main Layout

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se dibuja la línea roja en el margen del editor para marcar un error
Cerrado de T1:
encontré el mecanismo de decisión: la línea roja marcador de error se resuelve como el carácter en line diagnostic marker from map 978 993, que es llamado por line gutter marker 1005 1017 . El dibujo real delega a la infraestructura de UI no leída que consume este carácter para renderizar el icono en el gutter.

T2 preguntó:
dónde se analizan los errores de compilación en la salida de la consola para extraer la línea y el mensaje
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line

T3 preguntó:
dónde se leen los errores de compilación de la consola para crear marcadores en el editor
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: line diagnostic marker from map, line diagnostic marker, diagnostics for editor line

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
exploradores agotados. Lo leído no era el disparo. Cierra ese mapeo; no cites esos nombres como el mecanismo del ancla. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
