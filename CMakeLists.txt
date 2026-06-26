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
  src/backend/dap_backend.cpp
  src/dap/gdb_launcher.cpp
  src/dap/gdb_protocol.cpp
  src/ui/main_layout.cpp
  src/ui/file_tree_panel.cpp
  src/ui/source_panel.cpp
  src/ui/watches_panel.cpp
  src/ui/console_panel.cpp
  src/ui/file_picker.cpp
  src/util/cpp_highlight.cpp
  src/util/filesystem_tree.cpp
)

target_include_directories(tgdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(tgdb PRIVATE
  ftxui::component
  ftxui::dom
  ftxui::screen
  cppdap
)

add_executable(hello examples/hello.cpp)
target_compile_options(hello PRIVATE -g -O0)
