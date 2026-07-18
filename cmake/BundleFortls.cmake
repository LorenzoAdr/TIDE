include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

set(TGDB_FORTLS_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/fortls_staging")
set(TGDB_FORTLS_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/fortls_payload")
set(TGDB_FORTLS_TAR_PATH "${TGDB_BUNDLED_GEN_DIR}/fortls_blob.tar")
set(TGDB_FORTLS_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/fortls_blob.zst")
set(TGDB_FORTLS_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/fortls_blob.o")
set(TGDB_FORTLS_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_fortls_manifest.hpp")

foreach(_tool python3 objcopy zstd sha256sum)
  find_program(_tgdb_fortls_${_tool} ${_tool})
  if(NOT _tgdb_fortls_${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_FORTLS requires '${_tool}' (python3 at bundle time)")
  endif()
endforeach()

set(TGDB_PREPARE_FORTLS_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_fortls_bundle.sh")

add_custom_command(
  OUTPUT "${TGDB_FORTLS_ZST_PATH}" "${TGDB_FORTLS_MANIFEST_HPP}" "${TGDB_FORTLS_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TGDB_FORTLS_STAGING_DIR}" "${TGDB_FORTLS_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TGDB_FORTLS_VERSION="${TGDB_FORTLS_VERSION}"
          TGDB_FORTLS_STAGING_DIR="${TGDB_FORTLS_STAGING_DIR}"
          TGDB_FORTLS_PAYLOAD_DIR="${TGDB_FORTLS_PAYLOAD_DIR}"
          TGDB_FORTLS_TAR_PATH="${TGDB_FORTLS_TAR_PATH}"
          TGDB_FORTLS_ZST_PATH="${TGDB_FORTLS_ZST_PATH}"
          TGDB_FORTLS_MANIFEST_HPP="${TGDB_FORTLS_MANIFEST_HPP}"
          TGDB_FORTLS_BLOB_OBJ="${TGDB_FORTLS_BLOB_OBJ}"
          bash "${TGDB_PREPARE_FORTLS_SCRIPT}"
  DEPENDS "${TGDB_PREPARE_FORTLS_SCRIPT}"
  COMMENT "Preparing embedded fortls ${TGDB_FORTLS_VERSION} (host python3 at runtime)"
  VERBATIM)

add_custom_target(tgdb_fortls_bundle DEPENDS
  "${TGDB_FORTLS_ZST_PATH}" "${TGDB_FORTLS_MANIFEST_HPP}" "${TGDB_FORTLS_BLOB_OBJ}")

tgdb_link_embedded_blob(tgdb "${TGDB_FORTLS_BLOB_OBJ}" TGDB_HAS_BUNDLED_FORTLS
  TGDB_DEFAULT_FORCE_BUNDLED_FORTLS "${TGDB_FORCE_BUNDLED_FORTLS}")
add_dependencies(tgdb tgdb_fortls_bundle)
