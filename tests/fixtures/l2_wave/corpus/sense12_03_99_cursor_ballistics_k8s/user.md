Consulta del usuario (ancla, claim; no la copies entera a un hijo):
necesito localizar el módulo que calcula la trayectoria balística de los cursores del editor para sincronizarlos en tiempo real con un cluster de Kubernetes cuando el usuario parpadea, y dónde se persiste esa física en el índice vectorial cuántico del workspace

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=12  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M1  kind=latch  ov=15  stems: editor panel editor panel
owns: editor panel
nucleus: scroll, region, semantic tokens enqueue pending, h scrollbar layout
peek: editor panel make breadcrumb bar, editor panel MakeEditorPanel
port: M1=>M2 workspace model reload stale tabs from disk -call-> workspace model reload active tab from disk
M2  kind=caller  ov=9  stems: workspace model workspace model
owns: workspace model
nucleus: buffer
peek: workspace model load tabular placeholder, workspace model load active tab into buffer
port: M2=>M4 workspace model clear tabs -call-> workspace model set welcome buffer
M3  kind=caller  ov=6  stems: visual highlight
owns: visual highlight
gap: state,trigger,effect
peek: visual highlight compute
port: M3=>M4 visual highlight compute -call-> ai controller clear
M4  kind=caller  ov=9  stems: application application
owns: application
nucleus: ai indexes requested 
peek: application sync symbol workspace indexer, application set workspace
port: M4=>M2 workspace model clear tabs -call-> workspace model set welcome buffer
M5  kind=chrome  ov=6  stems: search panel search panel
owns: search panel
nucleus: text input focus
peek: search panel MakeSearchPanel, search panel clear search input focus
port: M5=>M1 editor panel make breadcrumb bar -call-> raw pty screen text
M6  kind=chrome  ov=6  stems: watches panel watches panel
owns: watches panel
nucleus: text input focus
peek: watches panel MakeWatchesPanel, watches panel handle breakpoint hw input
port: M6=>M2 watches panel MakeWatchesPanel -call-> workspace model open file at
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
M9  kind=hole  ov=0  stems: language server spec language server spec
owns: language server spec
gap: no state
peek: language server spec make texlab spec, language server spec make lemminx spec
peek-edge: language server spec make texlab spec -write-> command
M10  kind=hole  ov=0  stems: file picker file tree panel file picker file tree panel
owns: file picker / file tree panel MakeFileTreePanel
gap: no state
peek: file tree panel MakeFileTreePanel, file picker sync index
peek-edge: file tree panel MakeFileTreePanel -call-> file picker sync index
M11  kind=hole  ov=12  stems: text input style performance panel key bindings shell session
owns: text input style / diagnostics panel MakeDiagnosticsPanel
gap: no state
peek: diagnostics panel MakeDiagnosticsPanel, clickable interaction clear hover if
peek-edge: diagnostics panel MakeDiagnosticsPanel -call-> clickable interaction clear hover if
M12  kind=hole  ov=9  stems: shutdown overlay visual highlight workspace model ai controller
owns: shutdown overlay
gap: no state
peek: ai controller find enclosing callable, best
peek-edge: ai controller find enclosing callable -write-> best
bridges: T2[M6,M1] T3[M2,M11] T4[M2,M6,M11] T5[M2,M11,M1]
holes: model store ensure llama cli model store llama bundle dir for language server spec make texlab spec file tree panel MakeFileTreePanel

Trabajos ya hechos:
(ninguno)

Legal ahora: explorar, ampliar, plan, cerrar
Aún no hay exploradores. El pack son las fichas. Si ov empatado o no hueles un mecanismo en owns/núcleo, amplia 1–3 M*. No copies el ancla: aún no hay inspect. El prompt de investigación sale de lo visto. Si el ancla enumera, la consulta no enumera. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
