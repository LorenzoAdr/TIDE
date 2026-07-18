include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_RUST_ANALYZER_TOOL_ID "rust_analyzer")
set(TUIDE_RUST_ANALYZER_EXTRACT_MODE "gunzip")
set(TUIDE_RUST_ANALYZER_BINARY_NAME "rust-analyzer")
set(TUIDE_RUST_ANALYZER_DOWNLOAD_NAME
    "rust-analyzer-x86_64-unknown-linux-gnu.gz")
set(TUIDE_RUST_ANALYZER_URL
    "https://github.com/rust-lang/rust-analyzer/releases/download/${TUIDE_RUST_ANALYZER_VERSION}/${TUIDE_RUST_ANALYZER_DOWNLOAD_NAME}")
set(TUIDE_RUST_ANALYZER_DOWNLOAD_PATH
    "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_RUST_ANALYZER_VERSION}-${TUIDE_RUST_ANALYZER_DOWNLOAD_NAME}")
set(TUIDE_RUST_ANALYZER_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/rust_analyzer_staging")
set(TUIDE_RUST_ANALYZER_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/rust_analyzer_payload")
set(TUIDE_RUST_ANALYZER_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/rust_analyzer_blob.tar")
set(TUIDE_RUST_ANALYZER_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/rust_analyzer_blob.zst")
set(TUIDE_RUST_ANALYZER_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/rust_analyzer_blob.o")
set(TUIDE_RUST_ANALYZER_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_rust_analyzer_manifest.hpp")
set(TUIDE_RUST_ANALYZER_MANIFEST_PREFIX "TUIDE_BUNDLED_RUST_ANALYZER")

foreach(_tool objcopy zstd sha256sum)
  find_program(_tuide_ra_${_tool} ${_tool})
  if(NOT _tuide_ra_${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_RUST_ANALYZER requires '${_tool}'")
  endif()
endforeach()

tuide_add_single_bin_bundle_command(RUST_ANALYZER)

add_custom_target(tuide_rust_analyzer_bundle DEPENDS
  "${TUIDE_RUST_ANALYZER_ZST_PATH}" "${TUIDE_RUST_ANALYZER_MANIFEST_HPP}"
  "${TUIDE_RUST_ANALYZER_BLOB_OBJ}")

tuide_link_embedded_blob(tuide "${TUIDE_RUST_ANALYZER_BLOB_OBJ}" TUIDE_HAS_BUNDLED_RUST_ANALYZER
  TUIDE_DEFAULT_FORCE_BUNDLED_RUST_ANALYZER "${TUIDE_FORCE_BUNDLED_RUST_ANALYZER}")
add_dependencies(tuide tuide_rust_analyzer_bundle)
