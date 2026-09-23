Consulta del usuario (ancla, claim; no la copies entera a un hijo):
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

luz (calibración del piloto; no copies): lsp, app, util
plano (calor del ancla; no copies nombres ni ids):
  ui ●●●  ai_missing_toast ai_path_scope_modal ai_transcript_view binary_symbols_panel bindings_store busy_strip  habla: ai, util, editor, lsp
  ai ●●  action_json ai_packages ai_path_scope ai_trace ai_types coding_stem_embed_index  habla: ui, util, editor, lsp
  util ●●  build_file_highlight child_process_guard clangd_workspace_setup compile_commands_lookup compile_commands_remap compile_commands_setup  habla: ui, ai, editor, lsp
  editor ●  ctx bracket_match clipboard code_snippets cursor_history editor_buffer_source  habla: ui, ai, util, lsp
  lsp ●  diagnostics gfortran_diagnostics language_server_spec lsp_position lsp_sync lsp_text_edits  habla: util, app, indexer, symbols
  terminal ●  app_session feed_pty_bytes_locked master pty_input raw_pty_screen shell_session  habla: ui, util, dap
  app ●  app_mode debug_model editor_tabs language_override workspace_detect workspace_session  habla: ui, ai, util, editor
  indexer ●  index_rules symbol_workspace_indexer workspace_indexer workspace_indexer_rg append_dir_globs append_heavy_dir_globs  habla: util, app, parser, symbols
  parser ●  tree_sitter_ast_utils tree_sitter_completion tree_sitter_highlight tree_sitter_language tree_sitter_service tree_sitter_symbols  habla: util, editor, lsp, terminal
  symbols ●  call_hierarchy code_action completion_snippet hover_info source_symbol_resolver symbol_kind  habla: util, editor, lsp, indexer
  search ●  workspace_search workspace_search_rg workspace_search_runner append_always_skip_dir_globs append_glob_args basename  habla: ai, util, indexer
  git ●  git_command git_diff git_log git_status git_subrepos repo_root  habla: ai, util, i18n
  backend ●  idebug_backend adapter_ dap_backend inferior_stopped_ response session_  habla: ui, util, dap, i18n
  build ●  build_artifact_watcher build_environment build_environment_detector build_environment_selector build_environment_service build_environment_state  habla: ui, util, app
  toolpacks ●  download export_portable install language_packs manifest packaged  habla: util
  core_analyzer ·  output_parser build_obj_search_command heap_block_re parse_hex_address  habla: app
  dap ·  debug_adapter_process debug_adapter_spec gdb_launcher gdb_protocol debug_adapter_kind_for_program fd_  habla: util
  i18n ·  locale strings_en strings_es strings_internal current_locale_setting detect_system_locale
  packet_monitor ·  pkt_connections pkt_monitor_service pkt_preload_path pkt_store af_inet array_count  habla: util
  src ·  main path_is_existing_regular_file path_looks_like_source_or_text program  habla: util, app, toolpacks, i18n
  top ui: diagnostics_panel, editor_panel

zoom lsp (stems; no copies ids):
  lsp_text_edits  lsp_text_edits apply_lsp_text_edits byte_offset_at byte_offset_to_line_byte_col  habla: lsp_position, lsp_uri, path_normalize
  lsp_client  documents_ intentionally_stopping_ is_null doc_it  habla: lsp_text_edits, clang_format_config, compile_commands_remap, lsp_transport
  lsp_transport  lsp_transport stdin_fd_ wait_response running_  habla: gdb_launcher, monitor_log, thread_name
  language_server_spec  location needs_stdio_flag language_id_is_cpp_family language_server_spec  habla: virtual_text_file, bundled_tools
  diagnostics  diagnostics lsp_ui_allowed allowed_paths build_diagnostic_suffix  habla: include_tree, lsp_sync, lsp_uri, symbol_provider
  gfortran_diagnostics  gfortran_diagnostics is_fixed_form_extension gfortran_path parse_gfortran_stderr  habla: virtual_text_file, diagnostics, shell_utils
  lsp_position  lsp_position line_text_at lsp_utf16_column make_lsp_position
  lsp_sync  lsp_sync
  … +2 stems
  contains: lsp_text_edits, lsp_client, lsp_transport
  item: lsp_text_edits, lsp_client, diagnostics
  edit: lsp_text_edits, lsp_client
