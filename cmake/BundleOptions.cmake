option(TUIDE_BUNDLE_CLANGD "Embed official clangd Linux x86_64 release" OFF)
option(TUIDE_FORCE_BUNDLED_CLANGD
       "At runtime, never fall back to clangd on PATH (requires TUIDE_BUNDLE_CLANGD)" OFF)

option(TUIDE_BUNDLE_GDB "Embed gdb Linux x86_64 build" OFF)
set(TUIDE_GDB_BUNDLE_KIND "static" CACHE STRING "Bundled gdb kind: static|core_analyzer")
set_property(CACHE TUIDE_GDB_BUNDLE_KIND PROPERTY STRINGS static core_analyzer)
option(TUIDE_BUILD_GDB_CA "Build gdb+Core Analyzer during bundle step (slow)" OFF)
option(TUIDE_FORCE_BUNDLED_GDB
       "At runtime, never fall back to gdb on PATH (requires TUIDE_BUNDLE_GDB)" OFF)

option(TUIDE_BUNDLE_RG "Embed official ripgrep Linux x86_64 release" ON)
option(TUIDE_FORCE_BUNDLED_RG
       "At runtime, never fall back to rg on PATH (requires TUIDE_BUNDLE_RG)" OFF)

# L1 uses llama-cli at runtime (PATH / auto-download to $XDG_CACHE_HOME/tuide/models).
# Linking llama.cpp into the binary stays optional for a future bundle step (D8).
option(TUIDE_BUNDLE_LLAMA
       "Reserve CMake hook for embedding llama.cpp (OFF: runtime llama-cli)" OFF)

# Python tooling: A = basedpyright only (host Python); B = CPython + basedpyright + debugpy.
# B supersedes A when both are requested.
option(TUIDE_BUNDLE_PYTHON_LSP_MIN
       "Embed basedpyright site-packages (needs host Python at runtime)" OFF)
option(TUIDE_BUNDLE_PYTHON_TOOLS
       "Embed portable CPython + basedpyright + debugpy" OFF)
option(TUIDE_FORCE_BUNDLED_PYTHON_TOOLS
       "At runtime, prefer bundled Python tools (requires A or B)" OFF)

option(TUIDE_BUNDLE_BASH_LS "Embed bash-language-server + Node" OFF)
option(TUIDE_FORCE_BUNDLED_BASH_LS
       "Prefer embedded bash-language-server (requires TUIDE_BUNDLE_BASH_LS)" OFF)
option(TUIDE_BUNDLE_TEXLAB "Embed TexLab + ChkTeX Linux x86_64 release" OFF)
option(TUIDE_FORCE_BUNDLED_TEXLAB
       "Prefer embedded TexLab (requires TUIDE_BUNDLE_TEXLAB)" OFF)
option(TUIDE_BUNDLE_BASH_DAP "Embed Bash DAP adapter + bashdb (+ Node unless BASH_LS)" OFF)
option(TUIDE_FORCE_BUNDLED_BASH_DAP
       "Prefer embedded Bash DAP (requires TUIDE_BUNDLE_BASH_DAP)" OFF)

option(TUIDE_BUNDLE_RUST_ANALYZER "Embed rust-analyzer Linux x86_64 release" OFF)
option(TUIDE_FORCE_BUNDLED_RUST_ANALYZER
       "Prefer embedded rust-analyzer (requires TUIDE_BUNDLE_RUST_ANALYZER)" OFF)
option(TUIDE_BUNDLE_GOPLS "Embed gopls Linux x86_64 (go install at bundle time)" OFF)
option(TUIDE_FORCE_BUNDLED_GOPLS
       "Prefer embedded gopls (requires TUIDE_BUNDLE_GOPLS)" OFF)
option(TUIDE_BUNDLE_ZLS "Embed zls Linux x86_64 release" OFF)
option(TUIDE_FORCE_BUNDLED_ZLS
       "Prefer embedded zls (requires TUIDE_BUNDLE_ZLS)" OFF)
option(TUIDE_BUNDLE_LUA_LS "Embed lua-language-server Linux x86_64 release" OFF)
option(TUIDE_FORCE_BUNDLED_LUA_LS
       "Prefer embedded lua-language-server (requires TUIDE_BUNDLE_LUA_LS)" OFF)
option(TUIDE_BUNDLE_FORTLS "Embed fortls (site-packages; host python3 at runtime)" OFF)
option(TUIDE_FORCE_BUNDLED_FORTLS
       "Prefer embedded fortls (requires TUIDE_BUNDLE_FORTLS)" OFF)
