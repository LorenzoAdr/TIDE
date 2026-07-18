set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_NODE_TAR_NAME "node-v${TUIDE_NODE_VERSION}-linux-x64.tar.xz")
set(TUIDE_NODE_URL "https://nodejs.org/dist/v${TUIDE_NODE_VERSION}/${TUIDE_NODE_TAR_NAME}")
set(TUIDE_NODE_TAR_PATH "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_NODE_TAR_NAME}")

set(TUIDE_BASH_DAP_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/bash_dap_staging")
set(TUIDE_BASH_DAP_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/bash_dap_payload")
set(TUIDE_BASH_DAP_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/bash_dap_blob.tar")
set(TUIDE_BASH_DAP_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/bash_dap_blob.zst")
set(TUIDE_BASH_DAP_MANIFEST_PATH "${TUIDE_BUNDLED_GEN_DIR}/bash_dap_manifest.json")
set(TUIDE_BASH_DAP_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/bash_dap_blob.o")
set(TUIDE_BASH_DAP_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_bash_dap_manifest.hpp")
set(TUIDE_BASH_DEBUG_SRC "${CMAKE_SOURCE_DIR}/third_party/bash-debug")

# Share Node with bash-ls blob when both are enabled.
set(TUIDE_BASH_DAP_INCLUDE_NODE 1)
if(TUIDE_BUNDLE_BASH_LS)
  set(TUIDE_BASH_DAP_INCLUDE_NODE 0)
endif()

foreach(_tool objcopy zstd sha256sum)
  find_program(_tuide_${_tool} ${_tool})
  if(NOT _tuide_${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_BASH_DAP requires '${_tool}'")
  endif()
endforeach()

if(NOT EXISTS "${TUIDE_BASH_DEBUG_SRC}/out/bashDebug.js")
  message(FATAL_ERROR
    "TUIDE_BUNDLE_BASH_DAP requires ${TUIDE_BASH_DEBUG_SRC}/out/bashDebug.js "
    "(clone vscode-bash-debug and npm run compile)")
endif()

set(TUIDE_PREPARE_BASH_DAP_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_bash_dap_bundle.sh")

add_custom_command(
  OUTPUT "${TUIDE_BASH_DAP_ZST_PATH}" "${TUIDE_BASH_DAP_MANIFEST_HPP}" "${TUIDE_BASH_DAP_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TUIDE_BASH_DAP_STAGING_DIR}" "${TUIDE_BASH_DAP_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TUIDE_BASH_DAP_VERSION="${TUIDE_BASH_DAP_VERSION}"
          TUIDE_BASH_DEBUG_SRC="${TUIDE_BASH_DEBUG_SRC}"
          TUIDE_NODE_URL="${TUIDE_NODE_URL}"
          TUIDE_NODE_TAR_PATH="${TUIDE_NODE_TAR_PATH}"
          TUIDE_BASH_DAP_INCLUDE_NODE="${TUIDE_BASH_DAP_INCLUDE_NODE}"
          TUIDE_BASH_DAP_STAGING_DIR="${TUIDE_BASH_DAP_STAGING_DIR}"
          TUIDE_BASH_DAP_PAYLOAD_DIR="${TUIDE_BASH_DAP_PAYLOAD_DIR}"
          TUIDE_BASH_DAP_TAR_PATH="${TUIDE_BASH_DAP_TAR_PATH}"
          TUIDE_BASH_DAP_ZST_PATH="${TUIDE_BASH_DAP_ZST_PATH}"
          TUIDE_BASH_DAP_MANIFEST_PATH="${TUIDE_BASH_DAP_MANIFEST_PATH}"
          TUIDE_BASH_DAP_MANIFEST_HPP="${TUIDE_BASH_DAP_MANIFEST_HPP}"
          TUIDE_BASH_DAP_BLOB_OBJ="${TUIDE_BASH_DAP_BLOB_OBJ}"
          bash "${TUIDE_PREPARE_BASH_DAP_SCRIPT}"
  DEPENDS "${TUIDE_PREPARE_BASH_DAP_SCRIPT}"
  COMMENT "Preparing embedded Bash DAP (bashdb adapter)"
  VERBATIM)

add_custom_target(tuide_bash_dap_bundle DEPENDS
  "${TUIDE_BASH_DAP_ZST_PATH}" "${TUIDE_BASH_DAP_MANIFEST_HPP}" "${TUIDE_BASH_DAP_BLOB_OBJ}")

include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")
target_sources(tuide PRIVATE "${TUIDE_BASH_DAP_BLOB_OBJ}")
target_include_directories(tuide PRIVATE "${TUIDE_BUNDLED_GEN_DIR}")
target_compile_definitions(tuide PRIVATE TUIDE_HAS_BUNDLED_BASH_DAP=1)
if(TUIDE_FORCE_BUNDLED_BASH_DAP)
  target_compile_definitions(tuide PRIVATE TUIDE_DEFAULT_FORCE_BUNDLED_BASH_DAP=1)
endif()
target_link_libraries(tuide PRIVATE libzstd_static)
add_dependencies(tuide tuide_bash_dap_bundle)
