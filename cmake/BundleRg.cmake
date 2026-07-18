set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
set(TUIDE_RG_TAR_GZ_NAME "ripgrep-${TUIDE_RG_VERSION}-x86_64-unknown-linux-musl.tar.gz")
set(TUIDE_RG_TAR_GZ_URL
    "https://github.com/BurntSushi/ripgrep/releases/download/${TUIDE_RG_VERSION}/${TUIDE_RG_TAR_GZ_NAME}")

file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_RG_TAR_GZ_PATH "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_RG_TAR_GZ_NAME}")
set(TUIDE_RG_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/rg_staging")
set(TUIDE_RG_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/rg_payload")
set(TUIDE_RG_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/rg_blob.tar")
set(TUIDE_RG_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/rg_blob.zst")
set(TUIDE_RG_MANIFEST_PATH "${TUIDE_BUNDLED_GEN_DIR}/rg_manifest.json")
set(TUIDE_RG_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/rg_blob.o")
set(TUIDE_RG_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_rg_manifest.hpp")

find_program(TUIDE_LDD ldd)
find_program(TUIDE_OBJCOPY objcopy)
find_program(TUIDE_ZSTD zstd)
find_program(TUIDE_SHA256SUM sha256sum)
find_program(TUIDE_CURL curl)
find_program(TUIDE_WGET wget)

foreach(_tool TUIDE_LDD TUIDE_OBJCOPY TUIDE_ZSTD TUIDE_SHA256SUM)
  if(NOT ${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_RG requires '${_tool}' in PATH")
  endif()
endforeach()

if(NOT TUIDE_CURL AND NOT TUIDE_WGET)
  message(FATAL_ERROR "TUIDE_BUNDLE_RG requires curl or wget")
endif()

set(TUIDE_PREPARE_RG_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_rg_bundle.sh")

add_custom_command(
  OUTPUT "${TUIDE_RG_ZST_PATH}" "${TUIDE_RG_MANIFEST_PATH}" "${TUIDE_RG_MANIFEST_HPP}"
         "${TUIDE_RG_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TUIDE_RG_STAGING_DIR}" "${TUIDE_RG_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${TUIDE_RG_STAGING_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TUIDE_RG_VERSION="${TUIDE_RG_VERSION}"
          TUIDE_RG_TAR_GZ_PATH="${TUIDE_RG_TAR_GZ_PATH}"
          TUIDE_RG_TAR_GZ_URL="${TUIDE_RG_TAR_GZ_URL}"
          TUIDE_RG_STAGING_DIR="${TUIDE_RG_STAGING_DIR}"
          TUIDE_RG_PAYLOAD_DIR="${TUIDE_RG_PAYLOAD_DIR}"
          TUIDE_RG_TAR_PATH="${TUIDE_RG_TAR_PATH}"
          TUIDE_RG_ZST_PATH="${TUIDE_RG_ZST_PATH}"
          TUIDE_RG_MANIFEST_PATH="${TUIDE_RG_MANIFEST_PATH}"
          TUIDE_RG_MANIFEST_HPP="${TUIDE_RG_MANIFEST_HPP}"
          TUIDE_RG_BLOB_OBJ="${TUIDE_RG_BLOB_OBJ}"
          bash "${TUIDE_PREPARE_RG_SCRIPT}"
  DEPENDS "${TUIDE_PREPARE_RG_SCRIPT}"
  COMMENT "Preparing embedded ripgrep ${TUIDE_RG_VERSION}"
  VERBATIM)

add_custom_target(tuide_rg_bundle DEPENDS "${TUIDE_RG_ZST_PATH}" "${TUIDE_RG_MANIFEST_HPP}"
                                            "${TUIDE_RG_BLOB_OBJ}")

if(NOT TARGET libzstd_static)
  include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")
endif()

target_sources(tuide PRIVATE "${TUIDE_RG_BLOB_OBJ}")
target_include_directories(tuide PRIVATE "${TUIDE_BUNDLED_GEN_DIR}")
target_compile_definitions(tuide PRIVATE TUIDE_HAS_BUNDLED_RG=1)
if(TUIDE_FORCE_BUNDLED_RG)
  target_compile_definitions(tuide PRIVATE TUIDE_DEFAULT_FORCE_BUNDLED_RG=1)
endif()
target_link_libraries(tuide PRIVATE libzstd_static)
add_dependencies(tuide tuide_rg_bundle)
