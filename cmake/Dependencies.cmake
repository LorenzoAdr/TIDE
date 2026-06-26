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

FetchContent_MakeAvailable(ftxui cppdap)
