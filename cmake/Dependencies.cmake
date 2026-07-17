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

FetchContent_Declare(
  tree_sitter_rust
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-rust.git
  GIT_TAG v0.23.3
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  tree_sitter_go
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-go.git
  GIT_TAG v0.23.4
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  tree_sitter_zig
  GIT_REPOSITORY https://github.com/maxxnino/tree-sitter-zig.git
  GIT_TAG a80a6e9be81b33b182ce6305ae4ea28e29211bd5
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  tree_sitter_fortran
  GIT_REPOSITORY https://github.com/stadelmanma/tree-sitter-fortran.git
  GIT_TAG v0.5.1
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  tree_sitter_lua
  GIT_REPOSITORY https://github.com/tree-sitter-grammars/tree-sitter-lua.git
  GIT_TAG v0.5.0
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  tree_sitter_javascript
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-javascript.git
  GIT_TAG v0.23.1
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  tree_sitter_typescript
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-typescript.git
  GIT_TAG v0.23.2
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

foreach(_gram python bash latex rust go zig fortran lua javascript typescript)
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

function(tgdb_ensure_tree_sitter_c_header name function_name source_dir)
  string(TOUPPER "${name}" _name_upper)
  set(_header "${source_dir}/bindings/c/tree-sitter-${name}.h")
  if(EXISTS "${_header}")
    return()
  endif()
  file(MAKE_DIRECTORY "${source_dir}/bindings/c")
  file(WRITE "${_header}"
"#ifndef TREE_SITTER_${_name_upper}_H_
#define TREE_SITTER_${_name_upper}_H_
#ifdef __cplusplus
extern \"C\" {
#endif
typedef struct TSLanguage TSLanguage;
const TSLanguage *${function_name}(void);
#ifdef __cplusplus
}
#endif
#endif
")
endfunction()

tgdb_ensure_tree_sitter_c_header(zig tree_sitter_zig ${tree_sitter_zig_SOURCE_DIR})

foreach(_gram python bash latex rust go zig fortran lua javascript)
  tgdb_add_tree_sitter_grammar(${_gram} ${tree_sitter_${_gram}_SOURCE_DIR})
endforeach()
tgdb_add_tree_sitter_grammar(typescript ${tree_sitter_typescript_SOURCE_DIR}/typescript)

set(TGDB_TREE_SITTER_QUERY_HPP "${CMAKE_BINARY_DIR}/generated/tree_sitter_grammar_queries.gen.hpp")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
file(WRITE "${TGDB_TREE_SITTER_QUERY_HPP}" "#pragma once\n\nnamespace tgdb::tree_sitter_queries {\n\n")
foreach(_query_spec
    "rust|${tree_sitter_rust_SOURCE_DIR}/queries/highlights.scm"
    "go|${tree_sitter_go_SOURCE_DIR}/queries/highlights.scm"
    "zig|${tree_sitter_zig_SOURCE_DIR}/queries/highlights.scm"
    "fortran|${tree_sitter_fortran_SOURCE_DIR}/queries/highlights.scm"
    "lua|${tree_sitter_lua_SOURCE_DIR}/queries/highlights.scm"
    "javascript|${tree_sitter_javascript_SOURCE_DIR}/queries/highlights.scm")
  string(REPLACE "|" ";" _parts "${_query_spec}")
  list(GET _parts 0 _lang)
  list(GET _parts 1 _query_file)
  if(EXISTS "${_query_file}")
    file(READ "${_query_file}" _query_content)
    file(APPEND "${TGDB_TREE_SITTER_QUERY_HPP}"
"inline const char* ${_lang}() {
  return R\"TGDBQ_${_lang}(
${_query_content})TGDBQ_${_lang}\";
}

")
  else()
    file(APPEND "${TGDB_TREE_SITTER_QUERY_HPP}"
"inline const char* ${_lang}() { return \"\"; }

")
  endif()
endforeach()
# TypeScript highlights are incremental on top of JavaScript; combine both.
set(_tgdb_js_hl "${tree_sitter_javascript_SOURCE_DIR}/queries/highlights.scm")
set(_tgdb_ts_hl "${tree_sitter_typescript_SOURCE_DIR}/queries/highlights.scm")
set(_tgdb_ts_combined "")
if(EXISTS "${_tgdb_js_hl}")
  file(READ "${_tgdb_js_hl}" _tgdb_js_hl_content)
  string(APPEND _tgdb_ts_combined "${_tgdb_js_hl_content}\n")
endif()
if(EXISTS "${_tgdb_ts_hl}")
  file(READ "${_tgdb_ts_hl}" _tgdb_ts_hl_content)
  string(APPEND _tgdb_ts_combined "${_tgdb_ts_hl_content}\n")
endif()
file(APPEND "${TGDB_TREE_SITTER_QUERY_HPP}"
"inline const char* typescript() {
  return R\"TGDBQ_typescript(
${_tgdb_ts_combined})TGDBQ_typescript\";
}

")
file(APPEND "${TGDB_TREE_SITTER_QUERY_HPP}" "}  // namespace tgdb::tree_sitter_queries\n")
