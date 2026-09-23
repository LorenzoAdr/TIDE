Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

luz (calibración del piloto; no copies): ai, ui, editor, app
plano (calor del ancla; no copies nombres ni ids):
  ui ●●●  ai_missing_toast ai_path_scope_modal ai_transcript_view binary_symbols_panel bindings_store busy_strip  habla: ai, util, editor, app
  ai ●●  action_json ai_packages ai_path_scope ai_trace ai_types coding_stem_embed_index  habla: ui, util, editor, app
  util ●  build_file_highlight child_process_guard clangd_workspace_setup compile_commands_lookup compile_commands_remap compile_commands_setup  habla: ui, ai, editor, app
  editor ●  ctx bracket_match clipboard code_snippets cursor_history editor_buffer_source  habla: ui, ai, util, app
  app ●  app_mode debug_model editor_tabs language_override workspace_detect workspace_session  habla: ui, ai, util, editor
  terminal ●  app_session feed_pty_bytes_locked master pty_input raw_pty_screen shell_session  habla: ui, util, dap
  indexer ●  index_rules symbol_workspace_indexer workspace_indexer workspace_indexer_rg append_dir_globs append_heavy_dir_globs  habla: util, app, parser, symbols
  lsp ●  diagnostics gfortran_diagnostics language_server_spec lsp_position lsp_sync lsp_text_edits  habla: util, app, indexer, symbols
  parser ●  tree_sitter_ast_utils tree_sitter_completion tree_sitter_highlight tree_sitter_language tree_sitter_service tree_sitter_symbols  habla: util, editor, terminal, lsp
  search ●  workspace_search workspace_search_rg workspace_search_runner append_always_skip_dir_globs append_glob_args basename  habla: ai, util, indexer
  git ●  git_command git_diff git_log git_status git_subrepos repo_root  habla: ai, util, i18n
  symbols ●  call_hierarchy code_action completion_snippet hover_info source_symbol_resolver symbol_kind  habla: util, editor, indexer, lsp
  backend ●  idebug_backend adapter_ dap_backend inferior_stopped_ response session_  habla: ui, util, i18n, dap
  build ●  build_artifact_watcher build_environment build_environment_detector build_environment_selector build_environment_service build_environment_state  habla: ui, util, app
  i18n ●  locale strings_en strings_es strings_internal current_locale_setting detect_system_locale
  toolpacks ●  download export_portable install language_packs manifest packaged  habla: util
  core_analyzer ·  output_parser build_obj_search_command heap_block_re parse_hex_address  habla: app
  dap ·  debug_adapter_process debug_adapter_spec gdb_launcher gdb_protocol debug_adapter_kind_for_program fd_  habla: util
  packet_monitor ·  pkt_connections pkt_monitor_service pkt_preload_path pkt_store af_inet array_count  habla: util
  src ·  main path_is_existing_regular_file path_looks_like_source_or_text program  habla: util, app, i18n, toolpacks
  top ui: editor_panel, ui_invalidation_policy

zoom ai (stems; no copies ids):
  level2_session  ast append_observation gaps rejected_targets  habla: search_replace, virtual_text_file, l2_effect_registry, l2_effect_summary
  l2_think  high medium off apply_think_for_request  habla: l2_brain, llama_backend
  ai_controller  agent_busy_ settings_ download_busy_ embed_backend_  habla: level2_session, coding_embed_rerank, edit_journal, git_command
  l2_wave  candidatas cerrar encurso wave_control_induce_miss_answer  habla: l2_cerca_wake, action_json, get_code_of, key_binding_registry
  coding_embed_rerank  embed_text test_embed file_count stem_index  habla: coding_stem_embed_index, repo_map, search_needles, embedding_backend
  edit_journal  edit_journal apply_demo apply_replace author  habla: lsp_text_edits, text_ops, virtual_text_file, workspace_model
  llama_net  apply_ai_runtime_env getaddrinfo llama_host_is_local llama_net  habla: ai_types
  search_replace  hunk find_unique_span looks_like_aider_path search_replace  habla: virtual_text_file
  … +43 stems
  find: level2_session, l2_wave, coding_embed_rerank, ai_controller
  npos: level2_session, l2_wave, coding_embed_rerank, ai_controller
  line: level2_session, l2_wave, ai_controller, coding_embed_rerank
