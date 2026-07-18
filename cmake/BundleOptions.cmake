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

# Python tooling: A = basedpyright only (host Python); B = CPython + basedpyright + debugpy.
# B supersedes A when both are requested.
option(TGDB_BUNDLE_PYTHON_LSP_MIN
       "Embed basedpyright site-packages (needs host Python at runtime)" OFF)
option(TGDB_BUNDLE_PYTHON_TOOLS
       "Embed portable CPython + basedpyright + debugpy" OFF)
option(TGDB_FORCE_BUNDLED_PYTHON_TOOLS
       "At runtime, prefer bundled Python tools (requires A or B)" OFF)

option(TGDB_BUNDLE_BASH_LS "Embed bash-language-server + Node" OFF)
option(TGDB_FORCE_BUNDLED_BASH_LS
       "Prefer embedded bash-language-server (requires TGDB_BUNDLE_BASH_LS)" OFF)
option(TGDB_BUNDLE_TEXLAB "Embed TexLab + ChkTeX Linux x86_64 release" OFF)
option(TGDB_FORCE_BUNDLED_TEXLAB
       "Prefer embedded TexLab (requires TGDB_BUNDLE_TEXLAB)" OFF)
option(TGDB_BUNDLE_BASH_DAP "Embed Bash DAP adapter + bashdb (+ Node unless BASH_LS)" OFF)
option(TGDB_FORCE_BUNDLED_BASH_DAP
       "Prefer embedded Bash DAP (requires TGDB_BUNDLE_BASH_DAP)" OFF)

option(TGDB_BUNDLE_RUST_ANALYZER "Embed rust-analyzer Linux x86_64 release" OFF)
option(TGDB_FORCE_BUNDLED_RUST_ANALYZER
       "Prefer embedded rust-analyzer (requires TGDB_BUNDLE_RUST_ANALYZER)" OFF)
option(TGDB_BUNDLE_GOPLS "Embed gopls Linux x86_64 (go install at bundle time)" OFF)
option(TGDB_FORCE_BUNDLED_GOPLS
       "Prefer embedded gopls (requires TGDB_BUNDLE_GOPLS)" OFF)
option(TGDB_BUNDLE_ZLS "Embed zls Linux x86_64 release" OFF)
option(TGDB_FORCE_BUNDLED_ZLS
       "Prefer embedded zls (requires TGDB_BUNDLE_ZLS)" OFF)
option(TGDB_BUNDLE_LUA_LS "Embed lua-language-server Linux x86_64 release" OFF)
option(TGDB_FORCE_BUNDLED_LUA_LS
       "Prefer embedded lua-language-server (requires TGDB_BUNDLE_LUA_LS)" OFF)
option(TGDB_BUNDLE_FORTLS "Embed fortls (site-packages; host python3 at runtime)" OFF)
option(TGDB_FORCE_BUNDLED_FORTLS
       "Prefer embedded fortls (requires TGDB_BUNDLE_FORTLS)" OFF)
option(TGDB_BUNDLE_TSSERVER
       "Embed typescript-language-server + Node Linux x86_64" OFF)
option(TGDB_FORCE_BUNDLED_TSSERVER
       "Prefer embedded typescript-language-server (requires TGDB_BUNDLE_TSSERVER)" OFF)

set(TGDB_DEFAULT_UI_LOCALE "es" CACHE STRING "Default UI locale baked into the binary: es|en")
set_property(CACHE TGDB_DEFAULT_UI_LOCALE PROPERTY STRINGS es en)
if(NOT TGDB_DEFAULT_UI_LOCALE MATCHES "^(es|en)$")
  message(FATAL_ERROR "TGDB_DEFAULT_UI_LOCALE must be 'es' or 'en'")
endif()

set(TGDB_CLANGD_VERSION "19.1.2" CACHE STRING "clangd release version to bundle")
set(TGDB_GDB_STATIC_VERSION "v16.3-static" CACHE STRING "gdb-static release tag")
set(TGDB_GDB_CA_VERSION "16.3-ca" CACHE STRING "gdb+core_analyzer bundle version tag")
set(TGDB_RG_VERSION "15.1.0" CACHE STRING "ripgrep release version to bundle")
set(TGDB_BASEDPYRIGHT_VERSION "1.39.9" CACHE STRING "basedpyright version to bundle")
set(TGDB_DEBUGPY_VERSION "1.8.21" CACHE STRING "debugpy version to bundle (full only)")
set(TGDB_PYTHON_STANDALONE_VERSION "3.12.13" CACHE STRING
    "CPython version from python-build-standalone (full only)")
set(TGDB_PYTHON_STANDALONE_TAG "20260623" CACHE STRING
    "python-build-standalone release tag (full only)")
set(TGDB_TEXLAB_VERSION "5.26.0" CACHE STRING "TexLab release version to bundle")
set(TGDB_CHKTEX_VERSION "1.7.10-1" CACHE STRING "Debian chktex package version to bundle with TexLab")
# Avoid '~' in the version: CMake/make then wrap KEY=VALUE in quotes and curl sees a bad URL.
set(TGDB_PCRE2_VERSION "10.47-2" CACHE STRING
    "Debian libpcre2 package version bundled with chktex")
set(TGDB_BASH_LS_NPM_VERSION "5.6.0" CACHE STRING "bash-language-server npm version")
set(TGDB_BASH_LS_VERSION "${TGDB_BASH_LS_NPM_VERSION}" CACHE STRING "bash-ls bundle tag")
set(TGDB_BASH_DAP_VERSION "0.3.9" CACHE STRING "bash DAP adapter bundle tag")
set(TGDB_NODE_VERSION "22.16.0" CACHE STRING "Node.js version for Bash LS/DAP blobs")
set(TGDB_RUST_ANALYZER_VERSION "2025-12-29" CACHE STRING
    "rust-analyzer dated release tag to bundle")
