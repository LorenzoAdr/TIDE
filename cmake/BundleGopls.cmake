include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

set(TGDB_GOPLS_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/gopls_staging")
set(TGDB_GOPLS_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/gopls_payload")
set(TGDB_GOPLS_TAR_PATH "${TGDB_BUNDLED_GEN_DIR}/gopls_blob.tar")
set(TGDB_GOPLS_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/gopls_blob.zst")
set(TGDB_GOPLS_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/gopls_blob.o")
set(TGDB_GOPLS_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_gopls_manifest.hpp")

# Toolchain Go temporal si el host no tiene `go` (solo para construir gopls).
# URL directa a dl.google.com (evita el redirect HTML de go.dev/dl).
set(TGDB_GO_TAR_NAME "go${TGDB_GO_VERSION}.linux-amd64.tar.gz")
set(TGDB_GO_URL "https://dl.google.com/go/${TGDB_GO_TAR_NAME}")
set(TGDB_GO_TAR_PATH "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_GO_TAR_NAME}")

foreach(_tool objcopy zstd sha256sum)
  find_program(_tgdb_gopls_${_tool} ${_tool})
  if(NOT _tgdb_gopls_${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_GOPLS requires '${_tool}'")
  endif()
endforeach()

set(TGDB_PREPARE_GOPLS_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_gopls_bundle.sh")

add_custom_command(
  OUTPUT "${TGDB_GOPLS_ZST_PATH}" "${TGDB_GOPLS_MANIFEST_HPP}" "${TGDB_GOPLS_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TGDB_GOPLS_STAGING_DIR}" "${TGDB_GOPLS_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TGDB_GOPLS_VERSION="${TGDB_GOPLS_VERSION}"
          TGDB_GOPLS_STAGING_DIR="${TGDB_GOPLS_STAGING_DIR}"
          TGDB_GOPLS_PAYLOAD_DIR="${TGDB_GOPLS_PAYLOAD_DIR}"
          TGDB_GOPLS_TAR_PATH="${TGDB_GOPLS_TAR_PATH}"
          TGDB_GOPLS_ZST_PATH="${TGDB_GOPLS_ZST_PATH}"
          TGDB_GOPLS_MANIFEST_HPP="${TGDB_GOPLS_MANIFEST_HPP}"
          TGDB_GOPLS_BLOB_OBJ="${TGDB_GOPLS_BLOB_OBJ}"
          TGDB_BUNDLED_CACHE_DIR="${TGDB_BUNDLED_CACHE_DIR}"
          TGDB_GO_VERSION="${TGDB_GO_VERSION}"
          TGDB_GO_URL="${TGDB_GO_URL}"
          TGDB_GO_TAR_PATH="${TGDB_GO_TAR_PATH}"
          bash "${TGDB_PREPARE_GOPLS_SCRIPT}"
  DEPENDS "${TGDB_PREPARE_GOPLS_SCRIPT}"
  COMMENT "Preparing embedded gopls ${TGDB_GOPLS_VERSION} (go install; bootstraps Go if needed)"
  VERBATIM)

add_custom_target(tgdb_gopls_bundle DEPENDS
  "${TGDB_GOPLS_ZST_PATH}" "${TGDB_GOPLS_MANIFEST_HPP}" "${TGDB_GOPLS_BLOB_OBJ}")

tgdb_link_embedded_blob(tgdb "${TGDB_GOPLS_BLOB_OBJ}" TGDB_HAS_BUNDLED_GOPLS
  TGDB_DEFAULT_FORCE_BUNDLED_GOPLS "${TGDB_FORCE_BUNDLED_GOPLS}")
add_dependencies(tgdb tgdb_gopls_bundle)
