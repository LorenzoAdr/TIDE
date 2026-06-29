option(TGDB_BUNDLE_CLANGD "Embed official clangd Linux x86_64 release" OFF)
option(TGDB_FORCE_BUNDLED_CLANGD
       "At runtime, never fall back to clangd on PATH (requires TGDB_BUNDLE_CLANGD)" OFF)

option(TGDB_BUNDLE_GDB "Embed gdb-static Full Linux x86_64 release" OFF)
option(TGDB_FORCE_BUNDLED_GDB
       "At runtime, never fall back to gdb on PATH (requires TGDB_BUNDLE_GDB)" OFF)

set(TGDB_CLANGD_VERSION "19.1.2" CACHE STRING "clangd release version to bundle")
set(TGDB_GDB_VERSION "v16.3-static" CACHE STRING "gdb-static release tag to bundle")

if(TGDB_FORCE_BUNDLED_CLANGD AND NOT TGDB_BUNDLE_CLANGD)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_CLANGD requires TGDB_BUNDLE_CLANGD=ON")
endif()

if(TGDB_FORCE_BUNDLED_GDB AND NOT TGDB_BUNDLE_GDB)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_GDB requires TGDB_BUNDLE_GDB=ON")
endif()

if(TGDB_BUNDLE_CLANGD)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "TGDB_BUNDLE_CLANGD is only supported on Linux")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "TGDB_BUNDLE_CLANGD is only supported on x86_64")
  endif()
endif()

if(TGDB_BUNDLE_GDB)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "TGDB_BUNDLE_GDB is only supported on Linux")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "TGDB_BUNDLE_GDB is only supported on x86_64")
  endif()
endif()
