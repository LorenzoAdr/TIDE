cmake_minimum_required(VERSION 3.20)
project(tuide VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

function(tuide_link_pthread target)
  if(UNIX)
    target_link_libraries(${target} PRIVATE pthread)
  endif()
endfunction()

if(UNIX)
  find_library(TUIDE_UTIL_LIBRARY util)
endif()


include(cmake/Dependencies.cmake)

set(TUIDE_TREE_SITTER_INCLUDE_DIRS
  ${tree_sitter_SOURCE_DIR}/lib/include
  ${tree_sitter_cpp_SOURCE_DIR}/bindings/c
  ${tree_sitter_python_SOURCE_DIR}/bindings/c
  ${tree_sitter_bash_SOURCE_DIR}/bindings/c
  ${tree_sitter_latex_SOURCE_DIR}/bindings/c
  ${tree_sitter_rust_SOURCE_DIR}/bindings/c
  ${tree_sitter_go_SOURCE_DIR}/bindings/c
  ${tree_sitter_zig_SOURCE_DIR}/bindings/c
  ${tree_sitter_fortran_SOURCE_DIR}/bindings/c
  ${tree_sitter_lua_SOURCE_DIR}/bindings/c
  ${tree_sitter_javascript_SOURCE_DIR}/bindings/c
  ${tree_sitter_typescript_SOURCE_DIR}/bindings/c
  ${tree_sitter_cmake_SOURCE_DIR}/bindings/c
  ${tree_sitter_make_SOURCE_DIR}/bindings/c
  ${CMAKE_BINARY_DIR}/generated
)

set(TUIDE_TREE_SITTER_GRAMMAR_LIBS
  tree-sitter
  tree-sitter-cpp
  tree-sitter-python
  tree-sitter-bash
  tree-sitter-latex
  tree-sitter-rust
  tree-sitter-go
  tree-sitter-zig
  tree-sitter-fortran
  tree-sitter-lua
  tree-sitter-javascript
  tree-sitter-typescript
  tree-sitter-cmake
  tree-sitter-make
)

set(TUIDE_TREE_SITTER_SOURCES
  src/parser/tree_sitter_language.cpp
  src/parser/tree_sitter_document.cpp
  src/parser/tree_sitter_symbols.cpp
  src/parser/tree_sitter_highlight.cpp
  src/parser/tree_sitter_locals.cpp
  src/parser/tree_sitter_blocks.cpp
  src/parser/tree_sitter_completion.cpp
  src/parser/tree_sitter_service.cpp
  src/util/syntax_highlight.cpp
  src/util/syntax_scope.cpp
  src/editor/editor_context.cpp
  src/util/csv_viewer.cpp
  src/ui/theme.cpp
  src/i18n/locale.cpp
  src/i18n/tr.cpp
  src/i18n/strings_es.cpp
  src/i18n/strings_en.cpp
  src/lsp/lsp_uri.cpp
)

function(tuide_link_tree_sitter target)
  target_sources(${target} PRIVATE ${TUIDE_TREE_SITTER_SOURCES})
  target_include_directories(${target} PRIVATE
    ${TUIDE_TREE_SITTER_INCLUDE_DIRS}
    ${cppdap_SOURCE_DIR}/include
    ${cppdap_SOURCE_DIR}/third_party/json/include
  )
  target_link_libraries(${target} PRIVATE
    ${TUIDE_TREE_SITTER_GRAMMAR_LIBS} ftxui::dom)
endfunction()

add_executable(tuide
  src/main.cpp
  src/app/application.cpp
  src/app/app_settings.cpp
  src/i18n/locale.cpp
  src/i18n/tr.cpp
  src/i18n/strings_es.cpp
  src/i18n/strings_en.cpp
  src/app/workspace_detect.cpp
  src/app/workspace_config.cpp
  src/app/workspace_session.cpp
  src/app/debug_model.cpp
  src/app/workspace_model.cpp
  src/app/editor_tabs.cpp
  src/backend/dap_backend.cpp
  src/dap/gdb_launcher.cpp
  src/dap/debug_adapter_spec.cpp
  src/dap/gdb_protocol.cpp
  src/ui/main_layout.cpp
  src/ui/hover_effects.cpp
  src/ui/status_layout_popover.cpp
  src/ui/file_tree_panel.cpp
  src/ui/source_panel.cpp
  src/ui/editor_panel.cpp
  src/ui/editor_grid_node.cpp
  src/ui/editor_tab_bar.cpp
  src/ui/context_menu.cpp
  src/ui/call_hierarchy_panel.cpp
  src/ui/call_hierarchy_view.cpp
  src/ui/diagnostics_panel.cpp
  src/ui/performance_panel.cpp
  src/ui/git_panel.cpp
  src/ui/git_diff_sync.cpp
  src/ui/git_diff_view.cpp
  src/ui/outline_panel.cpp
  src/ui/search_panel.cpp
  src/ui/right_sidebar_panel.cpp
  src/ui/watches_panel.cpp
  src/ui/console_panel.cpp
  src/ui/core_analyzer_panel.cpp
  src/ui/binary_symbols_panel.cpp
  src/core_analyzer/output_parser.cpp
  src/ui/file_picker.cpp
  src/ui/file_picker_match_runner.cpp
  src/ui/file_picker_preview.cpp
  src/ui/file_preview_panel.cpp
  src/ui/symbol_picker.cpp
  src/ui/symbol_picker_match_runner.cpp
  src/ui/quit_confirm.cpp
  src/ui/debug_launch_modal.cpp
  src/ui/shutdown_overlay.cpp
  src/ui/open_file_confirm.cpp
  src/ui/lsp_missing_toast.cpp
  src/ui/shortcuts_modal.cpp
  src/ui/settings_modal.cpp
  src/ui/source_substitute_modal.cpp
  src/ui/theme.cpp
  src/ui/connection_wizard.cpp
  src/ui/workspace_wizard.cpp
  src/ui/welcome_screen.cpp
  src/ui/external_file_wizard.cpp
  src/ui/path_browser.cpp
  src/ui/focus_manager.cpp
  src/ui/key_bindings.cpp
  src/ui/terminal_keyboard.cpp
  src/ui/terminal_display.cpp
  src/ui/focusable_component.cpp
  src/ui/clickable_interaction.cpp
  src/ui/ui_event_dispatcher.cpp
  src/ui/ui_event_trace.cpp
  src/ui/mouse_velocity_tracker.cpp
  src/ui/cursor_blink.cpp
  src/ui/spinner.cpp
  src/ui/glyphs.cpp
  src/indexer/workspace_indexer.cpp
  src/indexer/workspace_indexer_rg.cpp
  src/indexer/symbol_workspace_indexer.cpp
  src/indexer/index_rules.cpp
  src/lsp/lsp_uri.cpp
  src/lsp/lsp_position.cpp
  src/lsp/lsp_transport.cpp
  src/lsp/lsp_text_edits.cpp
  src/lsp/language_server_spec.cpp
  src/lsp/lsp_client.cpp
  src/lsp/diagnostics.cpp
  src/lsp/gfortran_diagnostics.cpp
  src/symbols/lsp_symbol_provider.cpp
  src/symbols/tree_sitter_symbol_provider.cpp
  src/symbols/completion_snippet.cpp
  src/parser/tree_sitter_language.cpp
  src/parser/tree_sitter_document.cpp
  src/parser/tree_sitter_symbols.cpp
  src/parser/tree_sitter_highlight.cpp
  src/parser/tree_sitter_locals.cpp
  src/parser/tree_sitter_blocks.cpp
  src/parser/tree_sitter_completion.cpp
  src/parser/tree_sitter_service.cpp
  src/util/syntax_highlight.cpp
  src/util/syntax_scope.cpp
  src/util/clang_format_config.cpp
  src/util/build_file_highlight.cpp
  src/util/csv_viewer.cpp
  src/util/nm_reader.cpp
  src/util/compiler_location.cpp
  src/util/nm_reader_runner.cpp
  src/util/tabular_file.cpp
  src/util/virtual_text_file.cpp
  src/util/external_viewer.cpp
  src/search/workspace_search.cpp
  src/search/workspace_search_runner.cpp
  src/search/workspace_search_rg.cpp
  src/symbols/source_symbol_resolver.cpp
  src/symbols/symbol_utils.cpp
  src/editor/editor_state.cpp
  src/editor/editor_buffer_source.cpp
  src/editor/text_rope.cpp
  src/editor/cursor_history.cpp
  src/editor/text_ops.cpp
  src/editor/line_comment.cpp
  src/editor/doc_comment.cpp
  src/editor/indent_guides.cpp
  src/editor/text_search.cpp
  src/editor/find_match_runner.cpp
  src/editor/editor_find_state.cpp
  src/editor/undo_stack.cpp
  src/editor/clipboard.cpp
  src/editor/editor_render.cpp
  src/editor/editor_context.cpp
  src/editor/editor_folds.cpp
  src/editor/bracket_match.cpp
  src/editor/visual_highlight.cpp
  src/editor/code_snippets.cpp
  src/editor/helix/helix_state.cpp
  src/editor/helix/helix_key.cpp
  src/editor/helix/helix_keymap.cpp
  src/editor/helix/helix_dispatch.cpp
  src/editor/helix/helix_hints.cpp
  src/editor/helix/helix_scope_nav.cpp
  src/editor/helix/helix_textobjects.cpp
  src/util/crash_handler.cpp
  src/util/child_process_guard.cpp
  src/util/monitor_log.cpp
  src/util/ui_activity_gate.cpp
  src/util/ui_perf_monitor.cpp
  src/util/filesystem_tree.cpp
  src/util/include_tree.cpp
  src/util/path_normalize.cpp
  src/util/fuzzy_match.cpp
  src/util/fuzzy_catalog_filter.cpp
  src/util/file_open_policy.cpp
  src/util/compile_commands_setup.cpp
  src/util/compile_commands_remap.cpp
  src/util/compile_commands_lookup.cpp
  src/util/shell_args.cpp
  src/util/shell_utils.cpp
  src/util/system_clipboard.cpp
  src/util/docker_shell.cpp
  src/util/clangd_workspace_setup.cpp
  src/build/build_environment.cpp
  src/build/build_environment_state.cpp
  src/build/make_qp_parser.cpp
  src/build/build_environment_detector.cpp
  src/build/docker_environment_detector.cpp
  src/build/build_environment_selector.cpp
  src/build/compile_commands_generator.cpp
  src/build/build_environment_service.cpp
  src/build/build_artifact_watcher.cpp
  src/util/bundled_tools.cpp
  src/util/tools_status.cpp
  src/util/lsp_missing_prompt.cpp
  src/util/core_analyzer_support.cpp
  src/util/process_list.cpp
  src/util/system_stats.cpp
  src/packet_monitor/pkt_ring.cpp
  src/packet_monitor/pkt_protocol.cpp
  src/packet_monitor/pkt_connections.cpp
  src/packet_monitor/pkt_store.cpp
  src/packet_monitor/pkt_preload_path.cpp
  src/packet_monitor/pkt_monitor_service.cpp
  src/ui/packet_monitor_panel.cpp
  src/git/git_command.cpp
  src/git/git_status.cpp
  src/git/git_subrepos.cpp
  src/git/git_diff.cpp
  src/git/git_log.cpp
  src/git/git_service.cpp
  src/terminal/shell_session.cpp
  src/terminal/raw_pty_screen.cpp
  src/terminal/pty_input.cpp
)

target_include_directories(tuide PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/packet_preload
  ${TUIDE_TREE_SITTER_INCLUDE_DIRS}
)

target_link_libraries(tuide PRIVATE
  ftxui::component
  ftxui::dom
  ftxui::screen
  cppdap
  ${TUIDE_TREE_SITTER_GRAMMAR_LIBS}
  Threads::Threads
)

# Dev/default paths for Bash DAP (vscode-bash-debug + vendored bashdb_dir).
set(TUIDE_BASH_DEBUG_ROOT "${CMAKE_SOURCE_DIR}/third_party/bash-debug")
target_compile_definitions(tuide PRIVATE
  "TUIDE_BASH_DEBUG_ADAPTER_JS=\"${TUIDE_BASH_DEBUG_ROOT}/out/bashDebug.js\""
  "TUIDE_BASHDB_PATH=\"${TUIDE_BASH_DEBUG_ROOT}/bashdb_dir/bashdb\""
  "TUIDE_BASHDB_LIB=\"${TUIDE_BASH_DEBUG_ROOT}/bashdb_dir\""
)

set(TUIDE_CPPDAP_ARCHIVE "${CMAKE_BINARY_DIR}/_deps/cppdap-build/libcppdap.a")
add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/cppdap_archive_verified.stamp"
  COMMAND ${CMAKE_COMMAND}
    -DARCHIVE=${TUIDE_CPPDAP_ARCHIVE}
    -DEXPECTED=13
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyCppdapArchive.cmake
  COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/cppdap_archive_verified.stamp"
  DEPENDS cppdap
  COMMENT "Verifying libcppdap.a is complete"
  VERBATIM)
