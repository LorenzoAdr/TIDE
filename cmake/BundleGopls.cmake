include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_GOPLS_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/gopls_staging")
set(TUIDE_GOPLS_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/gopls_payload")
set(TUIDE_GOPLS_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/gopls_blob.tar")
set(TUIDE_GOPLS_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/gopls_blob.zst")
set(TUIDE_GOPLS_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/gopls_blob.o")
set(TUIDE_GOPLS_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_gopls_manifest.hpp")

# Toolchain Go temporal si el host no tiene `go` (solo para construir gopls).
# URL directa a dl.google.com (evita el redirect HTML de go.dev/dl).
set(TUIDE_GO_TAR_NAME "go${TUIDE_GO_VERSION}.linux-amd64.tar.gz")
set(TUIDE_GO_URL "https://dl.google.com/go/${TUIDE_GO_TAR_NAME}")
set(TUIDE_GO_TAR_PATH "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_GO_TAR_NAME}")

foreach(_tool objcopy zstd sha256sum)
  find_program(_tuide_gopls_${_tool} ${_tool})
  if(NOT _tuide_gopls_${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_GOPLS requires '${_tool}'")
  endif()
endforeach()

set(TUIDE_PREPARE_GOPLS_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_gopls_bundle.sh")

add_custom_command(
  OUTPUT "${TUIDE_GOPLS_ZST_PATH}" "${TUIDE_GOPLS_MANIFEST_HPP}" "${TUIDE_GOPLS_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TUIDE_GOPLS_STAGING_DIR}" "${TUIDE_GOPLS_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TUIDE_GOPLS_VERSION="${TUIDE_GOPLS_VERSION}"
          TUIDE_GOPLS_STAGING_DIR="${TUIDE_GOPLS_STAGING_DIR}"
          TUIDE_GOPLS_PAYLOAD_DIR="${TUIDE_GOPLS_PAYLOAD_DIR}"
          TUIDE_GOPLS_TAR_PATH="${TUIDE_GOPLS_TAR_PATH}"
          TUIDE_GOPLS_ZST_PATH="${TUIDE_GOPLS_ZST_PATH}"
          TUIDE_GOPLS_MANIFEST_HPP="${TUIDE_GOPLS_MANIFEST_HPP}"
          TUIDE_GOPLS_BLOB_OBJ="${TUIDE_GOPLS_BLOB_OBJ}"
          TUIDE_BUNDLED_CACHE_DIR="${TUIDE_BUNDLED_CACHE_DIR}"
          TUIDE_GO_VERSION="${TUIDE_GO_VERSION}"
          TUIDE_GO_URL="${TUIDE_GO_URL}"
          TUIDE_GO_TAR_PATH="${TUIDE_GO_TAR_PATH}"
          bash "${TUIDE_PREPARE_GOPLS_SCRIPT}"
  DEPENDS "${TUIDE_PREPARE_GOPLS_SCRIPT}"
  COMMENT "Preparing embedded gopls ${TUIDE_GOPLS_VERSION} (go install; bootstraps Go if needed)"
  VERBATIM)

add_custom_target(tuide_gopls_bundle DEPENDS
  "${TUIDE_GOPLS_ZST_PATH}" "${TUIDE_GOPLS_MANIFEST_HPP}" "${TUIDE_GOPLS_BLOB_OBJ}")

tuide_link_embedded_blob(tuide "${TUIDE_GOPLS_BLOB_OBJ}" TUIDE_HAS_BUNDLED_GOPLS
  TUIDE_DEFAULT_FORCE_BUNDLED_GOPLS "${TUIDE_FORCE_BUNDLED_GOPLS}")
add_dependencies(tuide tuide_gopls_bundle)
