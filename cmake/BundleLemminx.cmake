include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_LEMMINX_TOOL_ID "lemminx")
set(TUIDE_LEMMINX_EXTRACT_MODE "zip_binary")
set(TUIDE_LEMMINX_BINARY_NAME "lemminx")
set(TUIDE_LEMMINX_ARCHIVE_BINARY_NAME "lemminx-linux-x86_64")
set(TUIDE_LEMMINX_DOWNLOAD_NAME "lemminx-linux-x86_64.zip")
set(TUIDE_LEMMINX_URL
    "https://github.com/redhat-developer/vscode-xml/releases/download/${TUIDE_LEMMINX_VERSION}/${TUIDE_LEMMINX_DOWNLOAD_NAME}")
set(TUIDE_LEMMINX_DOWNLOAD_PATH
    "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_LEMMINX_VERSION}-${TUIDE_LEMMINX_DOWNLOAD_NAME}")
set(TUIDE_LEMMINX_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/lemminx_staging")
set(TUIDE_LEMMINX_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/lemminx_payload")
set(TUIDE_LEMMINX_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/lemminx_blob.tar")
set(TUIDE_LEMMINX_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/lemminx_blob.zst")
set(TUIDE_LEMMINX_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/lemminx_blob.o")
set(TUIDE_LEMMINX_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_lemminx_manifest.hpp")
set(TUIDE_LEMMINX_MANIFEST_PREFIX "TUIDE_BUNDLED_LEMMINX")

foreach(_tool objcopy zstd sha256sum unzip)
  find_program(_tuide_lemminx_${_tool} ${_tool})
  if(NOT _tuide_lemminx_${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_LEMMINX requires '${_tool}'")
  endif()
endforeach()

tuide_add_single_bin_bundle_command(LEMMINX)

add_custom_target(tuide_lemminx_bundle DEPENDS
  "${TUIDE_LEMMINX_ZST_PATH}" "${TUIDE_LEMMINX_MANIFEST_HPP}" "${TUIDE_LEMMINX_BLOB_OBJ}")

tuide_link_embedded_blob(tuide "${TUIDE_LEMMINX_BLOB_OBJ}" TUIDE_HAS_BUNDLED_LEMMINX
  TUIDE_DEFAULT_FORCE_BUNDLED_LEMMINX "${TUIDE_FORCE_BUNDLED_LEMMINX}")
add_dependencies(tuide tuide_lemminx_bundle)
