Consulta del usuario (ancla, claim; no la copies entera a un hijo):
necesito localizar el módulo que calcula la trayectoria balística de los cursores del editor para sincronizarlos en tiempo real con un cluster de Kubernetes cuando el usuario parpadea, y dónde se persiste esa física en el índice vectorial cuántico del workspace

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=12  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M1  kind=caller  ov=6  stems: visual highlight visual highlight
owns: visual highlight
nucleus: fold regions revision, fold regions
peek: visual highlight apply visual highlight fold regions, visual highlight compute
port: M1=>M2 editor panel MakeEditorPanel -write-> scroll
M2  kind=chrome  ov=15  stems: editor panel editor panel
owns: editor panel
nucleus: scroll
peek: editor panel make breadcrumb bar, editor panel MakeEditorPanel
port: M2=>M1 visual highlight apply visual highlight fold regions -write-> fold regions
M3  kind=other  ov=9  stems: workspace model
owns: workspace model
gap: state,trigger,effect
peek: workspace model load tabular placeholder
M4  kind=caller  ov=9  stems: application application
owns: application
nucleus: ai indexes requested 
peek: application sync symbol workspace indexer, application set workspace
port: M4=>M5 application handle focus shortcuts -call-> console panel cycle console tab
M5  kind=chrome  ov=0  stems: console panel console panel
owns: console panel
nucleus: text input focus, focus sync needed
peek: console panel MakeConsolePanel, console panel open terminal link
port: M5=>M4 application handle focus shortcuts -call-> console panel cycle console tab
M6  kind=chrome  ov=6  stems: watches panel watches panel
owns: watches panel
nucleus: text input focus, watch selected
peek: watches panel MakeWatchesPanel, watches panel rebuild flat watches
port: M6=>M2 editor panel make breadcrumb bar -call-> raw pty screen text
M7  kind=hole  ov=0  stems: ai controller llama backend model store ai controller
owns: ai controller / model store download url to file
gap: no state
peek: model store download url to file, model store ensure llama cli
peek-edge: model store ensure llama cli -call-> model store cli runnable
M8  kind=hole  ov=0  stems: llama backend model store llama backend model store
owns: llama backend
gap: no state
peek: model store llama bundle dir for, model store runtime dir
peek-edge: model store llama bundle dir for -call-> model store runtime dir
M9  same=M4  kind=hole  ov=9  stems: application
peek: workspace model open new tab from disk
M10  kind=hole  ov=12  stems: text input style key bindings pty input call hierarchy view
owns: text input style / diagnostics panel MakeDiagnosticsPanel
nucleus: text input focus
peek: diagnostics panel MakeDiagnosticsPanel, symbol provider diagnostics revision
peek-edge: diagnostics panel MakeDiagnosticsPanel -call-> symbol provider diagnostics revision
M11  kind=hole  ov=0  stems: language server spec language server spec
owns: language server spec
gap: no state
peek: language server spec make texlab spec, language server spec make lemminx spec
peek-edge: language server spec make texlab spec -write-> command
M12  same=M4  kind=hole  ov=12  stems: application
peek: file picker MakeFilePickerOverlay
bridges: T1[M9,M4] T2[M2,M1] T3[M8,M7,M4] T4[M5,M2,M10]
holes: model store ensure llama cli model store llama bundle dir for workspace model stamp tab disk mtime diagnostics panel MakeDiagnosticsPanel

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se calcula la trayectoria de los cursores del editor para sincronizarlos en tiempo real
keep: M1
leído: cursor row, caret visual in view, caret column in body, show
Cerrado:
leído: cursor row, caret visual in view, caret column in body, show
Abierto:
- body start visual column


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: cursor row, caret visual in view, caret column in body, show
    extra: ansi 256 color, ansi base color
hacia el resto:
  T1 → ansi 256 color
  T1 → ansi base color

# examen
Ancla:
necesito localizar el módulo que calcula la trayectoria balística de los cursores del editor para sincronizarlos en tiempo real con un cluster de Kubernetes cuando el usuario parpadea, y dónde se persiste esa física en el índice vectorial cuántico del workspace

T1 preguntó:
dónde se calcula la trayectoria de los cursores del editor para sincronizarlos en tiempo real
Cerrado de T1:
leído: cursor row, caret visual in view, caret column in body, show

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: explorar, plan, cerrar
Este es el estado. Tú diriges: atomiza el trabajo (un hijo, un trozo). El plan no es un contrato: tras un Cerrado puedes soltarlo (explorar o plan nuevo). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. 'mismo objeto' en entre abiertas es que la caza no se movió. Si queda un tirón, explorar; si el trabajo no cabe en uno, plan. consulta = prompt de investigación (fenómeno y verbo; el hijo no ve el ancla); why = tu estrategia (el hijo no la lee). Lo que no se caza no se nombra. hacia opcional: conceptos de cerca, no ids. evita opcional. Un explorar es una sola rama; atomiza, no empaquetes dos cazas en un hijo. tú eliges cómo seguir. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
