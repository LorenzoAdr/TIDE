option(TGDB_BUNDLE_CLANGD "Embed official clangd Linux x86_64 release" OFF)
option(TGDB_FORCE_BUNDLED_CLANGD
       "At runtime, never fall back to clangd on PATH (requires TGDB_BUNDLE_CLANGD)" OFF)

option(TGDB_BUNDLE_GDB "Embed gdb Linux x86_64 build" OFF)
set(TGDB_GDB_BUNDLE_KIND "static" CACHE STRING "Bundled gdb kind: static|core_analyzer")
set_property(CACHE TGDB_GDB_BUNDLE_KIND PROPERTY STRINGS static core_analyzer)
option(TGDB_BUILD_GDB_CA "Build gdb+Core Analyzer during bundle step (slow)" OFF)
option(TGDB_FORCE_BUNDLED_GDB
       "At runtime, never fall back to gdb on PATH (requires TGDB_BUNDLE_GDB)" OFF)

option(TGDB_BUNDLE_RG "Embed official ripgrep Linux x86_64 release" ON)
option(TGDB_FORCE_BUNDLED_RG
       "At runtime, never fall back to rg on PATH (requires TGDB_BUNDLE_RG)" OFF)

set(TGDB_CLANGD_VERSION "19.1.2" CACHE STRING "clangd release version to bundle")
set(TGDB_GDB_STATIC_VERSION "v16.3-static" CACHE STRING "gdb-static release tag")
set(TGDB_GDB_CA_VERSION "16.3-ca" CACHE STRING "gdb+core_analyzer bundle version tag")
set(TGDB_RG_VERSION "15.1.0" CACHE STRING "ripgrep release version to bundle")

if(TGDB_FORCE_BUNDLED_CLANGD AND NOT TGDB_BUNDLE_CLANGD)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_CLANGD requires TGDB_BUNDLE_CLANGD=ON")
endif()

if(TGDB_FORCE_BUNDLED_GDB AND NOT TGDB_BUNDLE_GDB)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_GDB requires TGDB_BUNDLE_GDB=ON")
endif()

if(TGDB_BUNDLE_GDB)
  if(NOT TGDB_GDB_BUNDLE_KIND MATCHES "^(static|core_analyzer)$")
    message(FATAL_ERROR "TGDB_GDB_BUNDLE_KIND must be 'static' or 'core_analyzer'")
  endif()
endif()

if(TGDB_FORCE_BUNDLED_RG AND NOT TGDB_BUNDLE_RG)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_RG requires TGDB_BUNDLE_RG=ON")
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

if(TGDB_BUNDLE_RG)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "TGDB_BUNDLE_RG is only supported on Linux")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "TGDB_BUNDLE_RG is only supported on x86_64")
  endif()
endif()
