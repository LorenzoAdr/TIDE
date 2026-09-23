Consulta del usuario (ancla, claim; no la copies entera a un hijo):
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

luz (calibración del piloto; no copies): ai, editor, ui, app
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

Trabajos ya hechos:
### Trabajo 1
consulta: dónde se captura Escape o clic fuera para cancelar la generación de la IA
keep: M8
leído: ai trace escape, handle route, handle user input, is cancel input, cancel current
Cerrado:
encontré el objeto de la consulta: la captura de Escape clic fuera para cancelar la generación de IA está en handle user input que delega en is cancel input detecta cancel cancel cancelar y llama a cancel current que invoca cancel all si hay trabajo activo . No hay evidencia de captura de clic fuera en el código leído; el mecanismo es puramente textual teclado.
Abierto:
- cancel all

### Trabajo 2
consulta: qué hace cancel all para limpiar el archivo a medias y detener la generación
keep: M8
leído: cancel current, cancel level1, cancel all, clear pending insert
Cerrado:
encontré el mecanismo: cancel all limpia el archivo a medias invocando clear pending insert que resetea pending insert e insert anchor y detiene la generación llamando a cancel level1 que setea agent cancel y tasks cancel
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: dónde se limpia el buffer del editor para deshacer los cambios parciales de la IA
keep: M8
leído: clear pending insert, cancel current, handle user input
Cerrado:
encontré el mecanismo de limpieza del buffer de inserción pendiente: clear pending insert resetea pending insert y insert anchor, y es invocado directamente desde handle user input cuando el input es un comando de cancelación o cuando se consume el input para ejecutar la inserción y desde cancel current
Abierto:
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, handle route, handle user input, is cancel input
    extra: handle level2 harness, background

T2
    visto: cancel current, cancel level1, cancel all, clear pending insert
    extra: handle user input, handle route

T3
    visto: clear pending insert, cancel current, handle user input
    extra: handle route, cancel all

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  cancel level1 → ai trace escape
  T1=>T3  mismo objeto: handle user input
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T2=>T3  mismo objeto: cancel current
  T2=>T3  handle user input → handle route → cancel current → cancel all → cancel level1
hacia el resto:
  T1 → handle level2 harness
  T1 → background
  T2 → Ai Controller
  T2 → Symbol Filter Runner
  T3 → run insert async
  T3 → deshabilitado

# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se captura Escape o clic fuera para cancelar la generación de la IA
Cerrado de T1:
encontré el objeto de la consulta: la captura de Escape clic fuera para cancelar la generación de IA está en handle user input que delega en is cancel input detecta cancel cancel cancelar y llama a cancel current que invoca cancel all si hay trabajo activo . No hay evidencia de captura de clic fuera en el código leído; el mecanismo es puramente textual teclado.

T2 preguntó:
qué hace cancel all para limpiar el archivo a medias y detener la generación
Cerrado de T2:
encontré el mecanismo: cancel all limpia el archivo a medias invocando clear pending insert que resetea pending insert e insert anchor y detiene la generación llamando a cancel level1 que setea agent cancel y tasks cancel

T3 preguntó:
dónde se limpia el buffer del editor para deshacer los cambios parciales de la IA
Cerrado de T3:
encontré el mecanismo de limpieza del buffer de inserción pendiente: clear pending insert resetea pending insert y insert anchor, y es invocado directamente desde handle user input cuando el input es un comando de cancelación o cuando se consume el input para ejecutar la inserción y desde cancel current

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: plan, cerrar
explorar agotado. Si falta un eslabón, plan (cada fase trae consulta tuya) o cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
