set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_NODE_TAR_NAME "node-v${TUIDE_NODE_VERSION}-linux-x64.tar.xz")
set(TUIDE_NODE_URL "https://nodejs.org/dist/v${TUIDE_NODE_VERSION}/${TUIDE_NODE_TAR_NAME}")
set(TUIDE_NODE_TAR_PATH "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_NODE_TAR_NAME}")

set(TUIDE_BASH_LS_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/bash_ls_staging")
set(TUIDE_BASH_LS_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/bash_ls_payload")
set(TUIDE_BASH_LS_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/bash_ls_blob.tar")
set(TUIDE_BASH_LS_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/bash_ls_blob.zst")
set(TUIDE_BASH_LS_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/bash_ls_blob.o")
set(TUIDE_BASH_LS_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_bash_ls_manifest.hpp")

foreach(_tool objcopy zstd sha256sum npm)
  find_program(_tuide_${_tool} ${_tool})
  if(NOT _tuide_${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_BASH_LS requires '${_tool}'")
  endif()
endforeach()

set(TUIDE_PREPARE_BASH_LS_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_bash_ls_bundle.sh")

add_custom_command(
  OUTPUT "${TUIDE_BASH_LS_ZST_PATH}" "${TUIDE_BASH_LS_MANIFEST_HPP}" "${TUIDE_BASH_LS_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TUIDE_BASH_LS_STAGING_DIR}" "${TUIDE_BASH_LS_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TUIDE_BASH_LS_VERSION="${TUIDE_BASH_LS_VERSION}"
          TUIDE_BASH_LS_NPM_VERSION="${TUIDE_BASH_LS_NPM_VERSION}"
          TUIDE_NODE_URL="${TUIDE_NODE_URL}"
          TUIDE_NODE_TAR_PATH="${TUIDE_NODE_TAR_PATH}"
          TUIDE_BASH_LS_STAGING_DIR="${TUIDE_BASH_LS_STAGING_DIR}"
          TUIDE_BASH_LS_PAYLOAD_DIR="${TUIDE_BASH_LS_PAYLOAD_DIR}"
          TUIDE_BASH_LS_TAR_PATH="${TUIDE_BASH_LS_TAR_PATH}"
          TUIDE_BASH_LS_ZST_PATH="${TUIDE_BASH_LS_ZST_PATH}"
          TUIDE_BASH_LS_MANIFEST_HPP="${TUIDE_BASH_LS_MANIFEST_HPP}"
          TUIDE_BASH_LS_BLOB_OBJ="${TUIDE_BASH_LS_BLOB_OBJ}"
          bash "${TUIDE_PREPARE_BASH_LS_SCRIPT}"
  DEPENDS "${TUIDE_PREPARE_BASH_LS_SCRIPT}"
  COMMENT "Preparing embedded bash-language-server"
  VERBATIM)

add_custom_target(tuide_bash_ls_bundle DEPENDS
  "${TUIDE_BASH_LS_ZST_PATH}" "${TUIDE_BASH_LS_MANIFEST_HPP}" "${TUIDE_BASH_LS_BLOB_OBJ}")

include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")
target_sources(tuide PRIVATE "${TUIDE_BASH_LS_BLOB_OBJ}")
target_include_directories(tuide PRIVATE "${TUIDE_BUNDLED_GEN_DIR}")
target_compile_definitions(tuide PRIVATE TUIDE_HAS_BUNDLED_BASH_LS=1)
if(TUIDE_FORCE_BUNDLED_BASH_LS)
  target_compile_definitions(tuide PRIVATE TUIDE_DEFAULT_FORCE_BUNDLED_BASH_LS=1)
endif()
target_link_libraries(tuide PRIVATE libzstd_static)
add_dependencies(tuide tuide_bash_ls_bundle)
