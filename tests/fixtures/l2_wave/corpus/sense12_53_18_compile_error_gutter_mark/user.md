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

Plan de búsqueda (romper):
1. [en_curso] A locator: dónde se aplican los errores leídos de la consola al sistema de resaltado visual del editor
2. [pendiente] B locator: dónde se configuran los colores y estilos de las líneas de error en el margen
3. [pendiente] P puente: hay camino aplicar los errores al resaltado y configurar su estilo visual (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1
leído: mark visual highlight inputs dirty, lsp sync pending
Cerrado:
leído: mark visual highlight inputs dirty, lsp sync pending
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación del texto de la consola para marcarlos
keep: M4
leído: parse gcc style, parse msvc style, problems visible terminal lines, terminal row link, find compiler location
Cerrado:
leído: parse gcc style, parse msvc style, problems visible terminal lines, terminal row link, find compiler location
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se aplican los errores leídos de la consola al sistema de resaltado visual del editor
keep: M1
leído: diagnostics for editor line, diagnostics on line, problems visible terminal lines, Make Console Panel, visual highlight config from settings, compute
Cerrado:
leído: diagnostics for editor line, diagnostics on line, problems visible terminal lines, Make Console Panel, visual highlight config from settings, compute
Abierto:
- handle editor mouse


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: mark visual highlight inputs dirty, lsp sync pending
    extra: sync git cache, Make Editor Panel

T2
    visto: parse gcc style, parse msvc style, problems visible terminal lines, terminal row link
    extra: parse positive int, pop back

T3
    visto: diagnostics for editor line, diagnostics on line, problems visible terminal lines, Make Console Panel
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: problems visible terminal lines
  T2=>T3  Make Console Panel → handle console panel mouse → terminal link at cell → terminal row link → find compiler location → parse gcc style
hacia el resto:
  T1 → sync git cache
  T1 → Make Editor Panel
  T2 → parse positive int
  T2 → pop back
  T3 → Make Editor Panel
  T3 → Make Main Layout

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se marcan los errores de compilación en el margen del editor
Cerrado de T1:
leído: mark visual highlight inputs dirty, lsp sync pending

T2 preguntó:
dónde se leen los errores de compilación del texto de la consola para marcarlos
Cerrado de T2:
leído: parse gcc style, parse msvc style, problems visible terminal lines, terminal row link, find compiler location

T3 preguntó:
dónde se aplican los errores leídos de la consola al sistema de resaltado visual del editor
Cerrado de T3:
leído: diagnostics for editor line, diagnostics on line, problems visible terminal lines, Make Console Panel, visual highlight config from settings, compute

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO 'el ancla queda contestada'.
