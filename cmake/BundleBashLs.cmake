set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

set(TGDB_NODE_TAR_NAME "node-v${TGDB_NODE_VERSION}-linux-x64.tar.xz")
set(TGDB_NODE_URL "https://nodejs.org/dist/v${TGDB_NODE_VERSION}/${TGDB_NODE_TAR_NAME}")
set(TGDB_NODE_TAR_PATH "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_NODE_TAR_NAME}")

set(TGDB_BASH_LS_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/bash_ls_staging")
set(TGDB_BASH_LS_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/bash_ls_payload")
set(TGDB_BASH_LS_TAR_PATH "${TGDB_BUNDLED_GEN_DIR}/bash_ls_blob.tar")
set(TGDB_BASH_LS_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/bash_ls_blob.zst")
set(TGDB_BASH_LS_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/bash_ls_blob.o")
set(TGDB_BASH_LS_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_bash_ls_manifest.hpp")

foreach(_tool objcopy zstd sha256sum npm)
  find_program(_tgdb_${_tool} ${_tool})
  if(NOT _tgdb_${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_BASH_LS requires '${_tool}'")
  endif()
endforeach()

set(TGDB_PREPARE_BASH_LS_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_bash_ls_bundle.sh")

add_custom_command(
  OUTPUT "${TGDB_BASH_LS_ZST_PATH}" "${TGDB_BASH_LS_MANIFEST_HPP}" "${TGDB_BASH_LS_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TGDB_BASH_LS_STAGING_DIR}" "${TGDB_BASH_LS_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TGDB_BASH_LS_VERSION="${TGDB_BASH_LS_VERSION}"
          TGDB_BASH_LS_NPM_VERSION="${TGDB_BASH_LS_NPM_VERSION}"
          TGDB_NODE_URL="${TGDB_NODE_URL}"
          TGDB_NODE_TAR_PATH="${TGDB_NODE_TAR_PATH}"
          TGDB_BASH_LS_STAGING_DIR="${TGDB_BASH_LS_STAGING_DIR}"
          TGDB_BASH_LS_PAYLOAD_DIR="${TGDB_BASH_LS_PAYLOAD_DIR}"
          TGDB_BASH_LS_TAR_PATH="${TGDB_BASH_LS_TAR_PATH}"
          TGDB_BASH_LS_ZST_PATH="${TGDB_BASH_LS_ZST_PATH}"
          TGDB_BASH_LS_MANIFEST_HPP="${TGDB_BASH_LS_MANIFEST_HPP}"
          TGDB_BASH_LS_BLOB_OBJ="${TGDB_BASH_LS_BLOB_OBJ}"
          bash "${TGDB_PREPARE_BASH_LS_SCRIPT}"
  DEPENDS "${TGDB_PREPARE_BASH_LS_SCRIPT}"
  COMMENT "Preparing embedded bash-language-server"
  VERBATIM)

add_custom_target(tgdb_bash_ls_bundle DEPENDS
  "${TGDB_BASH_LS_ZST_PATH}" "${TGDB_BASH_LS_MANIFEST_HPP}" "${TGDB_BASH_LS_BLOB_OBJ}")

include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")
target_sources(tgdb PRIVATE "${TGDB_BASH_LS_BLOB_OBJ}")
target_include_directories(tgdb PRIVATE "${TGDB_BUNDLED_GEN_DIR}")
target_compile_definitions(tgdb PRIVATE TGDB_HAS_BUNDLED_BASH_LS=1)
if(TGDB_FORCE_BUNDLED_BASH_LS)
  target_compile_definitions(tgdb PRIVATE TGDB_DEFAULT_FORCE_BUNDLED_BASH_LS=1)
endif()
target_link_libraries(tgdb PRIVATE libzstd_static)
add_dependencies(tgdb tgdb_bash_ls_bundle)
