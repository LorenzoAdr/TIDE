# Enlaza un blob .o embebido al target tgdb (idempotente con libzstd).
function(tgdb_link_embedded_blob target_name blob_obj has_define default_force_define
         force_enabled)
  if(NOT TARGET libzstd_static)
    include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")
  endif()
  target_sources(${target_name} PRIVATE "${blob_obj}")
  target_include_directories(${target_name} PRIVATE "${TGDB_BUNDLED_GEN_DIR}")
  target_compile_definitions(${target_name} PRIVATE ${has_define}=1)
  if(force_enabled)
    target_compile_definitions(${target_name} PRIVATE ${default_force_define}=1)
  endif()
  target_link_libraries(${target_name} PRIVATE libzstd_static)
endfunction()

# add_custom_command estándar para prepare_single_bin_bundle.sh
function(tgdb_add_single_bin_bundle_command prefix)
  set(_script "${CMAKE_SOURCE_DIR}/cmake/prepare_single_bin_bundle.sh")
  add_custom_command(
    OUTPUT "${TGDB_${prefix}_ZST_PATH}" "${TGDB_${prefix}_MANIFEST_HPP}"
           "${TGDB_${prefix}_BLOB_OBJ}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TGDB_${prefix}_STAGING_DIR}"
            "${TGDB_${prefix}_PAYLOAD_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TGDB_${prefix}_STAGING_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E env
            TGDB_TOOL_ID="${TGDB_${prefix}_TOOL_ID}"
            TGDB_TOOL_VERSION="${TGDB_${prefix}_VERSION}"
            TGDB_EXTRACT_MODE="${TGDB_${prefix}_EXTRACT_MODE}"
            TGDB_BINARY_NAME="${TGDB_${prefix}_BINARY_NAME}"
            TGDB_DOWNLOAD_URL="${TGDB_${prefix}_URL}"
            TGDB_DOWNLOAD_PATH="${TGDB_${prefix}_DOWNLOAD_PATH}"
            TGDB_STAGING_DIR="${TGDB_${prefix}_STAGING_DIR}"
            TGDB_PAYLOAD_DIR="${TGDB_${prefix}_PAYLOAD_DIR}"
            TGDB_TAR_PATH_OUT="${TGDB_${prefix}_TAR_PATH}"
            TGDB_ZST_PATH="${TGDB_${prefix}_ZST_PATH}"
            TGDB_MANIFEST_HPP="${TGDB_${prefix}_MANIFEST_HPP}"
            TGDB_BLOB_OBJ="${TGDB_${prefix}_BLOB_OBJ}"
            TGDB_MANIFEST_PREFIX="${TGDB_${prefix}_MANIFEST_PREFIX}"
            bash "${_script}"
    DEPENDS "${_script}"
    COMMENT "Preparing embedded ${TGDB_${prefix}_TOOL_ID} ${TGDB_${prefix}_VERSION}"
    VERBATIM)
endfunction()