set(TGDB_GOPLS_VERSION "v0.23.0" CACHE STRING "gopls module version (go install)")
set(TGDB_GO_VERSION "1.26.5" CACHE STRING
    "Go toolchain version bootstrapped to build gopls when go is not on PATH")
set(TGDB_ZLS_VERSION "0.16.0" CACHE STRING "zls release version to bundle")
set(TGDB_LUA_LS_VERSION "3.18.2" CACHE STRING "lua-language-server release version to bundle")
set(TGDB_FORTLS_VERSION "3.2.2" CACHE STRING "fortls PyPI version to bundle")
set(TGDB_TYPESCRIPT_LS_NPM_VERSION "5.3.0" CACHE STRING
    "typescript-language-server npm version")
set(TGDB_TYPESCRIPT_VERSION "7.0.2" CACHE STRING "typescript npm version bundled with tsserver")
set(TGDB_TYPESCRIPT_LS_VERSION
    "${TGDB_TYPESCRIPT_LS_NPM_VERSION}+ts${TGDB_TYPESCRIPT_VERSION}" CACHE STRING
    "typescript-language-server bundle tag")

if(TGDB_BUNDLE_PYTHON_TOOLS AND TGDB_BUNDLE_PYTHON_LSP_MIN)
  message(WARNING
          "TGDB_BUNDLE_PYTHON_TOOLS supersedes TGDB_BUNDLE_PYTHON_LSP_MIN; disabling LSP_MIN")
  set(TGDB_BUNDLE_PYTHON_LSP_MIN OFF CACHE BOOL
      "Embed basedpyright site-packages (needs host Python at runtime)" FORCE)
endif()

if(TGDB_BUNDLE_PYTHON_TOOLS)
  set(TGDB_PYTHON_TOOLS_VERSION
      "${TGDB_PYTHON_STANDALONE_VERSION}+${TGDB_BASEDPYRIGHT_VERSION}+${TGDB_DEBUGPY_VERSION}-full")
elseif(TGDB_BUNDLE_PYTHON_LSP_MIN)
  set(TGDB_PYTHON_TOOLS_VERSION "${TGDB_BASEDPYRIGHT_VERSION}-lsp-min")
endif()

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

if(TGDB_FORCE_BUNDLED_PYTHON_TOOLS
   AND NOT TGDB_BUNDLE_PYTHON_LSP_MIN
   AND NOT TGDB_BUNDLE_PYTHON_TOOLS)
  message(FATAL_ERROR
          "TGDB_FORCE_BUNDLED_PYTHON_TOOLS requires TGDB_BUNDLE_PYTHON_LSP_MIN or TGDB_BUNDLE_PYTHON_TOOLS")
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

if(TGDB_BUNDLE_PYTHON_LSP_MIN OR TGDB_BUNDLE_PYTHON_TOOLS)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Python tooling bundles are only supported on Linux")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "Python tooling bundles are only supported on x86_64")
  endif()
endif()

if(TGDB_FORCE_BUNDLED_BASH_LS AND NOT TGDB_BUNDLE_BASH_LS)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_BASH_LS requires TGDB_BUNDLE_BASH_LS=ON")
endif()
if(TGDB_FORCE_BUNDLED_TEXLAB AND NOT TGDB_BUNDLE_TEXLAB)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_TEXLAB requires TGDB_BUNDLE_TEXLAB=ON")
endif()
if(TGDB_FORCE_BUNDLED_BASH_DAP AND NOT TGDB_BUNDLE_BASH_DAP)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_BASH_DAP requires TGDB_BUNDLE_BASH_DAP=ON")
endif()

if(TGDB_FORCE_BUNDLED_RUST_ANALYZER AND NOT TGDB_BUNDLE_RUST_ANALYZER)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_RUST_ANALYZER requires TGDB_BUNDLE_RUST_ANALYZER=ON")
endif()
if(TGDB_FORCE_BUNDLED_GOPLS AND NOT TGDB_BUNDLE_GOPLS)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_GOPLS requires TGDB_BUNDLE_GOPLS=ON")
endif()
if(TGDB_FORCE_BUNDLED_ZLS AND NOT TGDB_BUNDLE_ZLS)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_ZLS requires TGDB_BUNDLE_ZLS=ON")
endif()
if(TGDB_FORCE_BUNDLED_LUA_LS AND NOT TGDB_BUNDLE_LUA_LS)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_LUA_LS requires TGDB_BUNDLE_LUA_LS=ON")
endif()
if(TGDB_FORCE_BUNDLED_FORTLS AND NOT TGDB_BUNDLE_FORTLS)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_FORTLS requires TGDB_BUNDLE_FORTLS=ON")
endif()
if(TGDB_FORCE_BUNDLED_TSSERVER AND NOT TGDB_BUNDLE_TSSERVER)
  message(FATAL_ERROR "TGDB_FORCE_BUNDLED_TSSERVER requires TGDB_BUNDLE_TSSERVER=ON")
endif()

if(TGDB_BUNDLE_BASH_LS OR TGDB_BUNDLE_TEXLAB OR TGDB_BUNDLE_BASH_DAP
   OR TGDB_BUNDLE_RUST_ANALYZER OR TGDB_BUNDLE_GOPLS OR TGDB_BUNDLE_ZLS
   OR TGDB_BUNDLE_LUA_LS OR TGDB_BUNDLE_FORTLS OR TGDB_BUNDLE_TSSERVER)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Embedded tooling bundles are only supported on Linux")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "Embedded tooling bundles are only supported on x86_64")
  endif()
endif()
