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

FetchContent_Declare(
  tree_sitter_bash
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-bash.git
  GIT_TAG v0.25.0
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  tree_sitter_latex
  GIT_REPOSITORY https://github.com/latex-lsp/tree-sitter-latex.git
  GIT_TAG v0.6.0
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(ftxui cppdap tree_sitter tree_sitter_cpp)

# Manual add: several grammar CMakeLists define a conflicting `ts-test` target
# with tree-sitter-cpp, so we only fetch sources and build the libraries ourselves.
function(tgdb_add_tree_sitter_grammar name source_dir)
  if(TARGET tree-sitter-${name})
    return()
  endif()
  set(_sources ${source_dir}/src/parser.c)
  if(EXISTS ${source_dir}/src/scanner.c)
    list(APPEND _sources ${source_dir}/src/scanner.c)
  elseif(EXISTS ${source_dir}/src/scanner.cc)
    list(APPEND _sources ${source_dir}/src/scanner.cc)
  endif()
  add_library(tree-sitter-${name} STATIC ${_sources})
  target_include_directories(tree-sitter-${name} PUBLIC
    ${source_dir}/bindings/c
    ${source_dir}/src
  )
  set_target_properties(tree-sitter-${name} PROPERTIES
    C_STANDARD 11
    CXX_STANDARD 17
    POSITION_INDEPENDENT_CODE ON)
endfunction()

foreach(_gram python bash latex)
  FetchContent_GetProperties(tree_sitter_${_gram})
  if(NOT tree_sitter_${_gram}_POPULATED)
    FetchContent_Populate(tree_sitter_${_gram})
  endif()
endforeach()

# tree-sitter-latex does not ship generated parser.c; generate at configure time.
if(NOT EXISTS "${tree_sitter_latex_SOURCE_DIR}/src/parser.c")
  find_program(TGDB_NPX npx)
  if(NOT TGDB_NPX)
    message(FATAL_ERROR "tree-sitter-latex needs 'npx' to generate parser.c (npm)")
  endif()
  message(STATUS "Generating tree-sitter-latex parser.c...")
  execute_process(
    COMMAND "${TGDB_NPX}" --yes tree-sitter-cli@0.25.8 generate
    WORKING_DIRECTORY "${tree_sitter_latex_SOURCE_DIR}"
    RESULT_VARIABLE _tgdb_latex_gen_rc
    OUTPUT_VARIABLE _tgdb_latex_gen_out
    ERROR_VARIABLE _tgdb_latex_gen_err)
  if(NOT _tgdb_latex_gen_rc EQUAL 0 OR NOT EXISTS "${tree_sitter_latex_SOURCE_DIR}/src/parser.c")
    message(FATAL_ERROR "Failed to generate tree-sitter-latex parser.c:\n${_tgdb_latex_gen_out}\n${_tgdb_latex_gen_err}")
  endif()
endif()

# Ensure a C bindings header exists for latex (upstream only ships Swift).
if(NOT EXISTS "${tree_sitter_latex_SOURCE_DIR}/bindings/c/tree-sitter-latex.h")
  file(MAKE_DIRECTORY "${tree_sitter_latex_SOURCE_DIR}/bindings/c")
  file(WRITE "${tree_sitter_latex_SOURCE_DIR}/bindings/c/tree-sitter-latex.h"
"#ifndef TREE_SITTER_LATEX_H_
#define TREE_SITTER_LATEX_H_
#ifdef __cplusplus
extern \"C\" {
#endif
typedef struct TSLanguage TSLanguage;
const TSLanguage *tree_sitter_latex(void);
#ifdef __cplusplus
}
#endif
#endif
")
endif()

foreach(_gram python bash latex)
  tgdb_add_tree_sitter_grammar(${_gram} ${tree_sitter_${_gram}_SOURCE_DIR})
endforeach()
