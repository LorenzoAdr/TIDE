set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

set(TGDB_NODE_TAR_NAME "node-v${TGDB_NODE_VERSION}-linux-x64.tar.xz")
set(TGDB_NODE_URL "https://nodejs.org/dist/v${TGDB_NODE_VERSION}/${TGDB_NODE_TAR_NAME}")
set(TGDB_NODE_TAR_PATH "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_NODE_TAR_NAME}")

set(TGDB_BASH_DAP_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/bash_dap_staging")
set(TGDB_BASH_DAP_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/bash_dap_payload")
set(TGDB_BASH_DAP_TAR_PATH "${TGDB_BUNDLED_GEN_DIR}/bash_dap_blob.tar")
set(TGDB_BASH_DAP_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/bash_dap_blob.zst")
set(TGDB_BASH_DAP_MANIFEST_PATH "${TGDB_BUNDLED_GEN_DIR}/bash_dap_manifest.json")
set(TGDB_BASH_DAP_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/bash_dap_blob.o")
set(TGDB_BASH_DAP_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_bash_dap_manifest.hpp")
set(TGDB_BASH_DEBUG_SRC "${CMAKE_SOURCE_DIR}/third_party/bash-debug")

# Share Node with bash-ls blob when both are enabled.
set(TGDB_BASH_DAP_INCLUDE_NODE 1)
if(TGDB_BUNDLE_BASH_LS)
  set(TGDB_BASH_DAP_INCLUDE_NODE 0)
endif()

foreach(_tool objcopy zstd sha256sum)
  find_program(_tgdb_${_tool} ${_tool})
  if(NOT _tgdb_${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_BASH_DAP requires '${_tool}'")
  endif()
endforeach()

if(NOT EXISTS "${TGDB_BASH_DEBUG_SRC}/out/bashDebug.js")
  message(FATAL_ERROR
    "TGDB_BUNDLE_BASH_DAP requires ${TGDB_BASH_DEBUG_SRC}/out/bashDebug.js "
    "(clone vscode-bash-debug and npm run compile)")
endif()

set(TGDB_PREPARE_BASH_DAP_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_bash_dap_bundle.sh")

add_custom_command(
  OUTPUT "${TGDB_BASH_DAP_ZST_PATH}" "${TGDB_BASH_DAP_MANIFEST_HPP}" "${TGDB_BASH_DAP_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TGDB_BASH_DAP_STAGING_DIR}" "${TGDB_BASH_DAP_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TGDB_BASH_DAP_VERSION="${TGDB_BASH_DAP_VERSION}"
          TGDB_BASH_DEBUG_SRC="${TGDB_BASH_DEBUG_SRC}"
          TGDB_NODE_URL="${TGDB_NODE_URL}"
          TGDB_NODE_TAR_PATH="${TGDB_NODE_TAR_PATH}"
          TGDB_BASH_DAP_INCLUDE_NODE="${TGDB_BASH_DAP_INCLUDE_NODE}"
          TGDB_BASH_DAP_STAGING_DIR="${TGDB_BASH_DAP_STAGING_DIR}"
          TGDB_BASH_DAP_PAYLOAD_DIR="${TGDB_BASH_DAP_PAYLOAD_DIR}"
          TGDB_BASH_DAP_TAR_PATH="${TGDB_BASH_DAP_TAR_PATH}"
          TGDB_BASH_DAP_ZST_PATH="${TGDB_BASH_DAP_ZST_PATH}"
          TGDB_BASH_DAP_MANIFEST_PATH="${TGDB_BASH_DAP_MANIFEST_PATH}"
          TGDB_BASH_DAP_MANIFEST_HPP="${TGDB_BASH_DAP_MANIFEST_HPP}"
          TGDB_BASH_DAP_BLOB_OBJ="${TGDB_BASH_DAP_BLOB_OBJ}"
          bash "${TGDB_PREPARE_BASH_DAP_SCRIPT}"
  DEPENDS "${TGDB_PREPARE_BASH_DAP_SCRIPT}"
  COMMENT "Preparing embedded Bash DAP (bashdb adapter)"
  VERBATIM)

add_custom_target(tgdb_bash_dap_bundle DEPENDS
  "${TGDB_BASH_DAP_ZST_PATH}" "${TGDB_BASH_DAP_MANIFEST_HPP}" "${TGDB_BASH_DAP_BLOB_OBJ}")

include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")
target_sources(tgdb PRIVATE "${TGDB_BASH_DAP_BLOB_OBJ}")
target_include_directories(tgdb PRIVATE "${TGDB_BUNDLED_GEN_DIR}")
target_compile_definitions(tgdb PRIVATE TGDB_HAS_BUNDLED_BASH_DAP=1)
if(TGDB_FORCE_BUNDLED_BASH_DAP)
  target_compile_definitions(tgdb PRIVATE TGDB_DEFAULT_FORCE_BUNDLED_BASH_DAP=1)
endif()
target_link_libraries(tgdb PRIVATE libzstd_static)
add_dependencies(tgdb tgdb_bash_dap_bundle)