add_custom_target(cppdap_archive_verify
  DEPENDS "${CMAKE_BINARY_DIR}/cppdap_archive_verified.stamp")
add_dependencies(tuide cppdap_archive_verify)
tuide_link_pthread(tuide)
if(TUIDE_UTIL_LIBRARY)
  target_link_libraries(tuide PRIVATE ${TUIDE_UTIL_LIBRARY})
endif()

if(UNIX)
  target_link_options(tuide PRIVATE -rdynamic)
endif()

option(TUIDE_STATIC_LIBSTDCXX
       "Link libstdc++ and libgcc statically (reduces runtime libstdc++.so dependency)" OFF)
if(TUIDE_STATIC_LIBSTDCXX)
  target_link_options(tuide PRIVATE -static-libstdc++ -static-libgcc)
endif()

include(cmake/BundleOptions.cmake)
target_compile_definitions(tuide PRIVATE
  "TUIDE_DEFAULT_UI_LOCALE_TAG=\"${TUIDE_DEFAULT_UI_LOCALE}\""
)
if(TUIDE_DEFAULT_HELIX_MODE)
  target_compile_definitions(tuide PRIVATE TUIDE_DEFAULT_HELIX_MODE=1)
endif()
if(TUIDE_BUNDLE_CLANGD)
  include(cmake/BundleClangd.cmake)
