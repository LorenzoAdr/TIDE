include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

set(TGDB_ZLS_TOOL_ID "zls")
set(TGDB_ZLS_EXTRACT_MODE "tar_binary")
set(TGDB_ZLS_BINARY_NAME "zls")
set(TGDB_ZLS_DOWNLOAD_NAME "zls-x86_64-linux.tar.xz")
set(TGDB_ZLS_URL
    "https://github.com/zigtools/zls/releases/download/${TGDB_ZLS_VERSION}/${TGDB_ZLS_DOWNLOAD_NAME}")
set(TGDB_ZLS_DOWNLOAD_PATH
    "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_ZLS_VERSION}-${TGDB_ZLS_DOWNLOAD_NAME}")
set(TGDB_ZLS_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/zls_staging")
set(TGDB_ZLS_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/zls_payload")
set(TGDB_ZLS_TAR_PATH "${TGDB_BUNDLED_GEN_DIR}/zls_blob.tar")
set(TGDB_ZLS_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/zls_blob.zst")
set(TGDB_ZLS_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/zls_blob.o")
set(TGDB_ZLS_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_zls_manifest.hpp")
set(TGDB_ZLS_MANIFEST_PREFIX "TGDB_BUNDLED_ZLS")

foreach(_tool objcopy zstd sha256sum)
  find_program(_tgdb_zls_${_tool} ${_tool})
  if(NOT _tgdb_zls_${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_ZLS requires '${_tool}'")
  endif()
endforeach()

tgdb_add_single_bin_bundle_command(ZLS)

add_custom_target(tgdb_zls_bundle DEPENDS
  "${TGDB_ZLS_ZST_PATH}" "${TGDB_ZLS_MANIFEST_HPP}" "${TGDB_ZLS_BLOB_OBJ}")

tgdb_link_embedded_blob(tgdb "${TGDB_ZLS_BLOB_OBJ}" TGDB_HAS_BUNDLED_ZLS
  TGDB_DEFAULT_FORCE_BUNDLED_ZLS "${TGDB_FORCE_BUNDLED_ZLS}")
add_dependencies(tgdb tgdb_zls_bundle)
