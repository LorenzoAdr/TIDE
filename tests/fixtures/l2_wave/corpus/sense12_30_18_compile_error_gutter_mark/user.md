Consulta del usuario (ancla, claim; no la copies entera a un hijo):
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

luz (calibración del piloto; no copies): lsp, editor, ui, build, terminal
plano (calor del ancla; no copies nombres ni ids):
  ui ●●●  ai_missing_toast ai_path_scope_modal ai_transcript_view binary_symbols_panel bindings_store busy_strip  habla: util, ai, app, editor
  util ●  build_file_highlight child_process_guard clangd_workspace_setup compile_commands_lookup compile_commands_remap compile_commands_setup  habla: ui, ai, app, editor
  ai ●  action_json ai_packages ai_path_scope ai_trace ai_types coding_stem_embed_index  habla: ui, util, app, editor
  app ●  app_mode debug_model editor_tabs language_override workspace_detect workspace_session  habla: ui, util, ai, editor
  editor ●  ctx bracket_match clipboard code_snippets cursor_history editor_buffer_source  habla: ui, util, ai, app
  lsp ●  diagnostics gfortran_diagnostics language_server_spec lsp_position lsp_sync lsp_text_edits  habla: util, app, symbols, i18n
  symbols ●  call_hierarchy code_action completion_snippet hover_info source_symbol_resolver symbol_kind  habla: util, editor, lsp, indexer
  terminal ●  app_session feed_pty_bytes_locked master pty_input raw_pty_screen shell_session  habla: ui, util, dap
  i18n ●  locale strings_en strings_es strings_internal current_locale_setting detect_system_locale
  build ●  build_artifact_watcher build_environment build_environment_detector build_environment_selector build_environment_service build_environment_state  habla: ui, util, app
  backend ●  idebug_backend adapter_ dap_backend inferior_stopped_ response session_  habla: ui, util, i18n, dap
  git ●  git_command git_diff git_log git_status git_subrepos repo_root  habla: util, ai, i18n
  indexer ●  index_rules symbol_workspace_indexer workspace_indexer workspace_indexer_rg append_dir_globs append_heavy_dir_globs  habla: util, app, symbols, parser
  parser ●  tree_sitter_ast_utils tree_sitter_completion tree_sitter_highlight tree_sitter_language tree_sitter_service tree_sitter_symbols  habla: util, editor, lsp, symbols
  search ●  workspace_search workspace_search_rg workspace_search_runner append_always_skip_dir_globs append_glob_args basename  habla: util, ai, indexer
  core_analyzer ·  output_parser build_obj_search_command heap_block_re parse_hex_address  habla: app
  dap ·  debug_adapter_process debug_adapter_spec gdb_launcher gdb_protocol debug_adapter_kind_for_program fd_  habla: util
  packet_monitor ·  pkt_connections pkt_monitor_service pkt_preload_path pkt_store af_inet array_count  habla: util
  src ·  main path_is_existing_regular_file path_looks_like_source_or_text program  habla: util, app, i18n, toolpacks
  toolpacks ·  download export_portable install language_packs manifest packaged  habla: util
  top ui: terminal_ui_channel, editor_panel

zoom lsp (stems; no copies ids):
  lsp_transport  lsp_transport stdin_fd_ wait_response running_  habla: gdb_launcher, monitor_log, thread_name
  diagnostics  diagnostics lsp_ui_allowed allowed_paths build_diagnostic_suffix  habla: include_tree, lsp_sync, lsp_uri, symbol_provider
  lsp_client  documents_ intentionally_stopping_ is_null doc_it  habla: lsp_transport, diagnostics, language_server_spec, workspace_config
  language_server_spec  location needs_stdio_flag language_id_is_cpp_family language_server_spec  habla: bundled_tools, virtual_text_file
  gfortran_diagnostics  gfortran_diagnostics is_fixed_form_extension gfortran_path parse_gfortran_stderr  habla: diagnostics, shell_utils, virtual_text_file
  lsp_position  lsp_position line_text_at lsp_utf16_column make_lsp_position
  lsp_sync  lsp_sync
  lsp_text_edits  lsp_text_edits apply_lsp_text_edits byte_offset_at byte_offset_to_line_byte_col  habla: lsp_position, lsp_uri, path_normalize
  … +2 stems
  data: lsp_transport, lsp_client
  deadline: lsp_transport, lsp_client
  find: lsp_transport, diagnostics
