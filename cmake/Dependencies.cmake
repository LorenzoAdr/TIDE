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

FetchContent_MakeAvailable(ftxui cppdap tree_sitter tree_sitter_cpp)