option(TUIDE_BUNDLE_TSSERVER
       "Embed typescript-language-server + Node Linux x86_64" OFF)
option(TUIDE_FORCE_BUNDLED_TSSERVER
       "Prefer embedded typescript-language-server (requires TUIDE_BUNDLE_TSSERVER)" OFF)
option(TUIDE_BUNDLE_NEOCMAKELSP "Embed neocmakelsp Linux x86_64 release" OFF)
option(TUIDE_FORCE_BUNDLED_NEOCMAKELSP
       "Prefer embedded neocmakelsp (requires TUIDE_BUNDLE_NEOCMAKELSP)" OFF)
option(TUIDE_BUNDLE_MAKE_LS "Embed make-ls Linux x86_64 release" OFF)
option(TUIDE_FORCE_BUNDLED_MAKE_LS
       "Prefer embedded make-ls (requires TUIDE_BUNDLE_MAKE_LS)" OFF)
option(TUIDE_BUNDLE_YAML_LS "Embed yaml-language-server + Node Linux x86_64" OFF)
option(TUIDE_FORCE_BUNDLED_YAML_LS
       "Prefer embedded yaml-language-server (requires TUIDE_BUNDLE_YAML_LS)" OFF)
option(TUIDE_BUNDLE_LEMMINX "Embed LemMinX Linux x86_64 release" OFF)
option(TUIDE_FORCE_BUNDLED_LEMMINX
       "Prefer embedded LemMinX (requires TUIDE_BUNDLE_LEMMINX)" OFF)

set(TUIDE_DEFAULT_UI_LOCALE "en" CACHE STRING "Default UI locale baked into the binary: es|en")
set_property(CACHE TUIDE_DEFAULT_UI_LOCALE PROPERTY STRINGS es en)
if(NOT TUIDE_DEFAULT_UI_LOCALE MATCHES "^(es|en)$")
  message(FATAL_ERROR "TUIDE_DEFAULT_UI_LOCALE must be 'es' or 'en'")
endif()

option(TUIDE_DEFAULT_HELIX_MODE
       "Default editor mode for new installs: ON=Helix, OFF=Normal" OFF)

set(TUIDE_CLANGD_VERSION "19.1.2" CACHE STRING "clangd release version to bundle")
set(TUIDE_GDB_STATIC_VERSION "v16.3-static" CACHE STRING "gdb-static release tag")
set(TUIDE_GDB_CA_VERSION "16.3-ca" CACHE STRING "gdb+core_analyzer bundle version tag")
set(TUIDE_RG_VERSION "15.1.0" CACHE STRING "ripgrep release version to bundle")
set(TUIDE_BASEDPYRIGHT_VERSION "1.39.9" CACHE STRING "basedpyright version to bundle")
set(TUIDE_DEBUGPY_VERSION "1.8.21" CACHE STRING "debugpy version to bundle (full only)")
set(TUIDE_PYTHON_STANDALONE_VERSION "3.12.13" CACHE STRING
    "CPython version from python-build-standalone (full only)")
set(TUIDE_PYTHON_STANDALONE_TAG "20260623" CACHE STRING
    "python-build-standalone release tag (full only)")
set(TUIDE_TEXLAB_VERSION "5.26.0" CACHE STRING "TexLab release version to bundle")
set(TUIDE_CHKTEX_VERSION "1.7.10-1" CACHE STRING "Debian chktex package version to bundle with TexLab")
# Avoid '~' in the version: CMake/make then wrap KEY=VALUE in quotes and curl sees a bad URL.
set(TUIDE_PCRE2_VERSION "10.47-2" CACHE STRING
    "Debian libpcre2 package version bundled with chktex")
set(TUIDE_BASH_LS_NPM_VERSION "5.6.0" CACHE STRING "bash-language-server npm version")
set(TUIDE_BASH_LS_VERSION "${TUIDE_BASH_LS_NPM_VERSION}" CACHE STRING "bash-ls bundle tag")
set(TUIDE_BASH_DAP_VERSION "0.3.9" CACHE STRING "bash DAP adapter bundle tag")
set(TUIDE_NODE_VERSION "22.16.0" CACHE STRING "Node.js version for Bash LS/DAP blobs")
set(TUIDE_RUST_ANALYZER_VERSION "2025-12-29" CACHE STRING
    "rust-analyzer dated release tag to bundle")
