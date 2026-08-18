# Enlaza un blob .o embebido al target tuide (idempotente con libzstd).
function(tuide_link_embedded_blob target_name blob_obj has_define default_force_define
         force_enabled)
  if(NOT TARGET libzstd_static)
    include("${CMAKE_SOURCE_DIR}/cmake/BundleZstd.cmake")
  endif()
  target_sources(${target_name} PRIVATE "${blob_obj}")
  target_include_directories(${target_name} PRIVATE "${TUIDE_BUNDLED_GEN_DIR}")
  target_compile_definitions(${target_name} PRIVATE ${has_define}=1)
  if(force_enabled)
    target_compile_definitions(${target_name} PRIVATE ${default_force_define}=1)
  endif()
  target_link_libraries(${target_name} PRIVATE libzstd_static)
endfunction()

# add_custom_command estándar para prepare_single_bin_bundle.sh
function(tuide_add_single_bin_bundle_command prefix)
  set(_script "${CMAKE_SOURCE_DIR}/cmake/prepare_single_bin_bundle.sh")
  if(DEFINED TUIDE_${prefix}_ARCHIVE_BINARY_NAME)
    set(_archive_binary_name "${TUIDE_${prefix}_ARCHIVE_BINARY_NAME}")
  else()
    set(_archive_binary_name "${TUIDE_${prefix}_BINARY_NAME}")
  endif()
  add_custom_command(
    OUTPUT "${TUIDE_${prefix}_ZST_PATH}" "${TUIDE_${prefix}_MANIFEST_HPP}"
           "${TUIDE_${prefix}_BLOB_OBJ}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TUIDE_${prefix}_STAGING_DIR}"
            "${TUIDE_${prefix}_PAYLOAD_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TUIDE_${prefix}_STAGING_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E env
            TUIDE_TOOL_ID="${TUIDE_${prefix}_TOOL_ID}"
            TUIDE_TOOL_VERSION="${TUIDE_${prefix}_VERSION}"
            TUIDE_EXTRACT_MODE="${TUIDE_${prefix}_EXTRACT_MODE}"
            TUIDE_BINARY_NAME="${TUIDE_${prefix}_BINARY_NAME}"
            TUIDE_ARCHIVE_BINARY_NAME="${_archive_binary_name}"
            TUIDE_DOWNLOAD_URL="${TUIDE_${prefix}_URL}"
            TUIDE_DOWNLOAD_PATH="${TUIDE_${prefix}_DOWNLOAD_PATH}"
            TUIDE_STAGING_DIR="${TUIDE_${prefix}_STAGING_DIR}"
            TUIDE_PAYLOAD_DIR="${TUIDE_${prefix}_PAYLOAD_DIR}"
            TUIDE_TAR_PATH_OUT="${TUIDE_${prefix}_TAR_PATH}"
            TUIDE_ZST_PATH="${TUIDE_${prefix}_ZST_PATH}"
            TUIDE_MANIFEST_HPP="${TUIDE_${prefix}_MANIFEST_HPP}"
            TUIDE_BLOB_OBJ="${TUIDE_${prefix}_BLOB_OBJ}"
            TUIDE_MANIFEST_PREFIX="${TUIDE_${prefix}_MANIFEST_PREFIX}"
            bash "${_script}"
    DEPENDS "${_script}"
    COMMENT "Preparing embedded ${TUIDE_${prefix}_TOOL_ID} ${TUIDE_${prefix}_VERSION}"
    VERBATIM)
endfunction()
