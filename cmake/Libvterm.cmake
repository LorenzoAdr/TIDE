set(LIBVTERM_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../third_party/libvterm)

if(NOT EXISTS ${LIBVTERM_SOURCE_DIR}/include/vterm.h)
  message(FATAL_ERROR "libvterm not found at ${LIBVTERM_SOURCE_DIR}")
endif()

add_library(libvterm STATIC
  ${LIBVTERM_SOURCE_DIR}/src/vterm.c
  ${LIBVTERM_SOURCE_DIR}/src/screen.c
  ${LIBVTERM_SOURCE_DIR}/src/state.c
  ${LIBVTERM_SOURCE_DIR}/src/parser.c
  ${LIBVTERM_SOURCE_DIR}/src/pen.c
  ${LIBVTERM_SOURCE_DIR}/src/keyboard.c
  ${LIBVTERM_SOURCE_DIR}/src/mouse.c
  ${LIBVTERM_SOURCE_DIR}/src/unicode.c
  ${LIBVTERM_SOURCE_DIR}/src/encoding.c
)

target_include_directories(libvterm PUBLIC ${LIBVTERM_SOURCE_DIR}/include)
target_include_directories(libvterm PRIVATE ${LIBVTERM_SOURCE_DIR}/src)
target_compile_definitions(libvterm PRIVATE _DEFAULT_SOURCE)
target_compile_options(libvterm PRIVATE -std=gnu99)
