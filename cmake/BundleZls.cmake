include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_ZLS_TOOL_ID "zls")
set(TUIDE_ZLS_EXTRACT_MODE "tar_binary")
set(TUIDE_ZLS_BINARY_NAME "zls")
set(TUIDE_ZLS_DOWNLOAD_NAME "zls-x86_64-linux.tar.xz")
set(TUIDE_ZLS_URL
    "https://github.com/zigtools/zls/releases/download/${TUIDE_ZLS_VERSION}/${TUIDE_ZLS_DOWNLOAD_NAME}")
set(TUIDE_ZLS_DOWNLOAD_PATH
    "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_ZLS_VERSION}-${TUIDE_ZLS_DOWNLOAD_NAME}")
set(TUIDE_ZLS_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/zls_staging")
set(TUIDE_ZLS_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/zls_payload")
set(TUIDE_ZLS_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/zls_blob.tar")
set(TUIDE_ZLS_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/zls_blob.zst")
set(TUIDE_ZLS_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/zls_blob.o")
set(TUIDE_ZLS_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_zls_manifest.hpp")
set(TUIDE_ZLS_MANIFEST_PREFIX "TUIDE_BUNDLED_ZLS")

foreach(_tool objcopy zstd sha256sum)
  find_program(_tuide_zls_${_tool} ${_tool})
  if(NOT _tuide_zls_${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_ZLS requires '${_tool}'")
  endif()
endforeach()

tuide_add_single_bin_bundle_command(ZLS)

add_custom_target(tuide_zls_bundle DEPENDS
  "${TUIDE_ZLS_ZST_PATH}" "${TUIDE_ZLS_MANIFEST_HPP}" "${TUIDE_ZLS_BLOB_OBJ}")

tuide_link_embedded_blob(tuide "${TUIDE_ZLS_BLOB_OBJ}" TUIDE_HAS_BUNDLED_ZLS
  TUIDE_DEFAULT_FORCE_BUNDLED_ZLS "${TUIDE_FORCE_BUNDLED_ZLS}")
add_dependencies(tuide tuide_zls_bundle)
