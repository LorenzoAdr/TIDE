include(FetchContent)

if(NOT CMAKE_C_COMPILE_OBJECT)
  enable_language(C)
endif()

FetchContent_Declare(
  libvterm
  GIT_REPOSITORY https://github.com/neovim/libvterm.git
  GIT_TAG v0.3.3
  GIT_SHALLOW TRUE
)

FetchContent_GetProperties(libvterm)
if(NOT libvterm_POPULATED)
  FetchContent_Populate(libvterm)

  add_library(libvterm STATIC
    ${libvterm_SOURCE_DIR}/src/vterm.c
    ${libvterm_SOURCE_DIR}/src/screen.c
    ${libvterm_SOURCE_DIR}/src/state.c
    ${libvterm_SOURCE_DIR}/src/parser.c
    ${libvterm_SOURCE_DIR}/src/pen.c
    ${libvterm_SOURCE_DIR}/src/keyboard.c
    ${libvterm_SOURCE_DIR}/src/mouse.c
    ${libvterm_SOURCE_DIR}/src/unicode.c
    ${libvterm_SOURCE_DIR}/src/encoding.c
  )

  target_include_directories(libvterm PUBLIC ${libvterm_SOURCE_DIR}/include)
  target_include_directories(libvterm PRIVATE ${libvterm_SOURCE_DIR}/src)
  target_compile_definitions(libvterm PRIVATE _DEFAULT_SOURCE)
endif()
