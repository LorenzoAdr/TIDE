set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
set(TUIDE_CLANGD_ZIP_NAME "clangd-linux-${TUIDE_CLANGD_VERSION}.zip")
set(TUIDE_CLANGD_ZIP_URL
    "https://github.com/clangd/clangd/releases/download/${TUIDE_CLANGD_VERSION}/${TUIDE_CLANGD_ZIP_NAME}")

file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_CLANGD_ZIP_PATH "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_CLANGD_ZIP_NAME}")
set(TUIDE_CLANGD_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/clangd_staging")
set(TUIDE_CLANGD_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/clangd_payload")
set(TUIDE_CLANGD_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/clangd_blob.tar")
set(TUIDE_CLANGD_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/clangd_blob.zst")
set(TUIDE_CLANGD_MANIFEST_PATH "${TUIDE_BUNDLED_GEN_DIR}/clangd_manifest.json")
set(TUIDE_CLANGD_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/clangd_blob.o")
set(TUIDE_CLANGD_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_clangd_manifest.hpp")

find_program(TUIDE_UNZIP unzip)
find_program(TUIDE_STRIP strip)
find_program(TUIDE_LDD ldd)
find_program(TUIDE_OBJCOPY objcopy)
find_program(TUIDE_ZSTD zstd)
find_program(TUIDE_SHA256SUM sha256sum)
find_program(TUIDE_CURL curl)
find_program(TUIDE_WGET wget)

foreach(_tool TUIDE_UNZIP TUIDE_STRIP TUIDE_LDD TUIDE_OBJCOPY TUIDE_ZSTD TUIDE_SHA256SUM)
  if(NOT ${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_CLANGD requires '${_tool}' in PATH")
  endif()
endforeach()

if(NOT TUIDE_CURL AND NOT TUIDE_WGET)
  message(FATAL_ERROR "TUIDE_BUNDLE_CLANGD requires curl or wget")
endif()
set(TUIDE_PREPARE_CLANGD_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_clangd_bundle.sh")

add_custom_command(
  OUTPUT "${TUIDE_CLANGD_ZST_PATH}" "${TUIDE_CLANGD_MANIFEST_PATH}"
         "${TUIDE_CLANGD_MANIFEST_HPP}" "${TUIDE_CLANGD_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TUIDE_CLANGD_STAGING_DIR}" "${TUIDE_CLANGD_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${TUIDE_CLANGD_STAGING_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TUIDE_CLANGD_VERSION="${TUIDE_CLANGD_VERSION}"
          TUIDE_CLANGD_ZIP_PATH="${TUIDE_CLANGD_ZIP_PATH}"
          TUIDE_CLANGD_ZIP_URL="${TUIDE_CLANGD_ZIP_URL}"
          TUIDE_CLANGD_STAGING_DIR="${TUIDE_CLANGD_STAGING_DIR}"
          TUIDE_CLANGD_PAYLOAD_DIR="${TUIDE_CLANGD_PAYLOAD_DIR}"
          TUIDE_CLANGD_TAR_PATH="${TUIDE_CLANGD_TAR_PATH}"
          TUIDE_CLANGD_ZST_PATH="${TUIDE_CLANGD_ZST_PATH}"
          TUIDE_CLANGD_MANIFEST_PATH="${TUIDE_CLANGD_MANIFEST_PATH}"
          TUIDE_CLANGD_MANIFEST_HPP="${TUIDE_CLANGD_MANIFEST_HPP}"
          TUIDE_CLANGD_BLOB_OBJ="${TUIDE_CLANGD_BLOB_OBJ}"
          bash "${TUIDE_PREPARE_CLANGD_SCRIPT}"
  DEPENDS "${TUIDE_PREPARE_CLANGD_SCRIPT}"
  COMMENT "Preparing embedded clangd ${TUIDE_CLANGD_VERSION}"
  VERBATIM)

add_custom_target(tuide_clangd_bundle DEPENDS
                  "${TUIDE_CLANGD_ZST_PATH}" "${TUIDE_CLANGD_MANIFEST_HPP}" "${TUIDE_CLANGD_BLOB_OBJ}")

include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")

target_sources(tuide PRIVATE "${TUIDE_CLANGD_BLOB_OBJ}")
target_include_directories(tuide PRIVATE "${TUIDE_BUNDLED_GEN_DIR}")
target_compile_definitions(tuide PRIVATE TUIDE_HAS_BUNDLED_CLANGD=1)
if(TUIDE_FORCE_BUNDLED_CLANGD)
  target_compile_definitions(tuide PRIVATE TUIDE_DEFAULT_FORCE_BUNDLED_CLANGD=1)
endif()
target_link_libraries(tuide PRIVATE libzstd_static)
add_dependencies(tuide tuide_clangd_bundle)
