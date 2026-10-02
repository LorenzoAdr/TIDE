Consulta del usuario (ancla, claim; no la copies entera a un hijo):
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

luz (calibración del piloto; no copies): lsp, application, util
plano (calor del ancla; no copies nombres ni ids):
  lsp ●●●  diagnostics gfortran_diagnostics language_server_spec lsp_position lsp_sync lsp_text_edits  habla: util, app, symbols, dap
  util ●●●  build_file_highlight child_process_guard clangd_workspace_setup compile_commands_lookup compile_commands_remap compile_commands_setup  habla: lsp, app, ui, ai
  app ●●  app_mode debug_model editor_tabs language_override workspace_detect workspace_session  habla: lsp, util, ui, symbols
  ui ●●  ai_missing_toast ai_path_scope_modal ai_transcript_view binary_symbols_panel bindings_store busy_strip  habla: lsp, util, app, symbols
  symbols ●●  call_hierarchy code_action completion_snippet hover_info source_symbol_resolver symbol_kind  habla: lsp, util, editor, indexer
  ai ●  action_json ai_packages ai_path_scope ai_trace ai_types coding_stem_embed_index  habla: lsp, util, app, ui
  backend ·  idebug_backend adapter_ dap_backend inferior_stopped_ response session_  habla: util, ui, dap, i18n
  build ·  build_artifact_watcher build_environment build_environment_detector build_environment_selector build_environment_service build_environment_state  habla: util, app, ui
  core_analyzer ·  output_parser build_obj_search_command heap_block_re parse_hex_address  habla: app
  dap ·  debug_adapter_process debug_adapter_spec gdb_launcher gdb_protocol debug_adapter_kind_for_program fd_  habla: util
  editor ·  ctx bracket_match clipboard code_snippets cursor_history editor_buffer_source  habla: lsp, util, app, ui
  git ·  git_command git_diff git_log git_status git_subrepos repo_root  habla: util, ai, i18n
  i18n ·  locale strings_en strings_es strings_internal current_locale_setting detect_system_locale
  indexer ·  index_rules symbol_workspace_indexer workspace_indexer workspace_indexer_rg append_dir_globs append_heavy_dir_globs  habla: util, app, symbols, parser
  packet_monitor ·  pkt_connections pkt_monitor_service pkt_preload_path pkt_store af_inet array_count  habla: util
  parser ·  tree_sitter_ast_utils tree_sitter_completion tree_sitter_highlight tree_sitter_language tree_sitter_service tree_sitter_symbols  habla: lsp, util, symbols, editor
  search ·  workspace_search workspace_search_rg workspace_search_runner append_always_skip_dir_globs append_glob_args basename  habla: util, ai, indexer
  src ·  main path_is_existing_regular_file path_looks_like_source_or_text program  habla: util, app, i18n, toolpacks
  terminal ·  app_session feed_pty_bytes_locked master pty_input raw_pty_screen shell_session  habla: util, ui, dap
  toolpacks ·  download export_portable install language_packs manifest packaged  habla: util
  top lsp: lsp_transport, lsp_client

zoom lsp (stems; no copies ids):
  lsp_transport  lsp_transport stdin_fd_ wait_response running_  habla: gdb_launcher, monitor_log, thread_name
  lsp_client  documents_ intentionally_stopping_ is_null doc_it  habla: lsp_transport, diagnostics, language_server_spec, bundled_tools
  language_server_spec  location needs_stdio_flag language_id_is_cpp_family language_server_spec  habla: bundled_tools, virtual_text_file
  diagnostics  diagnostics lsp_ui_allowed allowed_paths build_diagnostic_suffix  habla: include_tree, lsp_sync, lsp_uri, symbol_provider
  gfortran_diagnostics  gfortran_diagnostics is_fixed_form_extension gfortran_path parse_gfortran_stderr  habla: diagnostics, shell_utils, virtual_text_file
  lsp_position  lsp_position line_text_at lsp_utf16_column make_lsp_position
  lsp_sync  lsp_sync
  lsp_text_edits  lsp_text_edits apply_lsp_text_edits byte_offset_at byte_offset_to_line_byte_col  habla: lsp_position, lsp_uri, path_normalize
  … +2 stems
  data: lsp_transport, lsp_client
  deadline: lsp_transport, lsp_client
  is_null: lsp_transport, lsp_client

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
peek: application apply event, application restart lsp for workspace
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
peek: l2 effect registry ascii lower copy, l2 effect registry collect text tokens
M12  kind=hole  ov=0  stems: l2 brain remote l2 brain remote
owns: l2 brain remote
gap: no state
peek: l2 brain remote make l2 brain
bridges: T4[M3,M6]
holes: llama net apply ai runtime env l2 effect registry list file fns l2 effect registry ascii lower copy l2 brain remote make l2 brain

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
keep: M5 M7
leído: start completion server, status text, ensure completion server, health ok
Cerrado:
leído: start completion server, status text, ensure completion server, health ok
Abierto:
- start server
- stop owned unlocked
- stop listener on port


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: start completion server, status text, ensure completion server, health ok
    extra: stop listener on port, stop owned unlocked
hacia el resto:
  T1 → stop listener on port
  T1 → stop owned unlocked

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: start completion server, status text, ensure completion server, health ok

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: zoom, explorar, plan, cerrar
Este es el estado. Tú diriges: atomiza el trabajo (un hijo, un trozo). El plan no es un contrato: tras un Cerrado puedes soltarlo (explorar o plan nuevo). Lo leído no era el disparo. No pases como si afirmara. do=cerrar ese mapeo. 'mismo objeto' en entre abiertas es que la caza no se movió. Si queda un tirón, explorar; si el trabajo no cabe en uno, plan. consulta = prompt de investigación (fenómeno y verbo; el hijo no ve el ancla); why = tu estrategia (el hijo no la lee). Lo que no se caza no se nombra. hacia opcional: conceptos de cerca, no ids. evita opcional. Un explorar es una sola rama; atomiza, no empaquetes dos cazas en un hijo. tú eliges cómo seguir. PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.

Esa pregunta ya se contestó. do=cerrar ese mapeo. PROHIBIDO plan u otro explorar del mismo objeto. Ya se tiró: «dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio». Rechazado: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio
