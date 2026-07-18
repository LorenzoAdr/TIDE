include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TUIDE_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TUIDE_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TUIDE_BUNDLED_GEN_DIR}")

set(TUIDE_LUA_LS_TOOL_ID "lua_ls")
set(TUIDE_LUA_LS_EXTRACT_MODE "tar_tree")
set(TUIDE_LUA_LS_BINARY_NAME "lua-language-server")
set(TUIDE_LUA_LS_DOWNLOAD_NAME
    "lua-language-server-${TUIDE_LUA_LS_VERSION}-linux-x64.tar.gz")
set(TUIDE_LUA_LS_URL
    "https://github.com/LuaLS/lua-language-server/releases/download/${TUIDE_LUA_LS_VERSION}/${TUIDE_LUA_LS_DOWNLOAD_NAME}")
set(TUIDE_LUA_LS_DOWNLOAD_PATH
    "${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_LUA_LS_DOWNLOAD_NAME}")
set(TUIDE_LUA_LS_STAGING_DIR "${TUIDE_BUNDLED_GEN_DIR}/lua_ls_staging")
set(TUIDE_LUA_LS_PAYLOAD_DIR "${TUIDE_BUNDLED_GEN_DIR}/lua_ls_payload")
set(TUIDE_LUA_LS_TAR_PATH "${TUIDE_BUNDLED_GEN_DIR}/lua_ls_blob.tar")
set(TUIDE_LUA_LS_ZST_PATH "${TUIDE_BUNDLED_GEN_DIR}/lua_ls_blob.zst")
set(TUIDE_LUA_LS_BLOB_OBJ "${TUIDE_BUNDLED_GEN_DIR}/lua_ls_blob.o")
set(TUIDE_LUA_LS_MANIFEST_HPP "${TUIDE_BUNDLED_GEN_DIR}/bundled_lua_ls_manifest.hpp")
set(TUIDE_LUA_LS_MANIFEST_PREFIX "TUIDE_BUNDLED_LUA_LS")

foreach(_tool objcopy zstd sha256sum)
  find_program(_tuide_lua_${_tool} ${_tool})
  if(NOT _tuide_lua_${_tool})
    message(FATAL_ERROR "TUIDE_BUNDLE_LUA_LS requires '${_tool}'")
  endif()
endforeach()

tuide_add_single_bin_bundle_command(LUA_LS)

add_custom_target(tuide_lua_ls_bundle DEPENDS
  "${TUIDE_LUA_LS_ZST_PATH}" "${TUIDE_LUA_LS_MANIFEST_HPP}" "${TUIDE_LUA_LS_BLOB_OBJ}")

tuide_link_embedded_blob(tuide "${TUIDE_LUA_LS_BLOB_OBJ}" TUIDE_HAS_BUNDLED_LUA_LS
  TUIDE_DEFAULT_FORCE_BUNDLED_LUA_LS "${TUIDE_FORCE_BUNDLED_LUA_LS}")
add_dependencies(tuide tuide_lua_ls_bundle)
