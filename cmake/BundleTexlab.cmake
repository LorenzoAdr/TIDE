set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

set(TGDB_TEXLAB_TAR_NAME "texlab-x86_64-linux.tar.gz")
set(TGDB_TEXLAB_URL
    "https://github.com/latex-lsp/texlab/releases/download/v${TGDB_TEXLAB_VERSION}/${TGDB_TEXLAB_TAR_NAME}")
set(TGDB_TEXLAB_TAR_PATH "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_TEXLAB_TAR_NAME}")
set(TGDB_TEXLAB_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/texlab_staging")
set(TGDB_TEXLAB_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/texlab_payload")
set(TGDB_TEXLAB_TAR_PATH_OUT "${TGDB_BUNDLED_GEN_DIR}/texlab_blob.tar")
set(TGDB_TEXLAB_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/texlab_blob.zst")
set(TGDB_TEXLAB_MANIFEST_PATH "${TGDB_BUNDLED_GEN_DIR}/texlab_manifest.json")
set(TGDB_TEXLAB_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/texlab_blob.o")
set(TGDB_TEXLAB_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_texlab_manifest.hpp")

find_program(TGDB_OBJCOPY objcopy)
find_program(TGDB_ZSTD zstd)
find_program(TGDB_SHA256SUM sha256sum)
foreach(_tool TGDB_OBJCOPY TGDB_ZSTD TGDB_SHA256SUM)
  if(NOT ${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_TEXLAB requires '${_tool}'")
  endif()
endforeach()

set(TGDB_PREPARE_TEXLAB_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_texlab_bundle.sh")

add_custom_command(
  OUTPUT "${TGDB_TEXLAB_ZST_PATH}" "${TGDB_TEXLAB_MANIFEST_HPP}" "${TGDB_TEXLAB_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TGDB_TEXLAB_STAGING_DIR}" "${TGDB_TEXLAB_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${TGDB_TEXLAB_STAGING_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TGDB_TEXLAB_VERSION="${TGDB_TEXLAB_VERSION}"
          TGDB_TEXLAB_URL="${TGDB_TEXLAB_URL}"
          TGDB_TEXLAB_TAR_PATH="${TGDB_TEXLAB_TAR_PATH}"
          TGDB_TEXLAB_STAGING_DIR="${TGDB_TEXLAB_STAGING_DIR}"
          TGDB_TEXLAB_PAYLOAD_DIR="${TGDB_TEXLAB_PAYLOAD_DIR}"
          TGDB_TEXLAB_TAR_PATH_OUT="${TGDB_TEXLAB_TAR_PATH_OUT}"
          TGDB_TEXLAB_ZST_PATH="${TGDB_TEXLAB_ZST_PATH}"
          TGDB_TEXLAB_MANIFEST_PATH="${TGDB_TEXLAB_MANIFEST_PATH}"
          TGDB_TEXLAB_MANIFEST_HPP="${TGDB_TEXLAB_MANIFEST_HPP}"
          TGDB_TEXLAB_BLOB_OBJ="${TGDB_TEXLAB_BLOB_OBJ}"
          bash "${TGDB_PREPARE_TEXLAB_SCRIPT}"
  DEPENDS "${TGDB_PREPARE_TEXLAB_SCRIPT}"
  COMMENT "Preparing embedded TexLab ${TGDB_TEXLAB_VERSION}"
  VERBATIM)

add_custom_target(tgdb_texlab_bundle DEPENDS
  "${TGDB_TEXLAB_ZST_PATH}" "${TGDB_TEXLAB_MANIFEST_HPP}" "${TGDB_TEXLAB_BLOB_OBJ}")

include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")
target_sources(tgdb PRIVATE "${TGDB_TEXLAB_BLOB_OBJ}")
target_include_directories(tgdb PRIVATE "${TGDB_BUNDLED_GEN_DIR}")
target_compile_definitions(tgdb PRIVATE TGDB_HAS_BUNDLED_TEXLAB=1)
if(TGDB_FORCE_BUNDLED_TEXLAB)
  target_compile_definitions(tgdb PRIVATE TGDB_DEFAULT_FORCE_BUNDLED_TEXLAB=1)
endif()
target_link_libraries(tgdb PRIVATE libzstd_static)
add_dependencies(tgdb tgdb_texlab_bundle)
