Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando abro un proyecto que ya tenía abierto antes quiero que me recuerde exactamente en qué posición estaba el cursor, qué archivos tenía abiertos y si tenía algún panel lateral visible o cerrado

plano (calor del ancla; no copies nombres ni ids):
  ai ·  action_json ai_packages ai_path_scope ai_trace ai_types coding_stem_embed_index  habla: app, build, editor, git
  app ·  app_mode debug_model editor_tabs language_override workspace_detect workspace_session  habla: ai, backend, build, dap
  backend ·  idebug_backend adapter_ dap_backend inferior_stopped_ response session_  habla: dap, i18n, packet_monitor, ui
  build ·  build_artifact_watcher build_environment build_environment_detector build_environment_selector build_environment_service build_environment_state  habla: app, ui, util
  core_analyzer ·  output_parser build_obj_search_command heap_block_re parse_hex_address  habla: app
  dap ·  debug_adapter_process debug_adapter_spec gdb_launcher gdb_protocol debug_adapter_kind_for_program fd_  habla: util
  editor ·  ctx bracket_match clipboard code_snippets cursor_history editor_buffer_source  habla: ai, app, git, i18n
  git ·  git_command git_diff git_log git_status git_subrepos repo_root  habla: ai, i18n, util
  i18n ·  locale strings_en strings_es strings_internal current_locale_setting detect_system_locale
  indexer ·  index_rules symbol_workspace_indexer workspace_indexer workspace_indexer_rg append_dir_globs append_heavy_dir_globs  habla: app, parser, symbols, util
  lsp ·  diagnostics gfortran_diagnostics language_server_spec lsp_position lsp_sync lsp_text_edits  habla: app, dap, i18n, indexer
  packet_monitor ·  pkt_connections pkt_monitor_service pkt_preload_path pkt_store af_inet array_count  habla: util
  parser ·  tree_sitter_ast_utils tree_sitter_completion tree_sitter_highlight tree_sitter_language tree_sitter_service tree_sitter_symbols  habla: editor, lsp, symbols, terminal
  search ·  workspace_search workspace_search_rg workspace_search_runner append_always_skip_dir_globs append_glob_args basename  habla: ai, indexer, util
  src ·  main path_is_existing_regular_file path_looks_like_source_or_text program  habla: app, i18n, toolpacks, util
  symbols ·  call_hierarchy code_action completion_snippet hover_info source_symbol_resolver symbol_kind  habla: editor, indexer, lsp, parser
  terminal ·  app_session feed_pty_bytes_locked master pty_input raw_pty_screen shell_session  habla: dap, ui, util
  toolpacks ·  download export_portable install language_packs manifest packaged  habla: util
  ui ·  ai_missing_toast ai_path_scope_modal ai_transcript_view binary_symbols_panel bindings_store busy_strip  habla: ai, app, backend, core_analyzer
  util ·  build_file_highlight child_process_guard clangd_workspace_setup compile_commands_lookup compile_commands_remap compile_commands_setup  habla: ai, app, build, dap
  top ai: action_json, ai_controller

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=12  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M1  kind=other  ov=6  stems: editor state
owns: editor state
gap: state,trigger,effect
peek: editor state CursorPos
M2  kind=object  ov=0  stems: main layout
owns: main layout
gap: state,trigger,effect
peek: main layout AiController
M3  kind=other  ov=6  stems: app session raw pty screen
owns: app session
gap: state,effect
peek: shell session cursor row
peek-edge: app session cursor row -call-> raw pty screen cursor row
M4  same=M3  kind=other  ov=6  stems: app session
peek: shell session cursor col
M5  kind=object  ov=5  stems: editor panel
owns: editor panel
gap: state,trigger,effect
peek: editor panel end mouse selection
M6  kind=caller  ov=5  stems: panel
owns: panel
gap: state,trigger,effect
peek: panel ModalInputLine
port: M6=>M7 panel ModalInputLine -call-> raw pty screen text
M7  kind=caller  ov=5  stems: file picker file picker
owns: file picker
nucleus: ctrl chord active, ctrl chord armed
peek: file picker MakeFilePickerOverlay, file picker cancel ctrl chord
peek-edge: file picker cancel ctrl chord -write-> ctrl chord active
M8  kind=caller  ov=0  stems: level2 session level2 session
owns: level2 session
nucleus: search
peek: level2 session hunk shape error, level2 session hunk expands over opener
peek-edge: level2 session split mixed sibling hunks -write-> search
M9  kind=hole  ov=0  stems: intent embed attempted  llama backend visual highlight ai trace
owns: intent embed attempted  / ai controller make store progress
nucleus: intent embed attempted 
peek: ai controller make store progress, ai controller begin download
peek-edge: ai controller make store progress -call-> ai controller begin download
M10  kind=hole  ov=0  stems: editor text llama backend level1 action level0 intent index
owns: editor text
gap: no state
peek: level1 agent rank index needle candidates, level1 agent ascii lower simple
peek-edge: level1 agent rank index needle candidates -call-> level1 agent ascii lower simple
M11  kind=hole  ov=5  stems: ai controller level2 autonomous loop level0 intent index compile commands lookup
owns: ai controller
gap: no state
peek: level2 debrief collect from session observations, level2 debrief push unique
peek-edge: level2 debrief collect from session observations -call-> level2 debrief push unique
M12  kind=hole  ov=5  stems: embedding backend l2 effect registry ai missing toast open file confirm
owns: embedding backend
gap: no state
peek: level2 autonomous loop slim ranked map for prompt, ai controller clear
peek-edge: level2 autonomous loop slim ranked map for prompt -call-> ai controller clear
bridges: T3[M9,M12] T4[M9,M12]
holes: ai controller make store progress level1 agent rank index needle candidates level2 debrief collect from session observations level2 autonomous loop slim ranked map for prompt

Trabajos ya hechos:
(ninguno)

Legal ahora: agujas, zoom, explorar, ampliar, plan, cerrar
Aún no hay exploradores. El plano llega frío: primero agujas que cubran las zonas del ancla (entrada, generación, archivo), no idents del prompt. Luego zoom a un barrio o stem. Después TÚ eliges: un explorar (puede cubrir todo el ancla) o un plan (solo si ves cazas independientes). Si ov empatado, amplia 1–3 M*. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
