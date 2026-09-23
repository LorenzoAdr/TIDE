Consulta del usuario (ancla, claim; no la copies entera a un hijo):
necesito localizar el módulo que calcula la trayectoria balística de los cursores del editor para sincronizarlos en tiempo real con un cluster de Kubernetes cuando el usuario parpadea, y dónde se persiste esa física en el índice vectorial cuántico del workspace

luz (calibración del piloto; no copies): ai, app, editor, indexer
plano (calor del ancla; no copies nombres ni ids):
  ui ●●●  ai_missing_toast ai_path_scope_modal ai_transcript_view binary_symbols_panel bindings_store busy_strip  habla: ai, util, editor, app
  ai ●●  action_json ai_packages ai_path_scope ai_trace ai_types coding_stem_embed_index  habla: ui, util, editor, app
  util ●●  build_file_highlight child_process_guard clangd_workspace_setup compile_commands_lookup compile_commands_remap compile_commands_setup  habla: ui, ai, editor, app
  editor ●  ctx bracket_match clipboard code_snippets cursor_history editor_buffer_source  habla: ui, ai, util, app
  app ●  app_mode debug_model editor_tabs language_override workspace_detect workspace_session  habla: ui, ai, util, editor
  indexer ●  index_rules symbol_workspace_indexer workspace_indexer workspace_indexer_rg append_dir_globs append_heavy_dir_globs  habla: util, app, parser, symbols
  terminal ●  app_session feed_pty_bytes_locked master pty_input raw_pty_screen shell_session  habla: ui, util, dap
  lsp ●  diagnostics gfortran_diagnostics language_server_spec lsp_position lsp_sync lsp_text_edits  habla: util, app, indexer, symbols
  parser ●  tree_sitter_ast_utils tree_sitter_completion tree_sitter_highlight tree_sitter_language tree_sitter_service tree_sitter_symbols  habla: util, editor, terminal, lsp
  search ●  workspace_search workspace_search_rg workspace_search_runner append_always_skip_dir_globs append_glob_args basename  habla: ai, util, indexer
  git ●  git_command git_diff git_log git_status git_subrepos repo_root  habla: ai, util, i18n
  symbols ●  call_hierarchy code_action completion_snippet hover_info source_symbol_resolver symbol_kind  habla: util, editor, indexer, lsp
  backend ●  idebug_backend adapter_ dap_backend inferior_stopped_ response session_  habla: ui, util, dap, i18n
  build ●  build_artifact_watcher build_environment build_environment_detector build_environment_selector build_environment_service build_environment_state  habla: ui, util, app
  toolpacks ●  download export_portable install language_packs manifest packaged  habla: util
  core_analyzer ·  output_parser build_obj_search_command heap_block_re parse_hex_address  habla: app
  dap ·  debug_adapter_process debug_adapter_spec gdb_launcher gdb_protocol debug_adapter_kind_for_program fd_  habla: util
  i18n ·  locale strings_en strings_es strings_internal current_locale_setting detect_system_locale
  packet_monitor ·  pkt_connections pkt_monitor_service pkt_preload_path pkt_store af_inet array_count  habla: util
  src ·  main path_is_existing_regular_file path_looks_like_source_or_text program  habla: util, app, toolpacks, i18n
  top ui: editor_panel, ui_invalidation_policy

zoom editor (stems; no copies ids):
  cursor_history  back_ cursor_history apply_entry current_entry  habla: text_ops, workspace_model
  doc_comment  is_callable_kind kpythondocstring kdoxygenblock kfortranbang  habla: virtual_text_file, lsp_uri, symbol_kind
  text_ops  extend_selection any_cursor_has_selection removed closing_for_open_char  habla: clang_format_config, completion_snippet, helix_textobjects, bracket_match
  helix_textobjects  brace_pair_in_symbol_range helix_textobjects piece ctx  habla: text_ops, bracket_match, editor_context, editor_state
  editor_render  line_index coloredbrace diagnosticerror diagnosticwarning  habla: clang_format_config, raw_pty_screen, theme, bracket_match
  editor_text  editor_text emplace_back empty end  habla: line_source, text_rope
  visual_highlight  code_folding debounce_wake_scheduled job_inflight visual_highlight  habla: ui_invalidation_policy, main_layout, tree_sitter_document, git_diff
  bracket_match  bracket_match find_bracket_pair_highlight find_colored_curly_braces find_enclosing_block_comment  habla: tree_sitter_document, editor_buffer_source, editor_state, tree_sitter_service
  … +20 stems
  line: text_ops, editor_render, helix_textobjects, visual_highlight
  static_cast: text_ops, helix_textobjects, doc_comment, editor_render
  path: cursor_history, doc_comment, visual_highlight, helix_scope_nav
