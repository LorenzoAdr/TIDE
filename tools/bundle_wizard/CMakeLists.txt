cmake_minimum_required(VERSION 3.20)
project(tgdb-bundle-wizard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v6.1.9
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ftxui)

add_executable(tgdb-bundle-wizard main.cpp)
target_link_libraries(tgdb-bundle-wizard PRIVATE ftxui::component ftxui::dom ftxui::screen)
