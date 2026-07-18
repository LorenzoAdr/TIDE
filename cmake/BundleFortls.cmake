include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_FORTLS_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/fortls_staging")
set(TUIDE_FORTLS_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/fortls_payload")
set(TUIDE_FORTLS_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/fortls_blob.tar")
set(TUIDE_FORTLS_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/fortls_blob.zst")
set(TUIDE_FORTLS_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/fortls_blob.o")
set(TUIDE_FORTLS_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_fortls_manifest.hpp")

foreach(_tool python3 objcopy zstd sha256sum)
  find_program(_tuide_fortls_${_tool} ${_tool})
  if(NOT _tuide_fortls_${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_FORTLS requires '${_tool}' (python3 at bundle time)")
  endif()
endforeach()

set(TUIDE_PREPARE_FORTLS_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_fortls_bundle.sh")

add_custom_command(
  OUTPUT "${TUIDE_FORTLS_ZST_PATH}" "${TUIDE_FORTLS_MANIFEST_HPP}" "${TUIDE_FORTLS_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TUIDE_FORTLS_STAGING_DIR}" "${TUIDE_FORTLS_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TUIDE_FORTLS_VERSION="${TUIDE_FORTLS_VERSION}"
          TUIDE_FORTLS_STAGING_DIR="${TUIDE_FORTLS_STAGING_DIR}"
          TUIDE_FORTLS_PAYLOAD_DIR="${TUIDE_FORTLS_PAYLOAD_DIR}"
          TUIDE_FORTLS_TAR_PATH="${TUIDE_FORTLS_TAR_PATH}"
          TUIDE_FORTLS_ZST_PATH="${TUIDE_FORTLS_ZST_PATH}"
          TUIDE_FORTLS_MANIFEST_HPP="${TUIDE_FORTLS_MANIFEST_HPP}"
          TUIDE_FORTLS_BLOB_OBJ="${TUIDE_FORTLS_BLOB_OBJ}"
          bash "${TUIDE_PREPARE_FORTLS_SCRIPT}"
  DEPENDS "${TUIDE_PREPARE_FORTLS_SCRIPT}"
  COMMENT "Preparing embedded fortls ${TUIDE_FORTLS_VERSION} (host python3 at runtime)"
  VERBATIM)

add_custom_target(tuide_fortls_bundle DEPENDS
  "${TUIDE_FORTLS_ZST_PATH}" "${TUIDE_FORTLS_MANIFEST_HPP}" "${TUIDE_FORTLS_BLOB_OBJ}")

tuide_link_embedded_blob(tuide "${TUIDE_FORTLS_BLOB_OBJ}" TUIDE_HAS_BUNDLED_FORTLS
  TUIDE_DEFAULT_FORCE_BUNDLED_FORTLS "${TUIDE_FORCE_BUNDLED_FORTLS}")
add_dependencies(tuide tuide_fortls_bundle)
