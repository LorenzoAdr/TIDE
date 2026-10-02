Consulta del usuario (ancla, claim; no la copies entera a un hijo):
necesito localizar el módulo que calcula la trayectoria balística de los cursores del editor para sincronizarlos en tiempo real con un cluster de Kubernetes cuando el usuario parpadea, y dónde se persiste esa física en el índice vectorial cuántico del workspace

luz (calibración del piloto; no copies): cursor, blink, kubernetes, vector, physics, sync
plano (calor del ancla; no copies nombres ni ids):
  ui ●●●  ai_missing_toast ai_path_scope_modal ai_transcript_view binary_symbols_panel bindings_store busy_strip  habla: util, editor, ai, parser
  util ●●  build_file_highlight child_process_guard clangd_workspace_setup compile_commands_lookup compile_commands_remap compile_commands_setup  habla: ui, editor, ai, parser
  editor ●●  ctx bracket_match clipboard code_snippets cursor_history editor_buffer_source  habla: ui, util, ai, parser
  ai ●  action_json ai_packages ai_path_scope ai_trace ai_types coding_stem_embed_index  habla: ui, util, editor, parser
  parser ●  tree_sitter_ast_utils tree_sitter_completion tree_sitter_highlight tree_sitter_language tree_sitter_service tree_sitter_symbols  habla: util, editor, symbols, lsp
  symbols ●  call_hierarchy code_action completion_snippet hover_info source_symbol_resolver symbol_kind  habla: util, editor, parser, lsp
  app ●  app_mode debug_model editor_tabs language_override workspace_detect workspace_session  habla: ui, util, editor, ai
  lsp ●  diagnostics gfortran_diagnostics language_server_spec lsp_position lsp_sync lsp_text_edits  habla: util, symbols, app, dap
  backend ·  idebug_backend adapter_ dap_backend inferior_stopped_ response session_  habla: ui, util, dap, i18n
  build ·  build_artifact_watcher build_environment build_environment_detector build_environment_selector build_environment_service build_environment_state  habla: ui, util, app
  core_analyzer ·  output_parser build_obj_search_command heap_block_re parse_hex_address  habla: app
  dap ·  debug_adapter_process debug_adapter_spec gdb_launcher gdb_protocol debug_adapter_kind_for_program fd_  habla: util
  git ·  git_command git_diff git_log git_status git_subrepos repo_root  habla: util, ai, i18n
  i18n ·  locale strings_en strings_es strings_internal current_locale_setting detect_system_locale
  indexer ·  index_rules symbol_workspace_indexer workspace_indexer workspace_indexer_rg append_dir_globs append_heavy_dir_globs  habla: util, parser, symbols, app
  packet_monitor ·  pkt_connections pkt_monitor_service pkt_preload_path pkt_store af_inet array_count  habla: util
  search ·  workspace_search workspace_search_rg workspace_search_runner append_always_skip_dir_globs append_glob_args basename  habla: util, ai, indexer
  src ·  main path_is_existing_regular_file path_looks_like_source_or_text program  habla: util, app, i18n, toolpacks
  terminal ·  app_session feed_pty_bytes_locked master pty_input raw_pty_screen shell_session  habla: ui, util, dap
  toolpacks ·  download export_portable install language_packs manifest packaged  habla: util
  top ui: text_input_style, watches_panel

zoom editor (stems; no copies ids):
  text_ops  extend_selection any_cursor_has_selection removed closing_for_open_char  habla: editor_state, text_search, completion_snippet, cursor_blink
  text_search  active_request_id all_of completion_replace_range_at_cursor match_within_range  habla: text_ops, editor_state, clipboard
  editor_state  editor_state clamp_all_cursors clamp_cursor collapse_to_head  habla: cursor_blink, editor_folds, editor_buffer_source, editor_text
  helix_dispatch  helix text_object_context ctx helix_dispatch  habla: text_ops, editor_state, main_layout, text_search
  editor_render  line_index coloredbrace diagnosticerror diagnosticwarning  habla: theme, editor_state, text_search, cursor_blink
  editor_folds  open_line fold_line_hidden regions buffer_line  habla: editor_state
  bracket_match  bracket_match find_bracket_pair_highlight find_colored_curly_braces find_enclosing_block_comment  habla: editor_state, tree_sitter_document, editor_buffer_source, tree_sitter_service
  clipboard  clipboard cut_selection editor_clipboard extract_selection_text  habla: text_ops, editor_state, system_clipboard, undo_stack
  … +20 stems
  has_selection: text_ops, helix_dispatch, text_search, editor_state
  anchor: text_ops, editor_state
  before: text_ops, helix_dispatch
zoom ai (stems; no copies ids):
  l2_explore_a  useful a_looks_like_path_anchor dataflow trail  habla: l2_effect_summary, key_binding_registry, l2_feat, l2_problem_frame
  level2_debrief  contains_ci level2_debrief summary build_level2_debrief  habla: build_environment_state, level2_autonomous_loop, level2_session, workspace_session
  l2_effect_summary  seed_overlap is_noise_call_name is_symptom_edge_call catalog_call_noise  habla: l2_explore_a, l2_catalog, ai_controller, key_binding_registry
  l2_catalog  field_has_token heat list_has_token barrio  habla: l2_wave, virtual_text_file
  l2_effect_slice  deps add_node add_siblings case  habla: l2_explore_a, l2_effect_summary, key_binding_registry, l2_explore_a_trail
  action_json  action_json is_action_object ola_only cut
  ai_controller  agent_busy_ settings_ download_busy_ embed_backend_  habla: main_layout, level2_debrief, workspace_config, ai_packages
  ai_packages  ai_package_status l1_info_for_package l2_info_for_package ai_packages  habla: ai_types, model_store, tr
  … +43 stems
  body_lines: l2_explore_a, l2_effect_summary
  body_sem_permille: l2_explore_a, l2_effect_summary
  orphans: l2_explore_a, l2_effect_summary

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=12  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M1  kind=chrome  ov=15  stems: editor panel editor panel
owns: editor panel
nucleus: scroll
peek: editor panel make breadcrumb bar, editor panel MakeEditorPanel
port: M1=>M5 console panel toggle console expanded -call-> ui panel render cache mark dirty
M2  kind=other  ov=9  stems: workspace model
owns: workspace model
gap: state,trigger,effect
peek: workspace model load tabular placeholder
M3  kind=caller  ov=6  stems: visual highlight
owns: visual highlight
gap: state,trigger,effect
peek: visual highlight compute
port: M3=>M5 visual highlight compute -call-> ai controller clear
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
nucleus: text input focus, watch selected, region, input row
peek: watches panel MakeWatchesPanel, watches panel render breakpoint hw footer
port: M6=>M1 editor panel make breadcrumb bar -call-> raw pty screen text
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
bridges: T1[M9,M4] T2[M12,M1] T3[M12,M10,M1] T4[M9,M4]
holes: model store ensure llama cli model store llama bundle dir for workspace model stamp tab disk mtime diagnostics panel MakeDiagnosticsPanel

Trabajos ya hechos:
(ninguno)

Legal ahora: zoom, explorar, ampliar, plan, cerrar
Ya hay zoom. Tú eliges: un explorar (un cazador; puede cubrir todo el ancla) o un plan (partir solo si ves cazas independientes). No partas porque el plano muestre varios barrios. Cada fase trae consulta tuya. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
