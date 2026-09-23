Consulta del usuario (ancla, claim; no la copies entera a un hijo):
necesito localizar el módulo que calcula la trayectoria balística de los cursores del editor para sincronizarlos en tiempo real con un cluster de Kubernetes cuando el usuario parpadea, y dónde se persiste esa física en el índice vectorial cuántico del workspace

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=9  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
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
peek-edge: diagnos

Fichas ampliadas (pack+inspect. El inspect elige barrio; no copies símbolos; el fenómeno del ancla sí):
n=3  (owns+nucleus+peek+circuito; sin inspect gordo)
M1  kind=caller  ov=6
owns: visual highlight
nucleus: fold regions revision, fold regions
peek: visual highlight apply visual highlight fold regions, visual highlight compute
port: M1=>M2 editor panel MakeEditorPanel -write-> scroll
M2  kind=chrome  ov=15
owns: editor panel
nucleus: scroll
peek: editor panel make breadcrumb bar, editor panel MakeEditorPanel
port: M2=>M1 visual highlight apply visual highlight fold regions -write-> fold regions
M3  kind=other  ov=9
owns: workspace model
peek: workspace model load tabular placeholder
entre abiertas:
M1=>M2 editor panel MakeEditorPanel -write-> scroll
M2=>M1 visual highlight apply visual highlight fold regions -write-> fold regions
hacia el resto:
M6=>M2 editor panel make breadcrumb bar -call-> raw pty screen text
## M1
kind=caller
owns: visual highlight
nuclei: C2(fold regions revision) C3(fold regions)
anchors:
- G1: visual highlight compute[q1]
- G2: visual highlight apply visual highlight fold regions[q9]
closure: 2/2 groups, hub-free=2
roles:
- writers: visual highlight compute, visual highlight drain visual highlight results, visual highlight apply visual highlight fold regions
- readers: visual highlight apply visual highlight fold regions
- controls: visual highlight L728:guard, visual highlight L732:guard, visual highlight L742:if, visual highlight L737:if
mechanism:
- trigger: visual highlight apply visual highlight fold regions -enter ctrl-> visual highlight L728:guard if (snap.fold regions revision == 0)
- state: visual highlight compute -write(fold regions revision)-> fold regions revision
- effect: visual highlight compute -call-> ai controller clear
ports:
- M1=>M2 editor panel MakeEditorPanel -write-> scroll
causal edges:
- visual highlight compute -write(fold regions revision)-> fold regions revision [state]
- visual highlight apply visual highlight fold regions -enter ctrl-> visual highlight L728:guard [trigger] if (snap.fold regions revision == 0)
- visual highlight compute -call-> ai controller clear [effect]
- editor panel MakeEditorPanel -write(scroll)-> scroll [port]
mini-cards:
- visual highlight compute | VisualHighlightSnapshot VisualHighlightService compute(const VisualHighlightJob& job) const | roles=mutator | writes=snap.fold regions revision,snap.fold regions,snap.selection key | hot=multi return
· 347: root = ts tree root node(tree snapshot.tree copy);
· 350: bracket pair at(root, job.source, job.cursor line, job.cursor col);
· 358: scope bracket pair from tree(root, job.source, job.cursor line, job.cursor col);
· 361: snap.colored braces = colored curly braces(root, job.source);
- visual highlight apply visual highlight fold regions | bool apply visual highlight fold regions(EditorBuffer* buffer, VisualHighlightPanelState* state, | roles=mutator | writes=state.last applied fold revision,buffer.fold regions,changed | reads=snap,fold regions revision | hot=early return,multi return
· 719: const bool changed = !buffer->fold regions.empty() || !buffer->collapsed folds.empty();
· 720: buffer->fold regions.clear();
· 721: buffer->collapsed folds.clear();
· 731: const uint64 t current rev = tree sitter service().revision for(buffer->path);
- visual highlight drain visual highlight results | bool drain visual highlight results(VisualHighlightPanelState* state, const EditorBuffer& buffer, | roles=mutator,ui | writes=state.dirty,merged.fold regions,merged.fold regions revision | hot=early return,multi return,wake
risks: low top margin
## M2
kind=chrome
owns: editor panel
nuclei: C5(scroll)
anchors:
- G1: editor panel handle breadcrumb click[q3]
- G2: editor panel make breadcrumb bar[q0]
closure: 2/2 groups, hub-free=1
roles:
- writers: editor panel handle breadcrumb click, editor panel MakeEditorPanel, editor panel handle scrollbar mouse, editor panel ensure virtual caret visible
mechanism:
- trigger: editor panel handle editor chrome mouse -call-> editor panel handle breadcrumb click
- state: editor panel handle breadcrumb click -write(scroll)-> scroll
- effect: editor panel MakeEditorPanel -call-> editor panel make breadcrumb bar
ports:
- M2=>M1 visual highlight apply visual highlight fold regions -write-> fold regions
causal edges:
- editor panel handle breadcrumb click -write(scroll)-> scroll [state]
- editor panel handle editor chrome mouse -call-> editor panel handle breadcrumb click [trigger]
- editor panel MakeEditorPanel -call-> editor panel make breadcrumb bar [effect]
- visual highlight apply visual highlight fold regions -write(fold regions)-> fold regions [port]
mini-cards:
- editor panel handle breadcrumb click | bool handle breadcrumb click(WorkspaceModel* workspace, FocusManagerState* focus, | roles=mutator | writes=workspace.buffer.scroll | reads=hit,line,max | hot=early return,multi return
· 1620: workspace->ensure buffer();
· 1621: commit undo group(&workspace->buffer);
· 1622: workspace->buffer.reset to single cursor(hit.line, 0);
· 1623: workspace->buffer.scroll = std max(0, hit.line - 2);
- editor panel make breadcrumb bar | Element make breadcrumb bar(const std vector<BreadcrumbItem>& crumbs, | roles=mutator | writes=meta,problems btn,problems color | reads=to string,panel state,buffer
- editor panel clamp virtual scroll | void clamp virtual scroll(EditorPanelState* panel, EditorBuffer* buffer, int visible lines) | roles=mutator | writes=buffer.scroll | reads=allowed,max,min
risks: large zone hub dependent bridge
## M3
kind=other
owns: workspace model
nuclei:
anchors:
- G1: workspace model load tabular placeholder[q2]
closure: 0/1 groups, hub-free=0
roles:
skeleton missing: state trigger effect
causal edges:
mini-cards:
- workspace model load tabular placeholder | bool load tabular placeholder(EditorBuffer* buffer, const std string& absolute path) | roles=mutator | writes=buffer.path | reads=absolute path | hot=multi return
· 68: if (buffer == nullptr) {
· 69: return false;
· 71: set welcome buffer(buffer);
· 72: buffer->path = absolute path;
risks: no state nucleus
## zone bridges
- T2: M2 M1 | media cosine (4 hops)
## uncovered seeds
- q5 model store ensure llama cli
- q6 model store llama bundle dir for
- q7 workspace model stamp tab disk mtime
- q10 diagnostics panel MakeDiagnosticsPanel
- q11 language server spec make texlab spec

Trabajos ya hechos:
(ninguno)

Legal ahora: explorar, ampliar, plan, cerrar
Abiertas están completas (inspect). entre abiertas son ports, no cosine. Atomiza: cada encargo es un trozo que un hijo cierra. Si el ancla enumera dos disparos, la consulta nombra UNO de lo visto. Si junta un gesto y un efecto, no asumas el arco: parte o pregunta si hay camino después de leer los dos polos. Si cabe en un tirón, explorar: consulta = fenómeno y verbo del usuario (apagar, capturar, detener); el inspect elige el barrio, no el caller del writer. No una plantilla ('invoca la función que…') ni el ancla copiado. Nombrar el fenómeno no es recap; compartir palabras con el ancla tampoco. Si el trabajo cruza fronteras que un hijo no une, plan: cada locator y el puente traen consulta tuya; hacia es cerca, no el briefing. El runtime no redacta. why = por qué ese grano; el hijo no la lee. hacia opcional: 1–3 palabras, no el texto de un port (si es largo se recorta). Ampliar solo ids que aún no estén abiertos. No copies ::símbolos ni stems. Chrome no es el disparo. kind=hole que solo rima con el ancla no es el objeto. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