zoom app (stems; no copies ids):
  application  layout_state_ workspace_ backend_started_ config_  habla: hover_effects, shell_args, source_substitute_modal, context_menu
  debug_model  disabled_breakpoints has_breakpoint debug_model is_breakpoint_enabled  habla: idebug_backend, path_normalize, tr
  app_mode  app_mode
  app_settings  dist clamp_large_file_virtual_mb icon_mode large_file_virtual_bytes  habla: virtual_text_file, glyphs, locale
  editor_tabs  editor_tabs bar_width_chars compute_visible_tab_range format_editor_tab_overflow_button  habla: virtual_text_file, git_diff, editor_state
  language_override  language_override choice language_choices language_display_name  habla: lsp_uri, tr
  recent_projects  recent_projects existing_paths kmaxrecent normalize_workspace_path  habla: virtual_text_file, path_normalize
  workspace_config  config_dir compile_commands_mode_name language_overrides legacy_dir  habla: theme, virtual_text_file, ai_types, build_environment
  … +3 stems
  count: application, debug_model
  static_cast: application, debug_model, workspace_model, editor_tabs
  absolute: application, workspace_model

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=12  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M1  kind=other  ov=3  stems: lsp missing prompt
owns: lsp missing prompt
gap: state,effect
peek: lsp missing prompt catalog, lsp missing prompt lsp missing prompt for status key
peek-edge: lsp missing prompt lsp missing prompt for status key -call-> lsp missing prompt catalog
M2  kind=other  ov=3  stems: lsp symbol provider
owns: lsp symbol provider
gap: state,trigger,effect
peek: lsp symbol provider configure bash language server
M3  kind=caller  ov=0  stems: model store
owns: model store
gap: state,effect
peek: model store has llama server, model store resolve llama server
port: M3=>M6 llama backend ensure completion server -call-> model store resolve llama server
M4  same=M2  kind=other  ov=3  stems: lsp symbol provider
peek: lsp symbol provider join fortran startup thread
M5  kind=latch  ov=3  stems: application application
owns: application
nucleus: pending lsp restart , last lsp environment fingerprint 
peek: application restart lsp for workspace, application schedule debounced lsp restart
port: M5=>M7 application restart lsp for workspace -write-> status message
M6  kind=caller  ov=0  stems: embedding backend llama backend embedding backend llama backend
owns: embedding backend
nucleus: server pid , errno, server stamp , server pid 
peek: embedding backend start server, llama backend start completion server
port: M6=>M3 llama backend ensure completion server -call-> model store resolve llama server
M7  same=M5  kind=caller  ov=3  stems: application
peek: application apply event
M8  kind=caller  ov=0  stems: ai controller ai controller
owns: ai controller
nucleus: settings 
peek: ai controller begin insert at, llama net apply ai runtime env
port: M8=>M7 application run -call-> ai controller begin insert at
M9  kind=hole  ov=0  stems: settings  workspace config app settings model store
owns: settings  / llama net apply ai runtime env
nucleus: settings 
peek: llama net apply ai runtime env, api base
peek-edge: llama net apply ai runtime env -write-> api base
M10  kind=hole  ov=0  stems: l2 effect registry l2 effect registry
owns: l2 effect registry
gap: no state
peek: l2 effect registry list file fns, l2 effect registry read abs
peek-edge: l2 effect registry list file fns -call-> l2 effect registry read abs
M11  same=M8  kind=hole  ov=0  stems: ai controller
peek: l2 effect registry ascii lower copy
M12  kind=hole  ov=0  stems: l2 brain remote l2 brain remote
owns: l2 brain remote
gap: no state
peek: l2 brain remote make l2 brain
bridges: T4[M3,M6]
holes: llama net apply ai runtime env l2 effect registry list file fns l2 effect registry ascii lower copy l2 brain remote make l2 brain

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o dejado de responder y cómo se gestiona su reinicio
keep: M5
leído: transport running, send notification, restart lsp for workspace, schedule debounced lsp restart, is running
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de detección y gestión: la caída se detecta en async worker main cuando job lsp transport running devuelve falso lo que implica que is running es falso, típicamente por fallo en poll o cierre de fd en el reader thread, estableciendo pending transport restart true la gestión del reinicio se orquesta en restart lsp for workspace que se dispara tras la detección vía ensure async worker running - start async worker - async worker main - detección - flag - loop de aplicación que llama a restart lsp for workspace si pending lsp restart es true, aunque el caller exacto del flag a la acción de reinicio en Application no está completamente leído, el circuito ON OFF y el bosquej
Abierto:
- send cancel
- start async worker
- pending transport restart
- pending lsp restart


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: transport running, send notification, restart lsp for workspace, schedule debounced lsp restart
    extra: ensure python lsp async, start async worker
    entre interno: restart lsp for workspace → send notification
hacia el resto:
  T1 → ensure python lsp async
  T1 → start async worker

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor de lenguaje se ha caído o dejado de responder y cómo se gestiona su reinicio
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de detección y gestión: la caída se detecta en async worker main cuando job lsp transport running devuelve falso lo que implica que is running es falso, típicamente por fallo en poll o cierre de fd en el reader thread, estableciendo pending transport restart true la gestión del reinicio se orquesta en restart lsp for workspace que se dispara tras la detección vía ensure async worker running - start async worker - async worker main - detección - flag - loop de aplicación que llama a restart lsp for workspace si pending lsp restart es true, aunque el caller exacto del flag a la acción de reinicio en Application no está completamente leído, el circuito ON OFF y el bosquej

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: zoom, explorar, plan, cerrar
Este es el estado. Tú diriges: atomiza el trabajo (un hijo, un trozo). El plan no es un contrato: tras un Cerrado puedes soltarlo (explorar o plan nuevo). Esta pregunta ya tiene respuesta (no hay objeto). No la relances ni en otro keep. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no refutes el claim entero. 'mismo objeto' en entre abiertas es que la caza no se movió. Si queda un tirón, explorar; si el trabajo no cabe en uno, plan. consulta = prompt de investigación (fenómeno y verbo; el hijo no ve el ancla); why = tu estrategia (el hijo no la lee). Lo que no se caza no se nombra. hacia opcional: conceptos de cerca, no ids. evita opcional. Un explorar es una sola rama; atomiza, no empaquetes dos cazas en un hijo. tú eliges cómo seguir. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