endif()
if(TUIDE_BUNDLE_GDB)
  include(cmake/BundleGdb.cmake)
endif()
if(TUIDE_BUNDLE_RG)
  include(cmake/BundleRg.cmake)
endif()
if(TUIDE_BUNDLE_PYTHON_TOOLS)
  include(cmake/BundlePythonTools.cmake)
elseif(TUIDE_BUNDLE_PYTHON_LSP_MIN)
  include(cmake/BundlePythonLspMin.cmake)
endif()
if(TUIDE_BUNDLE_BASH_LS)
  include(cmake/BundleBashLs.cmake)
endif()
if(TUIDE_BUNDLE_TEXLAB)
  include(cmake/BundleTexlab.cmake)
endif()
if(TUIDE_BUNDLE_BASH_DAP)
  include(cmake/BundleBashDap.cmake)
endif()
if(TUIDE_BUNDLE_RUST_ANALYZER)
  include(cmake/BundleRustAnalyzer.cmake)
endif()
if(TUIDE_BUNDLE_GOPLS)
  include(cmake/BundleGopls.cmake)
endif()
if(TUIDE_BUNDLE_ZLS)
  include(cmake/BundleZls.cmake)
endif()
if(TUIDE_BUNDLE_LUA_LS)
  include(cmake/BundleLuaLs.cmake)
