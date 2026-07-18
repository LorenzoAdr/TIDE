set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")

file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

if(TUIDE_GDB_BUNDLE_KIND STREQUAL "static")
  set(TUIDE_GDB_VERSION "${TUIDE_GDB_STATIC_VERSION}")
  set(TUIDE_GDB_TAR_GZ_NAME "gdb-static-full-x86_64.tar.gz")
  set(TUIDE_GDB_TAR_GZ_URL
      "https://github.com/guyush1/gdb-static/releases/download/${TUIDE_GDB_VERSION}/${TUIDE_GDB_TAR_GZ_NAME}")
  set(TUIDE_GDB_TAR_GZ_PATH "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_GDB_VERSION}-${TUIDE_GDB_TAR_GZ_NAME}")
  set(TUIDE_PREPARE_GDB_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_gdb_static_bundle.sh")
  set(_tuide_gdb_bundle_deps "${TUIDE_PREPARE_GDB_SCRIPT}")
  set(_tuide_gdb_bundle_comment "Preparing embedded gdb-static ${TUIDE_GDB_VERSION}")
elseif(TUIDE_GDB_BUNDLE_KIND STREQUAL "core_analyzer")
  set(TUIDE_GDB_VERSION "${TUIDE_GDB_CA_VERSION}")
  set(TUIDE_GDB_TAR_GZ_NAME "gdb-ca-${TUIDE_GDB_CA_VERSION}.tar.gz")
  set(TUIDE_GDB_TAR_GZ_PATH "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_GDB_TAR_GZ_NAME}")
  set(TUIDE_GDB_CA_BUILD_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/build_gdb_core_analyzer.sh")
  set(TUIDE_PREPARE_GDB_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/prepare_gdb_core_analyzer_bundle.sh")
  set(_tuide_gdb_bundle_deps "${TUIDE_PREPARE_GDB_SCRIPT}" "${TUIDE_GDB_CA_BUILD_SCRIPT}")
  set(_tuide_gdb_bundle_comment "Preparing embedded gdb+core_analyzer ${TUIDE_GDB_VERSION}")
else()
  message(FATAL_ERROR "TUIDE_GDB_BUNDLE_KIND must be 'static' or 'core_analyzer' (got '${TUIDE_GDB_BUNDLE_KIND}')")
endif()

set(TUIDE_GDB_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/gdb_staging")
set(TUIDE_GDB_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/gdb_payload")
set(TUIDE_GDB_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/gdb_blob.tar")
set(TUIDE_GDB_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/gdb_blob.zst")
set(TUIDE_GDB_MANIFEST_PATH "${TUIDE_BUNDLED_GEN_DIR}/gdb_manifest.json")
set(TUIDE_GDB_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/gdb_blob.o")
set(TUIDE_GDB_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_gdb_manifest.hpp")

find_program(TUIDE_LDD ldd)
find_program(TUIDE_OBJCOPY objcopy)
find_program(TUIDE_ZSTD zstd)
find_program(TUIDE_SHA256SUM sha256sum)
find_program(TUIDE_CURL curl)
find_program(TUIDE_WGET wget)
foreach(_tool TUIDE_LDD TUIDE_OBJCOPY TUIDE_ZSTD TUIDE_SHA256SUM)
  if(NOT ${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_GDB requires '${_tool}' in PATH")
  endif()
endforeach()

if(TUIDE_GDB_BUNDLE_KIND STREQUAL "static")
  if(NOT TUIDE_CURL AND NOT TUIDE_WGET)
    message(FATAL_ERROR "TUIDE_BUNDLE_GDB (static) requires curl or wget")
  endif()
endif()

add_custom_command(
  OUTPUT "${TUIDE_GDB_ZST_PATH}" "${TUIDE_GDB_MANIFEST_PATH}" "${TUIDE_GDB_MANIFEST_HPP}"
         "${TUIDE_GDB_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TUIDE_GDB_STAGING_DIR}" "${TUIDE_GDB_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${TUIDE_GDB_STAGING_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TUIDE_GDB_VERSION="${TUIDE_GDB_VERSION}"
          TUIDE_GDB_TAR_GZ_PATH="${TUIDE_GDB_TAR_GZ_PATH}"
          TUIDE_GDB_STAGING_DIR="${TUIDE_GDB_STAGING_DIR}"
          TUIDE_BUILD_GDB_CA="${TUIDE_BUILD_GDB_CA}"
          TUIDE_GDB_CA_BUILD_SCRIPT="${TUIDE_GDB_CA_BUILD_SCRIPT}"
          TUIDE_GDB_TAR_GZ_URL="${TUIDE_GDB_TAR_GZ_URL}"
          TUIDE_GDB_PAYLOAD_DIR="${TUIDE_GDB_PAYLOAD_DIR}"
          TUIDE_GDB_TAR_PATH="${TUIDE_GDB_TAR_PATH}"
          TUIDE_GDB_ZST_PATH="${TUIDE_GDB_ZST_PATH}"
          TUIDE_GDB_MANIFEST_PATH="${TUIDE_GDB_MANIFEST_PATH}"
          TUIDE_GDB_MANIFEST_HPP="${TUIDE_GDB_MANIFEST_HPP}"
          TUIDE_GDB_BLOB_OBJ="${TUIDE_GDB_BLOB_OBJ}"
          bash "${TUIDE_PREPARE_GDB_SCRIPT}"
  DEPENDS ${_tuide_gdb_bundle_deps}
  COMMENT "${_tuide_gdb_bundle_comment}"
  VERBATIM)

add_custom_target(tuide_gdb_bundle DEPENDS "${TUIDE_GDB_ZST_PATH}" "${TUIDE_GDB_MANIFEST_HPP}"
                                             "${TUIDE_GDB_BLOB_OBJ}")

include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")

target_sources(tuide PRIVATE "${TUIDE_GDB_BLOB_OBJ}")
target_include_directories(tuide PRIVATE "${TUIDE_BUNDLED_GEN_DIR}")
target_compile_definitions(tuide PRIVATE TUIDE_HAS_BUNDLED_GDB=1)
if(TUIDE_GDB_BUNDLE_KIND STREQUAL "core_analyzer")
  target_compile_definitions(tuide PRIVATE TUIDE_BUNDLED_GDB_KIND_CORE_ANALYZER=1)
else()
  target_compile_definitions(tuide PRIVATE TUIDE_BUNDLED_GDB_KIND_STATIC=1)
endif()
if(TUIDE_FORCE_BUNDLED_GDB)
  target_compile_definitions(tuide PRIVATE TUIDE_DEFAULT_FORCE_BUNDLED_GDB=1)
endif()
target_link_libraries(tuide PRIVATE libzstd_static)
add_dependencies(tuide tuide_gdb_bundle)
