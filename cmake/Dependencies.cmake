include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v6.1.9
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  cppdap
  GIT_REPOSITORY https://github.com/google/cppdap.git
  GIT_TAG main
  GIT_SHALLOW TRUE
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  tree_sitter
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG master
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  tree_sitter_cpp
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
  GIT_TAG v0.23.4
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  tree_sitter_python
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python.git
  GIT_TAG v0.23.6
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(ftxui cppdap tree_sitter tree_sitter_cpp)

# Manual add: tree-sitter-python's CMakeLists defines a conflicting `ts-test` target
# with tree-sitter-cpp, so we only fetch sources and build the library ourselves.
FetchContent_GetProperties(tree_sitter_python)
if(NOT tree_sitter_python_POPULATED)
  FetchContent_Populate(tree_sitter_python)
endif()

if(NOT TARGET tree-sitter-python)
  set(_tgdb_ts_python_sources
    ${tree_sitter_python_SOURCE_DIR}/src/parser.c
  )
  if(EXISTS ${tree_sitter_python_SOURCE_DIR}/src/scanner.c)
    list(APPEND _tgdb_ts_python_sources ${tree_sitter_python_SOURCE_DIR}/src/scanner.c)
  endif()
  add_library(tree-sitter-python STATIC ${_tgdb_ts_python_sources})
  target_include_directories(tree-sitter-python PUBLIC
    ${tree_sitter_python_SOURCE_DIR}/bindings/c
    ${tree_sitter_python_SOURCE_DIR}/src
  )
  set_target_properties(tree-sitter-python PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
endif()