zoom editor (stems; no copies ids):
  editor_render  line_index coloredbrace diagnosticerror diagnosticwarning  habla: build_file_highlight, theme, diagnostics, language_server_spec
  editor_text  editor_text emplace_back empty end  habla: line_source, text_rope
  visual_highlight  code_folding debounce_wake_scheduled job_inflight visual_highlight  habla: main_layout, ui_invalidation_policy, app_settings, tree_sitter_document
  bracket_match  bracket_match find_bracket_pair_highlight find_colored_curly_braces find_enclosing_block_comment  habla: tree_sitter_document, editor_buffer_source, editor_state, tree_sitter_service
  clipboard  clipboard cut_selection editor_clipboard extract_selection_text  habla: system_clipboard, editor_state, text_ops, undo_stack
  code_snippets  prefix_match code_snippets kstructuresnippetcompletionsenabled keyword  habla: lsp_uri, symbol_provider, tr
  cursor_history  back_ cursor_history apply_entry current_entry  habla: workspace_model, text_ops
  doc_comment  is_callable_kind kpythondocstring kdoxygenblock kfortranbang  habla: lsp_uri, symbol_kind, virtual_text_file
  … +20 stems
  editor_focused: editor_render, visual_highlight
  item: editor_render, visual_highlight
  selection_occurrences: editor_render, visual_highlight

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
1. [en_curso] A locator: dónde se extraen los errores de compilación del texto de la consola para crear diagnósticos
2. [pendiente] B locator: dónde se pinta el carácter del gutter en la UI del editor
3. [pendiente] P puente: hay camino extraer errores de la consola y pintar el carácter del gutter (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
keep: M1
leído: diagnostics for editor line, line diagnostic marker from map, line gutter marker
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el dibujo de la línea roja; lo leído son funciones que devuelven un carácter para el gutter, pero no el código que pinta ese carácter en la UI
Abierto:
- paint gutter
- draw line marker

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior para extraer la línea exacta
keep: M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se leen los errores de compilación de la consola inferior para extraer la línea exacta
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se pinta el carácter del gutter en la UI del editor
keep: M1
leído: diagnostics for editor line, line diagnostic marker from map, line gutter marker, Make Editor Panel
Cerrado:
leído: diagnostics for editor line, line diagnostic marker from map, line gutter marker, Make Editor Panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se extraen los errores de compilación del texto de la consola para crear diagnósticos
keep: M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se extraen los errores de compilación del texto de la consola para crear diagnósticos
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, line diagnostic marker from map, line gutter marker
    extra: Make Editor Panel, Make Main Layout

T2
    visto: (nada)

T3
    visto: diagnostics for editor line, line diagnostic marker from map, line gutter marker, Make Editor Panel
    extra: Make Main Layout, git line changed

T4
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: diagnostics for editor line
  T1=>T4  sin camino
  T2=>T3  sin camino
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → git line changed
  T3 → Make Main Layout
  T3 → git line changed

# examen
Ancla:
quiero que los errores de compilación que aparecen en la consola inferior también se marquen con una línea roja en el margen izquierdo del editor en la línea exacta donde está el error

T1 preguntó:
dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el dibujo de la línea roja; lo leído son funciones que devuelven un carácter para el gutter, pero no el código que pinta ese carácter en la UI

T2 preguntó:
dónde se leen los errores de compilación de la consola inferior para extraer la línea exacta
Cerrado de T2:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
(vacío)

T3 preguntó:
dónde se pinta el carácter del gutter en la UI del editor
Cerrado de T3:
leído: diagnostics for editor line, line diagnostic marker from map, line gutter marker, Make Editor Panel

T4 preguntó:
dónde se extraen los errores de compilación del texto de la consola para crear diagnósticos
Cerrado de T4:
El hijo cerró esta pregunta: no hay objeto. No se relanza.
(vacío)

Esta pregunta ya tiene respuesta (no hay objeto). No la relances ni en otro keep. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no refutes el claim entero.

Legal ahora: no_pasar, revisar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Puerta de la fase en curso. pasar si el Cerrado afirma el objeto de esta fase; un visto que el Cerrado niega no es paso. no_pasar salta a la siguiente caza (no gastes otro hijo en el mismo pico). La última caza cerró: el siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
