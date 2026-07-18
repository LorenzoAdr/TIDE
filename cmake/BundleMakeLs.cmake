include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_MAKE_LS_TOOL_ID "make_ls")
set(TUIDE_MAKE_LS_EXTRACT_MODE "tar_binary")
set(TUIDE_MAKE_LS_BINARY_NAME "make-ls")
set(TUIDE_MAKE_LS_DOWNLOAD_NAME "make-ls_linux_amd64.tar.gz")
set(TUIDE_MAKE_LS_URL
    "https://github.com/owenrumney/make-ls/releases/download/${TUIDE_MAKE_LS_VERSION}/${TUIDE_MAKE_LS_DOWNLOAD_NAME}")
set(TUIDE_MAKE_LS_DOWNLOAD_PATH
    "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_MAKE_LS_VERSION}-${TUIDE_MAKE_LS_DOWNLOAD_NAME}")
set(TUIDE_MAKE_LS_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/make_ls_staging")
set(TUIDE_MAKE_LS_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/make_ls_payload")
set(TUIDE_MAKE_LS_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/make_ls_blob.tar")
set(TUIDE_MAKE_LS_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/make_ls_blob.zst")
set(TUIDE_MAKE_LS_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/make_ls_blob.o")
set(TUIDE_MAKE_LS_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_make_ls_manifest.hpp")
set(TUIDE_MAKE_LS_MANIFEST_PREFIX "TUIDE_BUNDLED_MAKE_LS")

foreach(_tool objcopy zstd sha256sum)
  find_program(_tuide_make_ls_${_tool} ${_tool})
  if(NOT _tuide_make_ls_${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_MAKE_LS requires '${_tool}'")
  endif()
endforeach()

tuide_add_single_bin_bundle_command(MAKE_LS)

add_custom_target(tuide_make_ls_bundle DEPENDS
  "${TUIDE_MAKE_LS_ZST_PATH}" "${TUIDE_MAKE_LS_MANIFEST_HPP}" "${TUIDE_MAKE_LS_BLOB_OBJ}")

tuide_link_embedded_blob(tuide "${TUIDE_MAKE_LS_BLOB_OBJ}" TUIDE_HAS_BUNDLED_MAKE_LS
  TUIDE_DEFAULT_FORCE_BUNDLED_MAKE_LS "${TUIDE_FORCE_BUNDLED_MAKE_LS}")
add_dependencies(tuide tuide_make_ls_bundle)
