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
M10  kind=hole  ov=6
owns: embedding backend / visual highlight apply visual highlight fold regions
peek: visual highlight apply visual highlight fold regions, ai controller clear
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
## M10
kind=hole
owns: embedding backend / visual highlight apply visual highlight fold regions
nuclei:
anchors:
- G1: visual highlight apply visual highlight fold regions[q8]
closure: 0/1 groups, hub-free=0
roles:
- writers: visual highlight apply visual highlight fold regions, visual highlight compute, visual highlight drain visual highlight results, visual highlight tick visual highlight scheduler
causal edges:
- visual highlight apply visual highlight fold regions -call()-> ai controller clear
- visual highlight apply visual highlight fold regions -call()-> visual highlight prune invalid collapsed folds
- visual highlight apply visual highlight fold regions -write(changed)-> changed
- visual highlight apply visual highlight fold regions -write(fold regions)-> fold regions
mini-cards:
- visual highlight apply visual highlight fold regions | bool apply visual highlight fold regions(EditorBuffer* buffer, VisualHighlightPanelState* state, | roles=mutator | writes=state.last applied fold revision,buffer.fold regions,changed | reads=snap,fold regions revision | hot=early return,multi return
· 719: const bool changed = !buffer->fold regions.empty() || !buffer->collapsed folds.empty();
· 720: buffer->fold regions.clear();
· 721: buffer->collapsed folds.clear();
· 731: const uint64 t current rev = tree sitter service().revision for(buffer->path);
- ai controller clear | void AiController clear() | roles=lock | hot=lock
- visual highlight prune invalid collapsed folds | void prune invalid collapsed folds(EditorBuffer* buffer) | roles=query | reads=buffer,collapsed folds,erase
risks: promoted from uncovered no state nucleus uncovered candidate
## uncovered seeds
- q5 shell session rebuild display
- q8 visual highlight apply visual highlight fold regions
- q9 binary symbols panel scan shell output for linker errors
- q13 main layout apply editor navigation
- q14 main layout MakeMainLayout

Plan de búsqueda (romper):
1. [en_curso] A locator: dónde se leen los errores de compilación de la consola inferior
2. [pendiente] B locator: dónde se pinta la línea roja en el margen izquierdo del editor
3. [pendiente] P puente: hay camino leer los errores de la consola y pintar la línea roja en el editor (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M1
leído: clear editor line paint caches
Cerrado:
leído: clear editor line paint caches
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M1
leído: editor panel cpp
Cerrado:
leído: editor panel cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M1
leído: editor panel cpp
Cerrado:
leído: editor panel cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M7
leído: scan shell output for linker errors, tick binary symbols panel, parse linker undefined reference
Cerrado:
leído: scan shell output for linker errors, tick binary symbols panel, parse linker undefined reference
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: clear editor line paint caches

T2
    visto: editor panel cpp, clear editor line paint caches

T3
    visto: editor panel cpp

T4
    visto: scan shell output for linker errors, tick binary symbols panel, parse linker undefined reference
    extra: has value, request binary symbols panel

entre abiertas:
  T1=>T2  mismo objeto: clear editor line paint caches
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: editor panel cpp
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T4 → has value
  T4 → request binary symbols panel

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se pinta la línea roja en el margen izquierdo del editor
Cerrado de T1:
leído: clear editor line paint caches

T2 preguntó:
dónde se pinta la línea roja en el margen izquierdo del editor
Cerrado de T2:
leído: editor panel cpp

T3 preguntó:
dónde se pinta la línea roja en el margen izquierdo del editor
Cerrado de T3:
leído: editor panel cpp

T4 preguntó:
dónde se leen los errores de compilación de la consola inferior
Cerrado de T4:
leído: scan shell output for linker errors, tick binary symbols panel, parse linker undefined reference

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: pasar, no_pasar, revisar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
