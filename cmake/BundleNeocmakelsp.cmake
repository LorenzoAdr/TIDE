include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

set(TGDB_NEOCMAKELSP_TOOL_ID "neocmakelsp")
set(TGDB_NEOCMAKELSP_EXTRACT_MODE "tar_binary")
set(TGDB_NEOCMAKELSP_BINARY_NAME "neocmakelsp")
set(TGDB_NEOCMAKELSP_DOWNLOAD_NAME "neocmakelsp-x86_64-unknown-linux-gnu.tar.gz")
set(TGDB_NEOCMAKELSP_URL
    "https://github.com/neocmakelsp/neocmakelsp/releases/download/${TGDB_NEOCMAKELSP_VERSION}/${TGDB_NEOCMAKELSP_DOWNLOAD_NAME}")
set(TGDB_NEOCMAKELSP_DOWNLOAD_PATH
    "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_NEOCMAKELSP_VERSION}-${TGDB_NEOCMAKELSP_DOWNLOAD_NAME}")
set(TGDB_NEOCMAKELSP_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/neocmakelsp_staging")
set(TGDB_NEOCMAKELSP_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/neocmakelsp_payload")
set(TGDB_NEOCMAKELSP_TAR_PATH "${TGDB_BUNDLED_GEN_DIR}/neocmakelsp_blob.tar")
set(TGDB_NEOCMAKELSP_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/neocmakelsp_blob.zst")
set(TGDB_NEOCMAKELSP_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/neocmakelsp_blob.o")
set(TGDB_NEOCMAKELSP_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_neocmakelsp_manifest.hpp")
set(TGDB_NEOCMAKELSP_MANIFEST_PREFIX "TGDB_BUNDLED_NEOCMAKELSP")

foreach(_tool objcopy zstd sha256sum)
  find_program(_tgdb_neocmakelsp_${_tool} ${_tool})
  if(NOT _tgdb_neocmakelsp_${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_NEOCMAKELSP requires '${_tool}'")
  endif()
endforeach()

tgdb_add_single_bin_bundle_command(NEOCMAKELSP)

add_custom_target(tgdb_neocmakelsp_bundle DEPENDS
  "${TGDB_NEOCMAKELSP_ZST_PATH}" "${TGDB_NEOCMAKELSP_MANIFEST_HPP}" "${TGDB_NEOCMAKELSP_BLOB_OBJ}")

tgdb_link_embedded_blob(tgdb "${TGDB_NEOCMAKELSP_BLOB_OBJ}" TGDB_HAS_BUNDLED_NEOCMAKELSP
  TGDB_DEFAULT_FORCE_BUNDLED_NEOCMAKELSP "${TGDB_FORCE_BUNDLED_NEOCMAKELSP}")
add_dependencies(tgdb tgdb_neocmakelsp_bundle)