zoom ai (stems; no copies ids):
  level2_session  ast append_observation gaps rejected_targets  habla: search_replace, virtual_text_file, l2_effect_registry, l2_effect_summary
  l2_think  high medium off apply_think_for_request  habla: l2_brain, llama_backend
  edit_journal  edit_journal apply_demo apply_replace author  habla: lsp_text_edits, text_ops, virtual_text_file, ai_types
  llama_net  apply_ai_runtime_env getaddrinfo llama_host_is_local llama_net  habla: ai_types
  ai_controller  agent_busy_ settings_ download_busy_ embed_backend_  habla: level2_session, edit_journal, git_command, llama_net
  l2_wave  candidatas cerrar encurso wave_control_induce_miss_answer  habla: action_json, get_code_of, key_binding_registry, l2_cerca_wake
  search_replace  hunk find_unique_span looks_like_aider_path search_replace  habla: virtual_text_file
  coding_stem_embed_index  baseline by_stem_ coding_stem_embed_index hdrdoc  habla: symbol_workspace_indexer, virtual_text_file, ai_trace, embedding_backend
  … +43 stems
  find: level2_session, llama_net, l2_wave, search_replace
  line: level2_session, edit_journal, l2_wave, ai_controller
  npos: level2_session, llama_net, l2_wave, search_replace

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
M4  kind=other  ov=0  stems: model store
owns: model store
gap: state,trigger,effect
peek: model store ensure llama cli
M5  kind=caller  ov=9  stems: application application
owns: application
nucleus: ai indexes requested 
peek: application sync symbol workspace indexer, application set workspace
port: M5=>M6 application handle focus shortcuts -call-> console panel cycle console tab
M6  kind=chrome  ov=0  stems: console panel console panel
owns: console panel
nucleus: text input focus, focus sync needed
peek: console panel MakeConsolePanel, console panel open terminal link
port: M6=>M5 application handle focus shortcuts -call-> console panel cycle console tab
M7  kind=chrome  ov=6  stems: watches panel watches panel
owns: watches panel
nucleus: text input focus, watch selected
peek: watches panel MakeWatchesPanel, watches panel rebuild flat watches
port: M7=>M2 editor panel make breadcrumb bar -call-> raw pty screen text
M8  kind=hole  ov=0  stems: llama backend model store llama backend model store
owns: llama backend
gap: no state
peek: model store llama bundle dir for, model store runtime dir
peek-edge: model store llama bundle dir for -call-> model store runtime dir
M9  same=M5  kind=hole  ov=9  stems: application
peek: workspace model open new tab from disk, workspace model save buffer
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
M12  same=M5  kind=hole  ov=12  stems: application
peek: file picker MakeFilePickerOverlay, symbol picker MakeSymbolPickerOverlay
bridges: T1[M9,M5] T3[M9,M5] T4[M10,M2] T5[M2,M10]
holes: model store llama bundle dir for workspace model stamp tab disk mtime diagnostics panel MakeDiagnosticsPanel language server spec make texlab spec

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se calcula la trayectoria balística de los cursores para sincronizarlos con un cluster cuando el usuario parpadea
keep: M6 M9
leído: record jump, show
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el cálculo de trayectoria balística; lo leído es un historial de saltos de edición cursor history y un toggle de visibilidad de parpadeo cursor blink, sin lógica de interpolación ni sincronización con cluster
Abierto:
- ballistic trajectory
- cursor sync cluster


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: record jump, show
    extra: current entry
hacia el resto:
  T1 → current entry

# examen
Ancla:
necesito localizar el módulo que calcula la trayectoria balística de los cursores del editor para sincronizarlos en tiempo real con un cluster de Kubernetes cuando el usuario parpadea, y dónde se persiste esa física en el índice vectorial cuántico del workspace

T1 preguntó:
dónde se calcula la trayectoria balística de los cursores para sincronizarlos con un cluster cuando el usuario parpadea
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el cálculo de trayectoria balística; lo leído es un historial de saltos de edición cursor history y un toggle de visibilidad de parpadeo cursor blink, sin lógica de interpolación ni sincronización con cluster

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: zoom, explorar, plan, cerrar
Este es el estado. Tú diriges: atomiza el trabajo (un hijo, un trozo). El plan no es un contrato: tras un Cerrado puedes soltarlo (explorar o plan nuevo). Esta pregunta ya tiene respuesta (no hay objeto). No la relances ni en otro keep. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero. 'mismo objeto' en entre abiertas es que la caza no se movió. Si queda un tirón, explorar; si el trabajo no cabe en uno, plan. consulta = prompt de investigación (fenómeno y verbo; el hijo no ve el ancla); why = tu estrategia (el hijo no la lee). Lo que no se caza no se nombra. hacia opcional: conceptos de cerca, no ids. evita opcional. Un explorar es una sola rama; atomiza, no empaquetes dos cazas en un hijo. tú eliges cómo seguir. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
