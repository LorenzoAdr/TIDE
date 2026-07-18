include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

set(TGDB_MAKE_LS_TOOL_ID "make_ls")
set(TGDB_MAKE_LS_EXTRACT_MODE "tar_binary")
set(TGDB_MAKE_LS_BINARY_NAME "make-ls")
set(TGDB_MAKE_LS_DOWNLOAD_NAME "make-ls_linux_amd64.tar.gz")
set(TGDB_MAKE_LS_URL
    "https://github.com/owenrumney/make-ls/releases/download/${TGDB_MAKE_LS_VERSION}/${TGDB_MAKE_LS_DOWNLOAD_NAME}")
set(TGDB_MAKE_LS_DOWNLOAD_PATH
    "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_MAKE_LS_VERSION}-${TGDB_MAKE_LS_DOWNLOAD_NAME}")
set(TGDB_MAKE_LS_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/make_ls_staging")
set(TGDB_MAKE_LS_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/make_ls_payload")
set(TGDB_MAKE_LS_TAR_PATH "${TGDB_BUNDLED_GEN_DIR}/make_ls_blob.tar")
set(TGDB_MAKE_LS_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/make_ls_blob.zst")
set(TGDB_MAKE_LS_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/make_ls_blob.o")
set(TGDB_MAKE_LS_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_make_ls_manifest.hpp")
set(TGDB_MAKE_LS_MANIFEST_PREFIX "TGDB_BUNDLED_MAKE_LS")

foreach(_tool objcopy zstd sha256sum)
  find_program(_tgdb_make_ls_${_tool} ${_tool})
  if(NOT _tgdb_make_ls_${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_MAKE_LS requires '${_tool}'")
  endif()
endforeach()

tgdb_add_single_bin_bundle_command(MAKE_LS)

add_custom_target(tgdb_make_ls_bundle DEPENDS
  "${TGDB_MAKE_LS_ZST_PATH}" "${TGDB_MAKE_LS_MANIFEST_HPP}" "${TGDB_MAKE_LS_BLOB_OBJ}")

tgdb_link_embedded_blob(tgdb "${TGDB_MAKE_LS_BLOB_OBJ}" TGDB_HAS_BUNDLED_MAKE_LS
  TGDB_DEFAULT_FORCE_BUNDLED_MAKE_LS "${TGDB_FORCE_BUNDLED_MAKE_LS}")
add_dependencies(tgdb tgdb_make_ls_bundle)
