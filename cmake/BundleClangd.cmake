set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
set(TGDB_CLANGD_ZIP_NAME "clangd-linux-${TGDB_CLANGD_VERSION}.zip")
set(TGDB_CLANGD_ZIP_URL
    "https://github.com/clangd/clangd/releases/download/${TGDB_CLANGD_VERSION}/${TGDB_CLANGD_ZIP_NAME}")

file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

set(TGDB_CLANGD_ZIP_PATH "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_CLANGD_ZIP_NAME}")
set(TGDB_CLANGD_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/clangd_staging")
set(TGDB_CLANGD_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/clangd_payload")
set(TGDB_CLANGD_TAR_PATH "${TGDB_BUNDLED_GEN_DIR}/clangd_blob.tar")
set(TGDB_CLANGD_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/clangd_blob.zst")
set(TGDB_CLANGD_MANIFEST_PATH "${TGDB_BUNDLED_GEN_DIR}/clangd_manifest.json")
set(TGDB_CLANGD_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/clangd_blob.o")
set(TGDB_CLANGD_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_clangd_manifest.hpp")

find_program(TGDB_UNZIP unzip)
find_program(TGDB_STRIP strip)
find_program(TGDB_LDD ldd)
find_program(TGDB_OBJCOPY objcopy)
find_program(TGDB_ZSTD zstd)
find_program(TGDB_SHA256SUM sha256sum)
find_program(TGDB_CURL curl)
find_program(TGDB_WGET wget)

foreach(_tool TGDB_UNZIP TGDB_STRIP TGDB_LDD TGDB_OBJCOPY TGDB_ZSTD TGDB_SHA256SUM)
  if(NOT ${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_CLANGD requires '${_tool}' in PATH")
  endif()
endforeach()

if(NOT TGDB_CURL AND NOT TGDB_WGET)
  message(FATAL_ERROR "TGDB_BUNDLE_CLANGD requires curl or wget")
endif()
set(TGDB_PREPARE_CLANGD_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_clangd_bundle.sh")

add_custom_command(
  OUTPUT "${TGDB_CLANGD_ZST_PATH}" "${TGDB_CLANGD_MANIFEST_PATH}"
         "${TGDB_CLANGD_MANIFEST_HPP}" "${TGDB_CLANGD_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TGDB_CLANGD_STAGING_DIR}" "${TGDB_CLANGD_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${TGDB_CLANGD_STAGING_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TGDB_CLANGD_VERSION="${TGDB_CLANGD_VERSION}"
          TGDB_CLANGD_ZIP_PATH="${TGDB_CLANGD_ZIP_PATH}"
          TGDB_CLANGD_ZIP_URL="${TGDB_CLANGD_ZIP_URL}"
          TGDB_CLANGD_STAGING_DIR="${TGDB_CLANGD_STAGING_DIR}"
          TGDB_CLANGD_PAYLOAD_DIR="${TGDB_CLANGD_PAYLOAD_DIR}"
          TGDB_CLANGD_TAR_PATH="${TGDB_CLANGD_TAR_PATH}"
          TGDB_CLANGD_ZST_PATH="${TGDB_CLANGD_ZST_PATH}"
          TGDB_CLANGD_MANIFEST_PATH="${TGDB_CLANGD_MANIFEST_PATH}"
          TGDB_CLANGD_MANIFEST_HPP="${TGDB_CLANGD_MANIFEST_HPP}"
          TGDB_CLANGD_BLOB_OBJ="${TGDB_CLANGD_BLOB_OBJ}"
          bash "${TGDB_PREPARE_CLANGD_SCRIPT}"
  DEPENDS "${TGDB_PREPARE_CLANGD_SCRIPT}"
  COMMENT "Preparing embedded clangd ${TGDB_CLANGD_VERSION}"
  VERBATIM)

add_custom_target(tgdb_clangd_bundle DEPENDS
                  "${TGDB_CLANGD_ZST_PATH}" "${TGDB_CLANGD_MANIFEST_HPP}" "${TGDB_CLANGD_BLOB_OBJ}")

include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")

target_sources(tgdb PRIVATE "${TGDB_CLANGD_BLOB_OBJ}")
target_include_directories(tgdb PRIVATE "${TGDB_BUNDLED_GEN_DIR}")
target_compile_definitions(tgdb PRIVATE TGDB_HAS_BUNDLED_CLANGD=1)
if(TGDB_FORCE_BUNDLED_CLANGD)
  target_compile_definitions(tgdb PRIVATE TGDB_DEFAULT_FORCE_BUNDLED_CLANGD=1)
endif()
target_link_libraries(tgdb PRIVATE libzstd_static)
add_dependencies(tgdb tgdb_clangd_bundle)
