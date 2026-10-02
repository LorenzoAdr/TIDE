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
M10  kind=hole  ov=6  stems: embedding backend

Fichas ampliadas (pack+inspect. El inspect elige barrio; no copies símbolos; el fenómeno del ancla sí):
n=2  (owns+nucleus+peek+circuito; sin inspect gordo)
M11  kind=hole  ov=5
owns: binary symbols panel
peek: binary symbols panel scan shell output for linker errors, binary path
M7  kind=caller  ov=0
owns: console panel
nucleus: terminal has selection, terminal line select drag, terminal selection kind
peek: console panel apply console line drag, console panel begin console drag selection
entre abiertas:
(ningún port)
## M7
kind=caller
owns: console panel
nuclei: C2(terminal has selection) C3(terminal line select drag) C4(terminal selection kind) C6(row)
anchors:
- G1: console panel console word bounds at[q7]
- G2: console panel apply console line drag[q6]
closure: 2/2 groups, hub-free=2
roles:
- writers: console panel clear terminal selection, console panel begin console drag selection, console panel select console word at, console panel select console line at
- readers: console panel terminal selected text, console panel apply console line drag, console panel update console drag head, console panel console selection line
- controls: console panel L707:guard, console panel L754:guard, console panel L616:guard, console panel L644:if
mechanism:
- trigger: console panel L644:if -then-> console panel apply console line drag
- state: console panel apply console line drag -write(row)-> row
- effect: console panel apply console line drag -call-> console panel console selection line
causal edges:
- console panel apply console line drag -write(row)-> row [state]
- console panel L644:if -then-> console panel apply console line drag [trigger]
- console panel apply console line drag -call-> console panel console selection line [effect]
- console panel apply console line drag -write(terminal has selection)-> terminal has selection [support]
mini-cards:
- console panel console word bounds at | void console word bounds at(const std string& line, int col, int* start col, int* end col) | roles=mutator | writes=end col,start col,col | reads=col,end,len | hot=early return,multi return
· 444: const int len = static cast<int>(line.size());
· 446: *start col = 0;
· 447: *end col = 0;
· 450: col = std max(0, std min(col, len - 1));
- console panel apply console line drag | void apply console line drag(ConsolePanelState* state, const DebugModel* model, int row) | roles=mutator | writes=state.terminal sel head col,state.terminal sel head row,row | reads=b,a,end line | hot=early return,multi return
· 619: const int total = console selection line count(state, model);
· 623: row = std max(0, std min(row, total - 1));
· 624: const int a = std min(state->terminal line select anchor, row);
· 625: const int b = std max(state->terminal line select anchor, row);
- console panel apply console multi click | ConsoleMultiClick apply console multi click(ConsolePanelState* state, const DebugModel* model, | roles=query | hot=early return,multi return
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
1. [paso] A locator: dónde se parsean los errores de compilación del texto de la consola inferior
2. [en_curso] B locator: dónde se dibuja una línea roja en el margen izquierdo del editor
3. [pendiente] P puente: hay camino parsear los errores de la consola y dibujar la línea roja en el editor (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se parsean los errores de compilación del texto de la consola inferior
keep: M7
leído: scan shell output for linker errors
Cerrado:
leído: scan shell output for linker errors
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dibuja una línea roja en el margen izquierdo del editor
keep: M1
leído: clear editor line paint caches
Cerrado:
leído: clear editor line paint caches
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, parse linker undefined reference

T2
    visto: clear editor line paint caches

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → tick binary symbols panel
  T1 → parse linker undefined reference

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se parsean los errores de compilación del texto de la consola inferior
Cerrado de T1:
leído: scan shell output for linker errors

T2 preguntó:
dónde se dibuja una línea roja en el margen izquierdo del editor
Cerrado de T2:
leído: clear editor line paint caches

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: pasar, no_pasar, revisar, explorar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
