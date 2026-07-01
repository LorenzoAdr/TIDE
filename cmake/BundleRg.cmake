set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
set(TGDB_RG_TAR_GZ_NAME "ripgrep-${TGDB_RG_VERSION}-x86_64-unknown-linux-musl.tar.gz")
set(TGDB_RG_TAR_GZ_URL
    "https://github.com/BurntSushi/ripgrep/releases/download/${TGDB_RG_VERSION}/${TGDB_RG_TAR_GZ_NAME}")

file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

set(TGDB_RG_TAR_GZ_PATH "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_RG_TAR_GZ_NAME}")
set(TGDB_RG_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/rg_staging")
set(TGDB_RG_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/rg_payload")
set(TGDB_RG_TAR_PATH "${TGDB_BUNDLED_GEN_DIR}/rg_blob.tar")
set(TGDB_RG_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/rg_blob.zst")
set(TGDB_RG_MANIFEST_PATH "${TGDB_BUNDLED_GEN_DIR}/rg_manifest.json")
set(TGDB_RG_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/rg_blob.o")
set(TGDB_RG_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_rg_manifest.hpp")

find_program(TGDB_LDD ldd)
find_program(TGDB_OBJCOPY objcopy)
find_program(TGDB_ZSTD zstd)
find_program(TGDB_SHA256SUM sha256sum)
find_program(TGDB_CURL curl)
find_program(TGDB_WGET wget)

foreach(_tool TGDB_LDD TGDB_OBJCOPY TGDB_ZSTD TGDB_SHA256SUM)
  if(NOT ${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_RG requires '${_tool}' in PATH")
  endif()
endforeach()

if(NOT TGDB_CURL AND NOT TGDB_WGET)
  message(FATAL_ERROR "TGDB_BUNDLE_RG requires curl or wget")
endif()

set(TGDB_PREPARE_RG_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_rg_bundle.sh")

add_custom_command(
  OUTPUT "${TGDB_RG_ZST_PATH}" "${TGDB_RG_MANIFEST_PATH}" "${TGDB_RG_MANIFEST_HPP}"
         "${TGDB_RG_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TGDB_RG_STAGING_DIR}" "${TGDB_RG_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${TGDB_RG_STAGING_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TGDB_RG_VERSION="${TGDB_RG_VERSION}"
          TGDB_RG_TAR_GZ_PATH="${TGDB_RG_TAR_GZ_PATH}"
          TGDB_RG_TAR_GZ_URL="${TGDB_RG_TAR_GZ_URL}"
          TGDB_RG_STAGING_DIR="${TGDB_RG_STAGING_DIR}"
          TGDB_RG_PAYLOAD_DIR="${TGDB_RG_PAYLOAD_DIR}"
          TGDB_RG_TAR_PATH="${TGDB_RG_TAR_PATH}"
          TGDB_RG_ZST_PATH="${TGDB_RG_ZST_PATH}"
          TGDB_RG_MANIFEST_PATH="${TGDB_RG_MANIFEST_PATH}"
          TGDB_RG_MANIFEST_HPP="${TGDB_RG_MANIFEST_HPP}"
          TGDB_RG_BLOB_OBJ="${TGDB_RG_BLOB_OBJ}"
          bash "${TGDB_PREPARE_RG_SCRIPT}"
  DEPENDS "${TGDB_PREPARE_RG_SCRIPT}"
  COMMENT "Preparing embedded ripgrep ${TGDB_RG_VERSION}"
  VERBATIM)

add_custom_target(tgdb_rg_bundle DEPENDS "${TGDB_RG_ZST_PATH}" "${TGDB_RG_MANIFEST_HPP}"
                                            "${TGDB_RG_BLOB_OBJ}")

if(NOT TARGET libzstd_static)
  include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")
endif()

target_sources(tgdb PRIVATE "${TGDB_RG_BLOB_OBJ}")
target_include_directories(tgdb PRIVATE "${TGDB_BUNDLED_GEN_DIR}")
target_compile_definitions(tgdb PRIVATE TGDB_HAS_BUNDLED_RG=1)
if(TGDB_FORCE_BUNDLED_RG)
  target_compile_definitions(tgdb PRIVATE TGDB_DEFAULT_FORCE_BUNDLED_RG=1)
endif()
target_link_libraries(tgdb PRIVATE libzstd_static)
add_dependencies(tgdb tgdb_rg_bundle)
