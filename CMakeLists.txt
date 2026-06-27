cmake_minimum_required(VERSION 3.20)
project(tgdb VERSION 0.1.0 LANGUAGES CXX)

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
  src/ui/watches_panel.cpp
  src/ui/console_panel.cpp
  src/ui/file_picker.cpp
  src/ui/connection_wizard.cpp
  src/ui/workspace_wizard.cpp
  src/ui/path_browser.cpp
  src/ui/focus_manager.cpp
  src/ui/focusable_component.cpp
  src/indexer/trie.cpp
  src/indexer/workspace_indexer.cpp
  src/symbols/regex_symbol_provider.cpp
  src/util/cpp_highlight.cpp
  src/util/crash_handler.cpp
  src/util/filesystem_tree.cpp
  src/util/process_list.cpp
)

target_include_directories(tgdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(tgdb PRIVATE
  ftxui::component
  ftxui::dom
  ftxui::screen
  cppdap
)

if(UNIX)
  target_link_options(tgdb PRIVATE -rdynamic)
endif()

add_executable(hello examples/hello.cpp)
target_compile_options(hello PRIVATE -g -O0)
