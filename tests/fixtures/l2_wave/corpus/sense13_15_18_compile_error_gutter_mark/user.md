Consulta del usuario (ancla, claim; no la copies entera a un hijo):
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

bosquejo (no es el código; no copies nombres ni ids):
  olores:
    errores compilación ~ (sin olor)
    margen editor ~ ui (c=0.52, twin editor)
    decoración línea ~ (sin olor)
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
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
keep: M2
leído: rebuild diagnostics by line if changed, line gutter marker, line diagnostic marker from map, rebuild diagnostics by line, diagnostics for editor line
Cerrado:
encontré el mecanismo: la línea roja marcador se determina en line gutter marker que delega en line diagnostic marker from map para devolver si hay un diagnóstico de severidad k Error en esa línea. El dibujo visual propiamente dicho ocurre en el renderizado del gutter que consume este carácter, pero la lógica de decisión está aquí.
Abierto:
- render gutter line

### Trabajo 2
consulta: dónde se insertan los diagnósticos de error en el mapa que consulta el marcador del margen
keep: M9
leído: rebuild diagnostics by line, line gutter marker, line diagnostic marker from map
Cerrado:
encontré el objeto de la consulta: la inserción ocurre en rebuild diagnostics by line línea 833: panel diagnostics by line item line push back que es llamado por rebuild diagnostics by line if changed . El marcador del margen line gutter marker consulta ese mapa vía line diagnostic marker from map - diagnostics for editor line .
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se aplican los diagnósticos de LSP al panel del editor para que aparezcan en el margen
keep: M1
leído: apply lsp diagnostics to panel, push active file diagnostics from cache, line diagnostic marker from map, line gutter marker, diagnostics for editor line
Cerrado:
encontré el mecanismo: apply lsp diagnostics to panel llena el estado diagnostics by line y line gutter marker consulta ese estado para pintar el margen
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: rebuild diagnostics by line if changed, line gutter marker, line diagnostic marker from map, rebuild diagnostics by line
    extra: apply lsp diagnostics to panel, push active file diagnostics from cache

T2
    visto: rebuild diagnostics by line, line gutter marker, line diagnostic marker from map
    extra: rebuild diagnostics by line if changed, apply lsp diagnostics to panel

T3
    visto: apply lsp diagnostics to panel, push active file diagnostics from cache, line diagnostic marker from map, line gutter marker
    extra: ISymbol Provider, allows lsp ui

entre abiertas:
  T1=>T2  mismo objeto: line gutter marker
  T1=>T3  mismo objeto: line gutter marker
  T1=>T3  apply lsp diagnostics to panel → push active file diagnostics from cache → rebuild diagnostics by line if changed
  T2=>T3  mismo objeto: line gutter marker
  T2=>T3  apply lsp diagnostics to panel → push active file diagnostics from cache → rebuild diagnostics by line if changed → rebuild diagnostics by line
hacia el resto:
  T1 → ISymbol Provider
  T1 → panel diagnostics match doc
  T2 → ISymbol Provider
  T2 → panel diagnostics match doc
  T3 → ISymbol Provider
  T3 → allows lsp ui

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
Cerrado de T1:
encontré el mecanismo: la línea roja marcador se determina en line gutter marker que delega en line diagnostic marker from map para devolver si hay un diagnóstico de severidad k Error en esa línea. El dibujo visual propiamente dicho ocurre en el renderizado del gutter que consume este carácter, pero la lógica de decisión está aquí.

T2 preguntó:
dónde se insertan los diagnósticos de error en el mapa que consulta el marcador del margen
Cerrado de T2:
encontré el objeto de la consulta: la inserción ocurre en rebuild diagnostics by line línea 833: panel diagnostics by line item line push back que es llamado por rebuild diagnostics by line if changed . El marcador del margen line gutter marker consulta ese mapa vía line diagnostic marker from map - diagnostics for editor line .

T3 preguntó:
dónde se aplican los diagnósticos de LSP al panel del editor para que aparezcan en el margen
Cerrado de T3:
encontré el mecanismo: apply lsp diagnostics to panel llena el estado diagnostics by line y line gutter marker consulta ese estado para pintar el margen

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