zoom ui (stems; no copies ids):
  editor_panel  code_box all_items lsp_inflight_key lsp_pending_key  habla: hover_effects, edit_journal, editor_grid_node, context_menu
  ui_invalidation_policy  ui_invalidation_policy appmodechanged editorviewonly filetreestructure  habla: main_layout, ui_wake, ui_event_types, ui_panel_render_cache
  hover_effects  hover_effects animations_enabled apply_hover_repaint editor_scope_effects_allowed  habla: main_layout, ui_wake, press_ids, ui_panel_render_cache
  settings_modal  has_workspace draft_compile_commands development_options_enabled draft_ui_colors_preset  habla: ai_path_scope_modal, ai_controller, llama_net, path_browser
  diagnostics_panel  diagnostics_panel a_active apply_fix_for_row b_active  habla: hover_effects, context_menu, debug_model, main_layout
  ai_path_scope_modal  draft_paths ai_path_scope_modal path_already_listed path_browser  habla: path_browser, main_layout, raw_pty_screen, theme
  binary_symbols_panel  binary_symbols_panel filter_runner loading binding_filter  habla: hover_effects, debug_model, main_layout, raw_pty_screen
  editor_grid_node  editor_grid_node element grid pixel  habla: ui_perf_monitor
  … +76 stems
  Character: editor_panel, settings_modal, ai_path_scope_modal, diagnostics_panel
  state: editor_panel, settings_modal, ai_path_scope_modal, diagnostics_panel
  Contain: editor_panel, settings_modal, diagnostics_panel, binary_symbols_panel

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=12  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M1  kind=other  ov=0  stems: search replace
owns: search replace
gap: state,trigger,effect
peek: search replace find unique span
M2  kind=caller  ov=6  stems: ai trace
owns: ai trace
gap: state,trigger,effect
peek: ai trace ai trace escape
port: M2=>M3 level2 session json escape -call-> ai trace ai trace escape
M3  kind=caller  ov=6  stems: level2 session level2 session
owns: level2 session
nucleus: phase, err out
peek: level2 session reopen for followup, level2 session apply tool
port: M3=>M8 ai controller clear ai session -call-> ai controller clear
M4  kind=caller  ov=6  stems: file picker file picker
owns: file picker
nucleus: ctrl chord active
peek: file picker MakeFilePickerOverlay, file picker cancel ctrl chord
port: M4=>M6 application open quick file picker -call-> ai controller clear
M5  kind=caller  ov=0  stems: console panel console panel
owns: console panel
nucleus: b row
peek: console panel copy terminal selection, console panel terminal selected text
port: M5=>M6 console panel handle ai console keys -call-> ai controller clear
M6  kind=caller  ov=0  stems: level1 agent level1 agent
owns: level1 agent
nucleus: ctx
peek: level1 agent filter distilled ignore, level1 agent run
port: M6=>M8 ai controller cancel level1 -call-> workspace config load
M7  kind=chrome  ov=4  stems: editor panel editor panel
owns: editor panel
nucleus: scroll
peek: editor panel MakeEditorPanel, editor panel handle breadcrumb click
port: M7=>M6 level1 agent run -call-> editor text clear
M8  kind=latch  ov=6  stems: ai controller ai controller
owns: ai controller
nucleus: pending insert 
peek: ai controller begin insert at, ai controller cancel level1
port: M8=>M6 ai controller cancel level1 -call-> workspace config load
M9  kind=hole  ov=6  stems: app session shell session raw pty screen app session
owns: app session
gap: no state
peek: raw pty screen skip escape, raw pty screen handle csi
peek-edge: raw pty screen skip escape -call-> raw pty screen handle csi
M10  kind=hole  ov=6  stems: gdb launcher thread safe queue git service gdb launcher
owns: gdb launcher
gap: no state
peek: git service unbind remote cancel, thread safe queue reset
peek-edge: git service unbind remote cancel -call-> thread safe queue reset
M11  kind=hole  ov=6  stems: git service git service
owns: git service
gap: no state
peek: git service cancel remote, cancel
peek-edge: git service cancel remote -write-> cancel
M12  kind=hole  ov=6  stems: git command git command
owns: git command
gap: no state
peek: git command request git cancel
bridges: T1[M6,M8] T2[M6,M3] T5[M6,M8]
holes: raw pty screen skip escape git service unbind remote cancel git service cancel remote git command request git cancel

Plan de búsqueda (romper):
1. [fallo] A locator: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
2. [fallo] B locator: dónde se limpia el archivo para evitar que quede a medias al cancelar
3. [pendiente] P puente: hay camino capturar la cancelación y limpiar el archivo (need A B)

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M3
leído: cancel level1, ai trace escape, clear hover if
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para cancelar la IA; lo leído son utilidades de escape de strings ai trace escape y gestión de hover UI clear hover if, sin invocación a cancel level1 ni lógica de cancelación
Abierto:
- handle problems scrollbar mouse
- cancel all
- clear hover state

### Trabajo 2
consulta: dónde se limpia el archivo para evitar que quede a medias al cancelar
keep: M6
leído: cancel current, clear pending insert, cancel all, cancel level1
Cerrado:
leído: cancel current, clear pending insert, cancel all, cancel level1
Abierto:
- handle user input
- handle route


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1, ai trace escape, clear hover if
    extra: cancel all, Ai Controller

T2
    visto: cancel current, clear pending insert, cancel all, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel level1
  T1=>T2  cancel current → ai trace escape
hacia el resto:
  T1 → Ai Controller
  T1 → handle problems scrollbar mouse
  T2 → handle user input
  T2 → handle route

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape ni clic fuera para cancelar la IA; lo leído son utilidades de escape de strings ai trace escape y gestión de hover UI clear hover if, sin invocación a cancel level1 ni lógica de cancelación

T2 preguntó:
dónde se limpia el archivo para evitar que quede a medias al cancelar
Cerrado de T2:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
leído: cancel current, clear pending insert, cancel all, cancel level1

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: explorar, plan, cerrar
El plan no es un contrato. Tras un Cerrado puedes soltarlo: explorar un port del pack, o un plan nuevo.
Lee Cerrado. Cierra si el claim ya se confirma o se refuta, o suelta el plan y sigue un port.
JSON ahora. Primer carácter `{`.

Esa pregunta ya se contestó. do=cerrar ese mapeo. PROHIBIDO plan u otro explorar del mismo objeto. Ya se tiró: «dónde se limpia el archivo para evitar que quede a medias al cancelar». Rechazado: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
