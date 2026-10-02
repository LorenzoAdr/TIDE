Consulta del usuario (ancla, claim; no la copies entera a un hijo):
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=10  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
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
peek: shell session rebuild

Fichas ampliadas (pack+inspect. El inspect elige barrio; no copies símbolos; el fenómeno del ancla sí):
n=2  (owns+nucleus+peek+circuito; sin inspect gordo)
M3  kind=other  ov=6
owns: visual highlight
peek: visual highlight build git marks snapshot
M11  kind=hole  ov=5
owns: binary symbols panel
peek: binary symbols panel scan shell output for linker errors, binary path
entre abiertas:
(ningún port)
## M3
kind=other
owns: visual highlight
nuclei:
anchors:
- G1: visual highlight build git marks snapshot[q2]
closure: 0/1 groups, hub-free=0
roles:
skeleton missing: state trigger effect
causal edges:
mini-cards:
- visual highlight build git marks snapshot | void build git marks snapshot(const VisualHighlightJob& job, VisualHighlightSnapshot* snap) | roles=mutator | writes=snap.overview.git changed lines,snap.overview.git head line count,snap.overview.git marks computed | reads=inputs,job,changed new lines | hot=early return,multi return
· 301: snap->overview.git changed lines.clear();
· 302: snap->overview.git previous by line.clear();
· 304: static cast<int>(job.inputs.git head lines.size());
· 312: const LineDiffResult diff = compute line diff(job.inputs.git head lines, job.lines);
risks: no state nucleus
## M11
kind=hole
owns: binary symbols panel
nuclei:
anchors:
- G1: binary symbols panel scan shell output for linker errors[q9]
closure: 0/1 groups, hub-free=0
roles:
- writers: binary symbols panel scan shell output for linker errors, binary symbols panel start analysis, binary symbols panel request binary symbols panel, binary symbols panel refresh binary symbols if matches
causal edges:
- binary symbols panel scan shell output for linker errors -write(binary path)-> binary path
- binary symbols panel tick binary symbols panel -call()-> binary symbols panel scan shell output for linker errors
- binary symbols panel start analysis -write(binary path)-> binary path
- binary symbols panel request binary symbols panel -write(binary path)-> binary path
mini-cards:
- binary symbols panel scan shell output for linker errors | void scan shell output for linker errors(const std string& output, MainLayoutState* layout state, | roles=mutator | writes=binary path | reads=model,program
· 783: if (layout state == nullptr || output.empty()) {
· 786: std istringstream stream(output);
· 789: const auto symbol = parse linker undefined reference(line);
· 795: binary path = model->program;
- binary path
- binary symbols panel tick binary symbols panel | void tick binary symbols panel(BinarySymbolsPanelState* state, MainLayoutState* layout state, | roles=mutator,ui | writes=state.last custom tick processed,state.last shell output size,state.pending shell scan | reads=layout state,load,memory order relaxed | hot=early return,multi return,ui event
risks: promoted from uncovered no state nucleus uncovered candidate
## uncovered seeds
- q5 shell session rebuild display
- q8 visual highlight apply visual highlight fold regions
- q9 binary symbols panel scan shell output for linker errors
- q13 main layout apply editor navigation
- q14 main layout MakeMainLayout

Plan de búsqueda (romper):
1. [paso] A locator: dónde se marcan los errores de compilación en el margen del editor
2. [en_curso] B locator: dónde se leen los errores de compilación de la consola inferior
3. [pendiente] P puente: hay camino leer los errores de la consola y marcarlos en el margen (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1
leído: diagnostics for editor line, diagnostics on line, line gutter marker, gutter buffer line at row, line diagnostic marker from map
Cerrado:
leído: diagnostics for editor line, diagnostics on line, line gutter marker, gutter buffer line at row, line diagnostic marker from map
Abierto:
- handle editor mouse
- handle gutter marker click

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M1
leído: diagnostics for editor line, diagnostics on line, line diagnostic marker from map
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré la lectura de errores de compilación de la consola inferior; lo leído es el renderizado de diagnostics LSP en el gutter del editor
Abierto:
- handle editor mouse
- handle gutter marker click
- parse compilation errors
- on output


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, diagnostics on line, line gutter marker, gutter buffer line at row
    extra: Make Editor Panel, Make Main Layout

T2
    visto: diagnostics for editor line, diagnostics on line, line diagnostic marker from map
    extra: Make Main Layout, Make Editor Panel

entre abiertas:
  T1=>T2  mismo objeto: diagnostics for editor line
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → Make Main Layout
  T2 → Make Editor Panel

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se marcan los errores de compilación en el margen del editor
Cerrado de T1:
leído: diagnostics for editor line, diagnostics on line, line gutter marker, gutter buffer line at row, line diagnostic marker from map

T2 preguntó:
dónde se leen los errores de compilación de la consola inferior
Cerrado de T2:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré la lectura de errores de compilación de la consola inferior; lo leído es el renderizado de diagnostics LSP en el gutter del editor

ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.

Legal ahora: pasar, no_pasar, revisar, explorar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). Esta pregunta ya tiene respuesta (no hay objeto). No la relances. PROHIBIDO plan u otro explorar con el mismo claim. do=cerrar citando lo leído. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
