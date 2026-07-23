include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_NODE_TAR_NAME "node-v${TUIDE_NODE_VERSION}-linux-x64.tar.xz")
set(TUIDE_NODE_URL "https://nodejs.org/dist/v${TUIDE_NODE_VERSION}/${TUIDE_NODE_TAR_NAME}")
set(TUIDE_NODE_TAR_PATH "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_NODE_TAR_NAME}")

set(TUIDE_YAML_LS_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/yaml_ls_staging")
set(TUIDE_YAML_LS_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/yaml_ls_payload")
set(TUIDE_YAML_LS_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/yaml_ls_blob.tar")
set(TUIDE_YAML_LS_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/yaml_ls_blob.zst")
set(TUIDE_YAML_LS_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/yaml_ls_blob.o")
set(TUIDE_YAML_LS_MANIFEST_HPP
    "${TUIDE_BUNDLED_GEN_DIR}/bundled_yaml_ls_manifest.hpp")

foreach(_tool objcopy zstd sha256sum npm)
  find_program(_tuide_yaml_${_tool} ${_tool})
  if(NOT _tuide_yaml_${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_YAML_LS requires '${_tool}'")
  endif()
endforeach()

set(TUIDE_PREPARE_YAML_LS_SCRIPT
    "${CMAKE_SOURCE_DIR}/cmake/prepare_yaml_ls_bundle.sh")

add_custom_command(
  OUTPUT "${TUIDE_YAML_LS_ZST_PATH}" "${TUIDE_YAML_LS_MANIFEST_HPP}"
         "${TUIDE_YAML_LS_BLOB_OBJ}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf
          "${TUIDE_YAML_LS_STAGING_DIR}" "${TUIDE_YAML_LS_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E env
          TUIDE_YAML_LS_VERSION="${TUIDE_YAML_LS_VERSION}"
          TUIDE_YAML_LS_NPM_VERSION="${TUIDE_YAML_LS_NPM_VERSION}"
          TUIDE_NODE_URL="${TUIDE_NODE_URL}"
          TUIDE_NODE_TAR_PATH="${TUIDE_NODE_TAR_PATH}"
          TUIDE_YAML_LS_STAGING_DIR="${TUIDE_YAML_LS_STAGING_DIR}"
          TUIDE_YAML_LS_PAYLOAD_DIR="${TUIDE_YAML_LS_PAYLOAD_DIR}"
          TUIDE_YAML_LS_TAR_PATH="${TUIDE_YAML_LS_TAR_PATH}"
          TUIDE_YAML_LS_ZST_PATH="${TUIDE_YAML_LS_ZST_PATH}"
          TUIDE_YAML_LS_MANIFEST_HPP="${TUIDE_YAML_LS_MANIFEST_HPP}"
          TUIDE_YAML_LS_BLOB_OBJ="${TUIDE_YAML_LS_BLOB_OBJ}"
          bash "${TUIDE_PREPARE_YAML_LS_SCRIPT}"
  DEPENDS "${TUIDE_PREPARE_YAML_LS_SCRIPT}"
  COMMENT "Preparing embedded yaml-language-server ${TUIDE_YAML_LS_VERSION}"
  VERBATIM)

add_custom_target(tuide_yaml_ls_bundle DEPENDS
  "${TUIDE_YAML_LS_ZST_PATH}" "${TUIDE_YAML_LS_MANIFEST_HPP}"
  "${TUIDE_YAML_LS_BLOB_OBJ}")

tuide_link_embedded_blob(tuide "${TUIDE_YAML_LS_BLOB_OBJ}" TUIDE_HAS_BUNDLED_YAML_LS
  TUIDE_DEFAULT_FORCE_BUNDLED_YAML_LS "${TUIDE_FORCE_BUNDLED_YAML_LS}")
add_dependencies(tuide tuide_yaml_ls_bundle)