set(TUIDE_GOPLS_VERSION "v0.23.0" CACHE STRING "gopls module version (go install)")
set(TUIDE_GO_VERSION "1.26.5" CACHE STRING
    "Go toolchain version bootstrapped to build gopls when go is not on PATH")
set(TUIDE_ZLS_VERSION "0.16.0" CACHE STRING "zls release version to bundle")
set(TUIDE_LUA_LS_VERSION "3.18.2" CACHE STRING "lua-language-server release version to bundle")
set(TUIDE_FORTLS_VERSION "3.2.2" CACHE STRING "fortls PyPI version to bundle")
set(TUIDE_TYPESCRIPT_LS_NPM_VERSION "5.3.0" CACHE STRING
    "typescript-language-server npm version")
set(TUIDE_TYPESCRIPT_VERSION "7.0.2" CACHE STRING "typescript npm version bundled with tsserver")
set(TUIDE_TYPESCRIPT_LS_VERSION
    "${TUIDE_TYPESCRIPT_LS_NPM_VERSION}+ts${TUIDE_TYPESCRIPT_VERSION}" CACHE STRING
    "typescript-language-server bundle tag")
set(TUIDE_NEOCMAKELSP_VERSION "v0.10.4" CACHE STRING "neocmakelsp release version to bundle")
set(TUIDE_MAKE_LS_VERSION "v0.1.16" CACHE STRING "make-ls release version to bundle")
set(TUIDE_YAML_LS_NPM_VERSION "1.24.0" CACHE STRING "yaml-language-server npm version")
set(TUIDE_YAML_LS_VERSION "${TUIDE_YAML_LS_NPM_VERSION}" CACHE STRING
    "yaml-language-server bundle tag")
set(TUIDE_LEMMINX_VERSION "0.29.3" CACHE STRING
    "LemMinX / vscode-xml release version to bundle")

if(TUIDE_BUNDLE_PYTHON_TOOLS AND TUIDE_BUNDLE_PYTHON_LSP_MIN)
  message(WARNING
          "TUIDE_BUNDLE_PYTHON_TOOLS supersedes TUIDE_BUNDLE_PYTHON_LSP_MIN; disabling LSP_MIN")
  set(TUIDE_BUNDLE_PYTHON_LSP_MIN OFF CACHE BOOL
      "Embed basedpyright site-packages (needs host Python at runtime)" FORCE)
endif()

if(TUIDE_BUNDLE_PYTHON_TOOLS)
  set(TUIDE_PYTHON_TOOLS_VERSION
      "${TUIDE_PYTHON_STANDALONE_VERSION}+${TUIDE_BASEDPYRIGHT_VERSION}+${TUIDE_DEBUGPY_VERSION}-full")
elseif(TUIDE_BUNDLE_PYTHON_LSP_MIN)
  set(TUIDE_PYTHON_TOOLS_VERSION "${TUIDE_BASEDPYRIGHT_VERSION}-lsp-min")
endif()

if(TUIDE_FORCE_BUNDLED_CLANGD AND NOT TUIDE_BUNDLE_CLANGD)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_CLANGD requires TUIDE_BUNDLE_CLANGD=ON")
endif()

if(TUIDE_FORCE_BUNDLED_GDB AND NOT TUIDE_BUNDLE_GDB)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_GDB requires TUIDE_BUNDLE_GDB=ON")
endif()

if(TUIDE_BUNDLE_GDB)
  if(NOT TUIDE_GDB_BUNDLE_KIND MATCHES "^(static|core_analyzer)$")
    message(FATAL_ERROR "TUIDE_GDB_BUNDLE_KIND must be 'static' or 'core_analyzer'")
  endif()
endif()

if(TUIDE_FORCE_BUNDLED_RG AND NOT TUIDE_BUNDLE_RG)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_RG requires TUIDE_BUNDLE_RG=ON")
endif()

if(TUIDE_FORCE_BUNDLED_PYTHON_TOOLS
   AND NOT TUIDE_BUNDLE_PYTHON_LSP_MIN
   AND NOT TUIDE_BUNDLE_PYTHON_TOOLS)
  message(FATAL_ERROR
          "TUIDE_FORCE_BUNDLED_PYTHON_TOOLS requires TUIDE_BUNDLE_PYTHON_LSP_MIN or TUIDE_BUNDLE_PYTHON_TOOLS")
endif()

if(TUIDE_BUNDLE_CLANGD)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "TUIDE_BUNDLE_CLANGD is only supported on Linux")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "TUIDE_BUNDLE_CLANGD is only supported on x86_64")
  endif()
endif()

