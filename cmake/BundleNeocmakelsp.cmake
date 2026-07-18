include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_NEOCMAKELSP_TOOL_ID "neocmakelsp")
set(TUIDE_NEOCMAKELSP_EXTRACT_MODE "tar_binary")
set(TUIDE_NEOCMAKELSP_BINARY_NAME "neocmakelsp")
set(TUIDE_NEOCMAKELSP_DOWNLOAD_NAME "neocmakelsp-x86_64-unknown-linux-gnu.tar.gz")
set(TUIDE_NEOCMAKELSP_URL
    "https://github.com/neocmakelsp/neocmakelsp/releases/download/${TUIDE_NEOCMAKELSP_VERSION}/${TUIDE_NEOCMAKELSP_DOWNLOAD_NAME}")
set(TUIDE_NEOCMAKELSP_DOWNLOAD_PATH
    "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_NEOCMAKELSP_VERSION}-${TUIDE_NEOCMAKELSP_DOWNLOAD_NAME}")
set(TUIDE_NEOCMAKELSP_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/neocmakelsp_staging")
set(TUIDE_NEOCMAKELSP_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/neocmakelsp_payload")
set(TUIDE_NEOCMAKELSP_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/neocmakelsp_blob.tar")
set(TUIDE_NEOCMAKELSP_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/neocmakelsp_blob.zst")
set(TUIDE_NEOCMAKELSP_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/neocmakelsp_blob.o")
set(TUIDE_NEOCMAKELSP_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_neocmakelsp_manifest.hpp")
set(TUIDE_NEOCMAKELSP_MANIFEST_PREFIX "TUIDE_BUNDLED_NEOCMAKELSP")

foreach(_tool objcopy zstd sha256sum)
  find_program(_tuide_neocmakelsp_${_tool} ${_tool})
  if(NOT _tuide_neocmakelsp_${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_NEOCMAKELSP requires '${_tool}'")
  endif()
endforeach()

tuide_add_single_bin_bundle_command(NEOCMAKELSP)

add_custom_target(tuide_neocmakelsp_bundle DEPENDS
  "${TUIDE_NEOCMAKELSP_ZST_PATH}" "${TUIDE_NEOCMAKELSP_MANIFEST_HPP}" "${TUIDE_NEOCMAKELSP_BLOB_OBJ}")

tuide_link_embedded_blob(tuide "${TUIDE_NEOCMAKELSP_BLOB_OBJ}" TUIDE_HAS_BUNDLED_NEOCMAKELSP
  TUIDE_DEFAULT_FORCE_BUNDLED_NEOCMAKELSP "${TUIDE_FORCE_BUNDLED_NEOCMAKELSP}")
add_dependencies(tuide tuide_neocmakelsp_bundle)
