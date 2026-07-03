set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")

file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

if(TGDB_GDB_BUNDLE_KIND STREQUAL "static")
  set(TGDB_GDB_VERSION "${TGDB_GDB_STATIC_VERSION}")
  set(TGDB_GDB_TAR_GZ_NAME "gdb-static-full-x86_64.tar.gz")
  set(TGDB_GDB_TAR_GZ_URL
      "https://github.com/guyush1/gdb-static/releases/download/${TGDB_GDB_VERSION}/${TGDB_GDB_TAR_GZ_NAME}")
  set(TGDB_GDB_TAR_GZ_PATH "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_GDB_VERSION}-${TGDB_GDB_TAR_GZ_NAME}")
  set(TGDB_PREPARE_GDB_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_gdb_static_bundle.sh")
  set(_tgdb_gdb_bundle_deps "${TGDB_PREPARE_GDB_SCRIPT}")
  set(_tgdb_gdb_bundle_comment "Preparing embedded gdb-static ${TGDB_GDB_VERSION}")
elseif(TGDB_GDB_BUNDLE_KIND STREQUAL "core_analyzer")
  set(TGDB_GDB_VERSION "${TGDB_GDB_CA_VERSION}")
  set(TGDB_GDB_TAR_GZ_NAME "gdb-ca-${TGDB_GDB_CA_VERSION}.tar.gz")
  set(TGDB_GDB_TAR_GZ_PATH "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_GDB_TAR_GZ_NAME}")
  set(TGDB_GDB_CA_BUILD_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/build_gdb_core_analyzer.sh")
  set(TGDB_PREPARE_GDB_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_gdb_core_analyzer_bundle.sh")
  set(_tgdb_gdb_bundle_deps "${TGDB_PREPARE_GDB_SCRIPT}" "${TGDB_GDB_CA_BUILD_SCRIPT}")
  set(_tgdb_gdb_bundle_comment "Preparing embedded gdb+core_analyzer ${TGDB_GDB_VERSION}")
else()
  message(FATAL_ERROR "TGDB_GDB_BUNDLE_KIND must be 'static' or 'core_analyzer' (got '${TGDB_GDB_BUNDLE_KIND}')")
endif()

set(TGDB_GDB_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/gdb_staging")
set(TGDB_GDB_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/gdb_payload")
set(TGDB_GDB_TAR_PATH "${TGDB_BUNDLED_GEN_DIR}/gdb_blob.tar")
set(TGDB_GDB_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/gdb_blob.zst")
set(TGDB_GDB_MANIFEST_PATH "${TGDB_BUNDLED_GEN_DIR}/gdb_manifest.json")
set(TGDB_GDB_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/gdb_blob.o")
set(TGDB_GDB_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_gdb_manifest.hpp")

find_program(TGDB_LDD ldd)
find_program(TGDB_OBJCOPY objcopy)
find_program(TGDB_ZSTD zstd)
find_program(TGDB_SHA256SUM sha256sum)
find_program(TGDB_CURL curl)
find_program(TGDB_WGET wget)
foreach(_tool TGDB_LDD TGDB_OBJCOPY TGDB_ZSTD TGDB_SHA256SUM)
  if(NOT ${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_GDB requires '${_tool}' in PATH")
  endif()
endforeach()

if(TGDB_GDB_BUNDLE_KIND STREQUAL "static")
  if(NOT TGDB_CURL AND NOT TGDB_WGET)
    message(FATAL_ERROR "TGDB_BUNDLE_GDB (static) requires curl or wget")
  endif()
endif()

add_custom_command(
  OUTPUT "${TGDB_GDB_ZST_PATH}" "${TGDB_GDB_MANIFEST_PATH}" "${TGDB_GDB_MANIFEST_HPP}"
         "${TGDB_GDB_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TGDB_GDB_STAGING_DIR}" "${TGDB_GDB_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${TGDB_GDB_STAGING_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TGDB_GDB_VERSION="${TGDB_GDB_VERSION}"
          TGDB_GDB_TAR_GZ_PATH="${TGDB_GDB_TAR_GZ_PATH}"
          TGDB_GDB_STAGING_DIR="${TGDB_GDB_STAGING_DIR}"
          TGDB_BUILD_GDB_CA="${TGDB_BUILD_GDB_CA}"
          TGDB_GDB_CA_BUILD_SCRIPT="${TGDB_GDB_CA_BUILD_SCRIPT}"
          TGDB_GDB_TAR_GZ_URL="${TGDB_GDB_TAR_GZ_URL}"
          TGDB_GDB_PAYLOAD_DIR="${TGDB_GDB_PAYLOAD_DIR}"
          TGDB_GDB_TAR_PATH="${TGDB_GDB_TAR_PATH}"
          TGDB_GDB_ZST_PATH="${TGDB_GDB_ZST_PATH}"
          TGDB_GDB_MANIFEST_PATH="${TGDB_GDB_MANIFEST_PATH}"
          TGDB_GDB_MANIFEST_HPP="${TGDB_GDB_MANIFEST_HPP}"
          TGDB_GDB_BLOB_OBJ="${TGDB_GDB_BLOB_OBJ}"
          bash "${TGDB_PREPARE_GDB_SCRIPT}"
  DEPENDS ${_tgdb_gdb_bundle_deps}
  COMMENT "${_tgdb_gdb_bundle_comment}"
  VERBATIM)

add_custom_target(tgdb_gdb_bundle DEPENDS "${TGDB_GDB_ZST_PATH}" "${TGDB_GDB_MANIFEST_HPP}"
                                             "${TGDB_GDB_BLOB_OBJ}")

include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")

target_sources(tgdb PRIVATE "${TGDB_GDB_BLOB_OBJ}")
target_include_directories(tgdb PRIVATE "${TGDB_BUNDLED_GEN_DIR}")
target_compile_definitions(tgdb PRIVATE TGDB_HAS_BUNDLED_GDB=1)
if(TGDB_GDB_BUNDLE_KIND STREQUAL "core_analyzer")
  target_compile_definitions(tgdb PRIVATE TGDB_BUNDLED_GDB_KIND_CORE_ANALYZER=1)
else()
  target_compile_definitions(tgdb PRIVATE TGDB_BUNDLED_GDB_KIND_STATIC=1)
endif()
if(TGDB_FORCE_BUNDLED_GDB)
  target_compile_definitions(tgdb PRIVATE TGDB_DEFAULT_FORCE_BUNDLED_GDB=1)
endif()
target_link_libraries(tgdb PRIVATE libzstd_static)
add_dependencies(tgdb tgdb_gdb_bundle)