if(TUIDE_BUNDLE_GDB)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "TUIDE_BUNDLE_GDB is only supported on Linux")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "TUIDE_BUNDLE_GDB is only supported on x86_64")
  endif()
endif()

if(TUIDE_BUNDLE_RG)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "TUIDE_BUNDLE_RG is only supported on Linux")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "TUIDE_BUNDLE_RG is only supported on x86_64")
  endif()
endif()

if(TUIDE_BUNDLE_PYTHON_LSP_MIN OR TUIDE_BUNDLE_PYTHON_TOOLS)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Python tooling bundles are only supported on Linux")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "Python tooling bundles are only supported on x86_64")
  endif()
endif()

if(TUIDE_FORCE_BUNDLED_BASH_LS AND NOT TUIDE_BUNDLE_BASH_LS)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_BASH_LS requires TUIDE_BUNDLE_BASH_LS=ON")
endif()
if(TUIDE_FORCE_BUNDLED_TEXLAB AND NOT TUIDE_BUNDLE_TEXLAB)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_TEXLAB requires TUIDE_BUNDLE_TEXLAB=ON")
endif()
if(TUIDE_FORCE_BUNDLED_BASH_DAP AND NOT TUIDE_BUNDLE_BASH_DAP)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_BASH_DAP requires TUIDE_BUNDLE_BASH_DAP=ON")
endif()

if(TUIDE_FORCE_BUNDLED_RUST_ANALYZER AND NOT TUIDE_BUNDLE_RUST_ANALYZER)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_RUST_ANALYZER requires TUIDE_BUNDLE_RUST_ANALYZER=ON")
endif()
if(TUIDE_FORCE_BUNDLED_GOPLS AND NOT TUIDE_BUNDLE_GOPLS)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_GOPLS requires TUIDE_BUNDLE_GOPLS=ON")
endif()
if(TUIDE_FORCE_BUNDLED_ZLS AND NOT TUIDE_BUNDLE_ZLS)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_ZLS requires TUIDE_BUNDLE_ZLS=ON")
endif()
if(TUIDE_FORCE_BUNDLED_LUA_LS AND NOT TUIDE_BUNDLE_LUA_LS)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_LUA_LS requires TUIDE_BUNDLE_LUA_LS=ON")
endif()
if(TUIDE_FORCE_BUNDLED_FORTLS AND NOT TUIDE_BUNDLE_FORTLS)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_FORTLS requires TUIDE_BUNDLE_FORTLS=ON")
endif()
if(TUIDE_FORCE_BUNDLED_TSSERVER AND NOT TUIDE_BUNDLE_TSSERVER)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_TSSERVER requires TUIDE_BUNDLE_TSSERVER=ON")
endif()
if(TUIDE_FORCE_BUNDLED_NEOCMAKELSP AND NOT TUIDE_BUNDLE_NEOCMAKELSP)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_NEOCMAKELSP requires TUIDE_BUNDLE_NEOCMAKELSP=ON")
endif()
if(TUIDE_FORCE_BUNDLED_MAKE_LS AND NOT TUIDE_BUNDLE_MAKE_LS)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_MAKE_LS requires TUIDE_BUNDLE_MAKE_LS=ON")
endif()
if(TUIDE_FORCE_BUNDLED_YAML_LS AND NOT TUIDE_BUNDLE_YAML_LS)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_YAML_LS requires TUIDE_BUNDLE_YAML_LS=ON")
endif()
if(TUIDE_FORCE_BUNDLED_LEMMINX AND NOT TUIDE_BUNDLE_LEMMINX)
  message(FATAL_ERROR "TUIDE_FORCE_BUNDLED_LEMMINX requires TUIDE_BUNDLE_LEMMINX=ON")
endif()

if(TUIDE_BUNDLE_BASH_LS OR TUIDE_BUNDLE_TEXLAB OR TUIDE_BUNDLE_BASH_DAP
   OR TUIDE_BUNDLE_RUST_ANALYZER OR TUIDE_BUNDLE_GOPLS OR TUIDE_BUNDLE_ZLS
   OR TUIDE_BUNDLE_LUA_LS OR TUIDE_BUNDLE_FORTLS OR TUIDE_BUNDLE_TSSERVER
   OR TUIDE_BUNDLE_NEOCMAKELSP OR TUIDE_BUNDLE_MAKE_LS OR TUIDE_BUNDLE_YAML_LS
   OR TUIDE_BUNDLE_LEMMINX)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Embedded tooling bundles are only supported on Linux")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "Embedded tooling bundles are only supported on x86_64")
  endif()
endif()
