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
keep: M2
leído: rebuild diagnostics by line
Cerrado:
leído: rebuild diagnostics by line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se parsean los errores de compilación de la salida de la consola y se convierten en diagnósticos del editor
keep: M8
leído: Make Diagnostics Panel, build rows
Cerrado:
leído: Make Diagnostics Panel, build rows
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se aplican los diagnósticos de la consola al panel del editor para que se dibujen
keep: M1
leído: apply lsp diagnostics to panel, sync diagnostic cache, push active file diagnostics from cache, cached file diagnostics
Cerrado:
encontré el objeto de la consulta: los diagnósticos se aplican al estado del panel en apply lsp diagnostics to panel que llena diagnostics by line vía push active file diagnostics from cache y se dibujan en el render del editor que lee esos mapas y invalida la vista vía invalidate editor view
Abierto:
- render loop
- draw line decorations


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: rebuild diagnostics by line
    extra: rebuild diagnostics by line if changed, apply lsp diagnostics to panel

T2
    visto: Make Diagnostics Panel, build rows
    extra: Make Console Panel, Make Main Layout

T3
    visto: apply lsp diagnostics to panel, sync diagnostic cache, push active file diagnostics from cache, cached file diagnostics
    extra: ISymbol Provider, allows lsp ui

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  apply lsp diagnostics to panel → push active file diagnostics from cache → rebuild diagnostics by line if changed → rebuild diagnostics by line
  T2=>T3  extra toca visto: build rows
hacia el resto:
  T1 → rebuild diagnostics by line if changed
  T1 → ISymbol Provider
  T2 → Make Console Panel
  T2 → Make Main Layout
  T3 → ISymbol Provider
  T3 → allows lsp ui

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
Cerrado de T1:
leído: rebuild diagnostics by line

T2 preguntó:
dónde se parsean los errores de compilación de la salida de la consola y se convierten en diagnósticos del editor
Cerrado de T2:
leído: Make Diagnostics Panel, build rows

T3 preguntó:
dónde se aplican los diagnósticos de la consola al panel del editor para que se dibujen
Cerrado de T3:
encontré el objeto de la consulta: los diagnósticos se aplican al estado del panel en apply lsp diagnostics to panel que llena diagnostics by line vía push active file diagnostics from cache y se dibujan en el render del editor que lee esos mapas y invalida la vista vía invalidate editor view

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
