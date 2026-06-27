cmake_minimum_required(VERSION 3.20)
project(tgdb VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Dependencies.cmake)

add_executable(tgdb
  src/main.cpp
  src/app/application.cpp
  src/app/debug_model.cpp
  src/app/workspace_model.cpp
  src/backend/dap_backend.cpp
  src/dap/gdb_launcher.cpp
  src/dap/gdb_protocol.cpp
  src/ui/main_layout.cpp
  src/ui/file_tree_panel.cpp
  src/ui/source_panel.cpp
  src/ui/editor_panel.cpp
  src/ui/outline_panel.cpp
  src/ui/search_panel.cpp
  src/ui/right_sidebar_panel.cpp
  src/ui/watches_panel.cpp
  src/ui/console_panel.cpp
  src/ui/file_picker.cpp
  src/ui/symbol_picker.cpp
  src/ui/quit_confirm.cpp
  src/ui/shortcuts_modal.cpp
  src/ui/connection_wizard.cpp
  src/ui/workspace_wizard.cpp
  src/ui/path_browser.cpp
  src/ui/focus_manager.cpp
  src/ui/key_bindings.cpp
  src/ui/terminal_keyboard.cpp
  src/ui/focusable_component.cpp
  src/indexer/workspace_indexer.cpp
  src/indexer/symbol_workspace_indexer.cpp
  src/indexer/index_rules.cpp
  src/indexer/workspace_watcher.cpp
  src/lsp/lsp_uri.cpp
  src/lsp/lsp_transport.cpp
  src/lsp/lsp_client.cpp
  src/symbols/lsp_symbol_provider.cpp
  src/search/workspace_search.cpp
  src/symbols/regex_symbol_provider.cpp
  src/symbols/symbol_utils.cpp
  src/editor/editor_state.cpp
  src/editor/text_ops.cpp
  src/editor/text_search.cpp
  src/editor/editor_find_state.cpp
  src/editor/undo_stack.cpp
  src/editor/clipboard.cpp
  src/editor/editor_render.cpp
  src/util/cpp_highlight.cpp
  src/util/crash_handler.cpp
  src/util/filesystem_tree.cpp
  src/util/path_normalize.cpp
  src/util/process_list.cpp
  src/terminal/shell_session.cpp
  src/terminal/terminal_emulator.cpp
  src/terminal/pty_input.cpp
)

target_include_directories(tgdb PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(tgdb PRIVATE
  ftxui::component
  ftxui::dom
  ftxui::screen
  cppdap
  libvterm
)

if(UNIX)
  target_link_options(tgdb PRIVATE -rdynamic)
endif()

add_executable(hello examples/hello.cpp)
target_compile_options(hello PRIVATE -g -O0)

add_executable(text_ops_test
  tests/text_ops_test.cpp
  src/editor/editor_state.cpp
  src/editor/text_ops.cpp
  src/editor/text_search.cpp
  src/editor/undo_stack.cpp
  src/editor/clipboard.cpp
)
target_include_directories(text_ops_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