endif()
if(TUIDE_BUNDLE_FORTLS)
  include(cmake/BundleFortls.cmake)
endif()
if(TUIDE_BUNDLE_TSSERVER)
  include(cmake/BundleTypescriptLs.cmake)
endif()
if(TUIDE_BUNDLE_NEOCMAKELSP)
  include(cmake/BundleNeocmakelsp.cmake)
endif()
if(TUIDE_BUNDLE_MAKE_LS)
  include(cmake/BundleMakeLs.cmake)
endif()

add_executable(tuide-bundle-wizard tools/bundle_wizard/main.cpp)
target_link_libraries(tuide-bundle-wizard PRIVATE ftxui::component ftxui::dom ftxui::screen)

add_executable(hello examples/hello.cpp)
target_compile_options(hello PRIVATE -g -O0)
target_link_libraries(hello PRIVATE Threads::Threads)
tuide_link_pthread(hello)

add_executable(packet_monitor_demo examples/packet_monitor_demo.cpp)
target_compile_options(packet_monitor_demo PRIVATE -g -O0)
target_link_libraries(packet_monitor_demo PRIVATE Threads::Threads)
tuide_link_pthread(packet_monitor_demo)

add_library(tuide_pkt_preload SHARED tools/packet_preload/pkt_hook.c)
target_include_directories(tuide_pkt_preload PRIVATE tools/packet_preload)
set_target_properties(tuide_pkt_preload PROPERTIES
  OUTPUT_NAME tuide_pkt
  PREFIX "lib"
  POSITION_INDEPENDENT_CODE ON
)
add_custom_command(TARGET tuide_pkt_preload POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:tuide_pkt_preload> ${CMAKE_BINARY_DIR}/lib/
)

