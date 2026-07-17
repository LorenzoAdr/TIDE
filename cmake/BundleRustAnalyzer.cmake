include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

set(TGDB_RUST_ANALYZER_TOOL_ID "rust_analyzer")
set(TGDB_RUST_ANALYZER_EXTRACT_MODE "gunzip")
set(TGDB_RUST_ANALYZER_BINARY_NAME "rust-analyzer")
set(TGDB_RUST_ANALYZER_DOWNLOAD_NAME
    "rust-analyzer-x86_64-unknown-linux-gnu.gz")
set(TGDB_RUST_ANALYZER_URL
    "https://github.com/rust-lang/rust-analyzer/releases/download/${TGDB_RUST_ANALYZER_VERSION}/${TGDB_RUST_ANALYZER_DOWNLOAD_NAME}")
set(TGDB_RUST_ANALYZER_DOWNLOAD_PATH
    "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_RUST_ANALYZER_VERSION}-${TGDB_RUST_ANALYZER_DOWNLOAD_NAME}")
set(TGDB_RUST_ANALYZER_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/rust_analyzer_staging")
set(TGDB_RUST_ANALYZER_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/rust_analyzer_payload")
set(TGDB_RUST_ANALYZER_TAR_PATH "${TGDB_BUNDLED_GEN_DIR}/rust_analyzer_blob.tar")
set(TGDB_RUST_ANALYZER_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/rust_analyzer_blob.zst")
set(TGDB_RUST_ANALYZER_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/rust_analyzer_blob.o")
set(TGDB_RUST_ANALYZER_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_rust_analyzer_manifest.hpp")
set(TGDB_RUST_ANALYZER_MANIFEST_PREFIX "TGDB_BUNDLED_RUST_ANALYZER")

foreach(_tool objcopy zstd sha256sum)
  find_program(_tgdb_ra_${_tool} ${_tool})
  if(NOT _tgdb_ra_${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_RUST_ANALYZER requires '${_tool}'")
  endif()
endforeach()

tgdb_add_single_bin_bundle_command(RUST_ANALYZER)

add_custom_target(tgdb_rust_analyzer_bundle DEPENDS
  "${TGDB_RUST_ANALYZER_ZST_PATH}" "${TGDB_RUST_ANALYZER_MANIFEST_HPP}"
  "${TGDB_RUST_ANALYZER_BLOB_OBJ}")

tgdb_link_embedded_blob(tgdb "${TGDB_RUST_ANALYZER_BLOB_OBJ}" TGDB_HAS_BUNDLED_RUST_ANALYZER
  TGDB_DEFAULT_FORCE_BUNDLED_RUST_ANALYZER "${TGDB_FORCE_BUNDLED_RUST_ANALYZER}")
add_dependencies(tgdb tgdb_rust_analyzer_bundle)
