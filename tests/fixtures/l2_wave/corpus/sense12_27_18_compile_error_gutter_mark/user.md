Consulta del usuario (ancla, claim; no la copies entera a un hijo):
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

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
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
keep: M1
leído: clear editor line paint caches, editor panel cpp, line gutter marker
Cerrado:
leído: clear editor line paint caches, editor panel cpp, line gutter marker
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola para marcarlos en el editor
keep: M1 M11
leído: diagnostics for translation unit, diagnostics on line, diagnostic severity tr
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, amplia un hop; no refutes el claim entero.
no encontré el mecanismo que lee errores de compilación de la consola para marcarlos en el editor; lo leído confirma que el editor usa diagnostics de LSP diagnostics cpp y que el panel de símbolos binarios solo escanea errores de linker de la shell scan shell output for linker errors, pero no existe un puente que convierta la salida de compilación gcc clang en diagnostics para el editor.
Abierto:
- handle editor mouse
- compiler output to diagnostics
- parse compiler errors
- shell to editor diagnostics


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: clear editor line paint caches, editor panel cpp, line gutter marker
    extra: Make Editor Panel, Make Main Layout

T2
    visto: diagnostics for translation unit, diagnostics on line, diagnostic severity tr
    extra: Make Main Layout, build include tree

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → Make Main Layout
  T2 → build include tree

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
Cerrado de T1:
leído: clear editor line paint caches, editor panel cpp, line gutter marker

T2 preguntó:
dónde se leen los errores de compilación de la consola para marcarlos en el editor
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, amplia un hop; no refutes el claim entero.
no encontré el mecanismo que lee errores de compilación de la consola para marcarlos en el editor; lo leído confirma que el editor usa diagnostics de LSP diagnostics cpp y que el panel de símbolos binarios solo escanea errores de linker de la shell scan shell output for linker errors, pero no existe un puente que convierta la salida de compilación gcc clang en diagnostics para el editor.

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, amplia un hop; no refutes el claim entero.

Legal ahora: explorar, plan, cerrar
Este es el estado. Tú diriges: atomiza el trabajo (un hijo, un trozo). El plan no es un contrato: tras un Cerrado puedes soltarlo (explorar o plan nuevo). Esta pregunta ya tiene respuesta (no hay objeto). No la relances. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero. 'mismo objeto' en entre abiertas es que la caza no se movió. Si queda un tirón, explorar; si el trabajo no cabe en uno, plan. consulta = prompt de investigación (fenómeno y verbo; el hijo no ve el ancla); why = tu estrategia (el hijo no la lee). Lo que no se caza no se nombra. hacia opcional: conceptos de cerca, no ids. evita opcional. Un explorar es una sola rama; atomiza, no empaquetes dos cazas en un hijo. tú eliges cómo seguir. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