add_executable(core_analyzer_demo examples/core_analyzer_demo.cpp)
target_compile_options(core_analyzer_demo PRIVATE -g -O0)

add_executable(i18n_test
  tests/i18n_test.cpp
  src/i18n/locale.cpp
  src/i18n/tr.cpp
  src/i18n/strings_es.cpp
  src/i18n/strings_en.cpp
)
target_include_directories(i18n_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(text_ops_test
  tests/text_ops_test.cpp
  src/editor/editor_state.cpp
  src/editor/editor_buffer_source.cpp
  src/editor/text_rope.cpp
  src/editor/text_ops.cpp
  src/editor/editor_folds.cpp
  src/editor/line_comment.cpp
  src/editor/indent_guides.cpp
  src/editor/text_search.cpp
  src/editor/undo_stack.cpp
  src/editor/clipboard.cpp
  src/editor/bracket_match.cpp
  src/symbols/completion_snippet.cpp
  src/ui/cursor_blink.cpp
  src/util/clang_format_config.cpp
  src/util/shell_utils.cpp
  src/util/system_clipboard.cpp
)
target_include_directories(text_ops_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
tuide_link_tree_sitter(text_ops_test)

add_executable(text_rope_test
  tests/text_rope_test.cpp
  src/editor/text_rope.cpp
)
target_include_directories(text_rope_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(text_buffer_bench
  tests/text_buffer_bench.cpp
  src/editor/editor_state.cpp
  src/editor/editor_buffer_source.cpp
  src/editor/text_rope.cpp
  src/editor/text_ops.cpp
  src/editor/editor_folds.cpp
  src/editor/line_comment.cpp
  src/editor/indent_guides.cpp
  src/editor/text_search.cpp
  src/editor/undo_stack.cpp
  src/editor/clipboard.cpp
  src/editor/bracket_match.cpp
  src/ui/cursor_blink.cpp
  src/util/clang_format_config.cpp
  src/util/shell_utils.cpp
  src/util/system_clipboard.cpp
)
target_include_directories(text_buffer_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
tuide_link_tree_sitter(text_buffer_bench)

add_executable(tree_sitter_test
  tests/tree_sitter_test.cpp
  src/editor/editor_state.cpp
  src/editor/editor_buffer_source.cpp
  src/editor/text_rope.cpp
  src/editor/text_ops.cpp
  src/editor/editor_folds.cpp
  src/editor/line_comment.cpp
  src/editor/indent_guides.cpp
  src/editor/text_search.cpp
  src/editor/undo_stack.cpp
  src/editor/clipboard.cpp
  src/editor/bracket_match.cpp
  src/util/clang_format_config.cpp
  src/util/shell_utils.cpp
  src/util/system_clipboard.cpp
  src/ui/cursor_blink.cpp
)
target_include_directories(tree_sitter_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
tuide_link_tree_sitter(tree_sitter_test)

add_executable(bracket_match_test
  tests/bracket_match_test.cpp
  src/editor/editor_state.cpp
  src/editor/editor_buffer_source.cpp
  src/editor/text_rope.cpp
  src/editor/bracket_match.cpp
  src/editor/indent_guides.cpp
  src/util/clang_format_config.cpp
  src/ui/cursor_blink.cpp
)
target_include_directories(bracket_match_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
tuide_link_tree_sitter(bracket_match_test)

add_executable(indent_guides_test
  tests/indent_guides_test.cpp
  src/editor/indent_guides.cpp
)
target_include_directories(indent_guides_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(build_environment_test
  tests/build_environment_test.cpp
  src/build/build_environment.cpp
  src/build/make_qp_parser.cpp
  src/build/build_environment_selector.cpp
)
target_include_directories(build_environment_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(clangd_workspace_setup_test
  tests/clangd_workspace_setup_test.cpp
  src/util/clangd_workspace_setup.cpp
)
target_include_directories(clangd_workspace_setup_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(clangd_workspace_setup_test PRIVATE ftxui::dom)

add_executable(workspace_detect_test
  tests/workspace_detect_test.cpp
  src/app/workspace_detect.cpp
  src/build/build_environment.cpp
  src/util/path_normalize.cpp
)
target_include_directories(workspace_detect_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(fuzzy_match_test
  tests/fuzzy_match_test.cpp
  src/util/fuzzy_match.cpp
)
target_include_directories(fuzzy_match_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(workspace_indexer_test
  tests/workspace_indexer_test.cpp
  src/indexer/workspace_indexer.cpp
  src/indexer/workspace_indexer_rg.cpp
  src/indexer/index_rules.cpp
  src/util/fuzzy_match.cpp
  src/util/monitor_log.cpp
  src/util/bundled_tools.cpp
  src/util/child_process_guard.cpp
)
target_include_directories(workspace_indexer_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

add_executable(csv_index_test
  tests/csv_index_test.cpp
  src/indexer/index_rules.cpp
  src/util/csv_viewer.cpp
  src/editor/code_snippets.cpp
  src/lsp/lsp_uri.cpp
  src/ui/theme.cpp
  src/i18n/locale.cpp
  src/i18n/tr.cpp
  src/i18n/strings_es.cpp
  src/i18n/strings_en.cpp
)
target_include_directories(csv_index_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(csv_index_test PRIVATE ftxui::dom)

add_executable(external_viewer_test
  tests/external_viewer_test.cpp
  src/util/external_viewer.cpp
  src/util/shell_utils.cpp
  src/util/monitor_log.cpp
  src/i18n/locale.cpp
  src/i18n/tr.cpp
  src/i18n/strings_es.cpp
  src/i18n/strings_en.cpp
)
target_include_directories(external_viewer_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

add_executable(tabular_file_test
  tests/tabular_file_test.cpp
  src/util/tabular_file.cpp
  src/util/csv_viewer.cpp
  src/ui/theme.cpp
  src/i18n/locale.cpp
  src/i18n/tr.cpp
  src/i18n/strings_es.cpp
  src/i18n/strings_en.cpp
)
target_include_directories(tabular_file_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(tabular_file_test PRIVATE ftxui::dom)

add_executable(glyphs_test
  tests/glyphs_test.cpp
  src/ui/glyphs.cpp
)
target_include_directories(glyphs_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(compiler_location_test
  tests/compiler_location_test.cpp
  src/util/compiler_location.cpp
)
target_include_directories(compiler_location_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(raw_pty_screen_test
  tests/raw_pty_screen_test.cpp
  src/terminal/raw_pty_screen.cpp
)
target_include_directories(raw_pty_screen_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(raw_pty_screen_test PRIVATE ftxui::screen)

add_executable(gfortran_diagnostics_test
  tests/gfortran_diagnostics_test.cpp
  src/lsp/gfortran_diagnostics.cpp
  src/util/shell_utils.cpp
)
target_include_directories(gfortran_diagnostics_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(doc_comment_test
  tests/doc_comment_test.cpp
  src/editor/doc_comment.cpp
  src/lsp/lsp_uri.cpp
  src/util/path_normalize.cpp
)
target_include_directories(doc_comment_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(nm_reader_test
  tests/nm_reader_test.cpp
  src/util/nm_reader.cpp
  src/util/shell_utils.cpp
  src/i18n/locale.cpp
  src/i18n/tr.cpp
  src/i18n/strings_es.cpp
  src/i18n/strings_en.cpp
)
target_include_directories(nm_reader_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(git_diff_test
  tests/git_diff_test.cpp
  src/git/git_diff.cpp
)
target_include_directories(git_diff_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(pkt_protocol_test
  tests/pkt_protocol_test.cpp
  src/packet_monitor/pkt_protocol.cpp
)
target_include_directories(pkt_protocol_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_compile_definitions(pkt_protocol_test PRIVATE
  TUIDE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_executable(lsp_completion_probe
  tools/lsp_completion_probe.cpp
  tools/lsp_probe_stubs.cpp
  src/lsp/language_server_spec.cpp
  src/lsp/lsp_client.cpp
  src/lsp/lsp_transport.cpp
  src/lsp/lsp_uri.cpp
  src/lsp/lsp_position.cpp
  src/lsp/lsp_text_edits.cpp
  src/indexer/index_rules.cpp
  src/util/bundled_tools.cpp
  src/util/child_process_guard.cpp
  src/util/monitor_log.cpp
  src/util/path_normalize.cpp
  src/util/clang_format_config.cpp
)
target_include_directories(lsp_completion_probe PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_compile_definitions(lsp_completion_probe PRIVATE
  TUIDE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(lsp_completion_probe PRIVATE ftxui::dom)

set(TUIDE_TEST_TARGETS
  i18n_test
  text_ops_test
  text_buffer_bench
  text_rope_test
  tree_sitter_test
  bracket_match_test
  indent_guides_test
  build_environment_test
  workspace_detect_test
  fuzzy_match_test
  workspace_indexer_test
  csv_index_test
  external_viewer_test
  tabular_file_test
  glyphs_test
  compiler_location_test
  raw_pty_screen_test
  gfortran_diagnostics_test
  doc_comment_test
  nm_reader_test
  git_diff_test
  pkt_protocol_test
  lsp_completion_probe
)
foreach(_tuide_test_target IN LISTS TUIDE_TEST_TARGETS)
  tuide_link_pthread(${_tuide_test_target})
endforeach()