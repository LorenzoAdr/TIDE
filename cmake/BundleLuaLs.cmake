include("${CMAKE_SOURCE_DIR}/cmake/BundleEmbedCommon.cmake")

set(TGDB_BUNDLED_CACHE_DIR "${CMAKE_SOURCE_DIR}/third_party/bundled/cache")
set(TGDB_BUNDLED_GEN_DIR "${CMAKE_BINARY_DIR}/generated/bundled")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_CACHE_DIR}")
file(MAKE_DIRECTORY "${TGDB_BUNDLED_GEN_DIR}")

set(TGDB_LUA_LS_TOOL_ID "lua_ls")
set(TGDB_LUA_LS_EXTRACT_MODE "tar_tree")
set(TGDB_LUA_LS_BINARY_NAME "lua-language-server")
set(TGDB_LUA_LS_DOWNLOAD_NAME
    "lua-language-server-${TGDB_LUA_LS_VERSION}-linux-x64.tar.gz")
set(TGDB_LUA_LS_URL
    "https://github.com/LuaLS/lua-language-server/releases/download/${TGDB_LUA_LS_VERSION}/${TGDB_LUA_LS_DOWNLOAD_NAME}")
set(TGDB_LUA_LS_DOWNLOAD_PATH
    "${TGDB_BUNDLED_CACHE_DIR}/${TGDB_LUA_LS_DOWNLOAD_NAME}")
set(TGDB_LUA_LS_STAGING_DIR "${TGDB_BUNDLED_GEN_DIR}/lua_ls_staging")
set(TGDB_LUA_LS_PAYLOAD_DIR "${TGDB_BUNDLED_GEN_DIR}/lua_ls_payload")
set(TGDB_LUA_LS_TAR_PATH "${TGDB_BUNDLED_GEN_DIR}/lua_ls_blob.tar")
set(TGDB_LUA_LS_ZST_PATH "${TGDB_BUNDLED_GEN_DIR}/lua_ls_blob.zst")
set(TGDB_LUA_LS_BLOB_OBJ "${TGDB_BUNDLED_GEN_DIR}/lua_ls_blob.o")
set(TGDB_LUA_LS_MANIFEST_HPP "${TGDB_BUNDLED_GEN_DIR}/bundled_lua_ls_manifest.hpp")
set(TGDB_LUA_LS_MANIFEST_PREFIX "TGDB_BUNDLED_LUA_LS")

foreach(_tool objcopy zstd sha256sum)
  find_program(_tgdb_lua_${_tool} ${_tool})
  if(NOT _tgdb_lua_${_tool})
    message(FATAL_ERROR "TGDB_BUNDLE_LUA_LS requires '${_tool}'")
  endif()
endforeach()

tgdb_add_single_bin_bundle_command(LUA_LS)

add_custom_target(tgdb_lua_ls_bundle DEPENDS
  "${TGDB_LUA_LS_ZST_PATH}" "${TGDB_LUA_LS_MANIFEST_HPP}" "${TGDB_LUA_LS_BLOB_OBJ}")

tgdb_link_embedded_blob(tgdb "${TGDB_LUA_LS_BLOB_OBJ}" TGDB_HAS_BUNDLED_LUA_LS
  TGDB_DEFAULT_FORCE_BUNDLED_LUA_LS "${TGDB_FORCE_BUNDLED_LUA_LS}")
add_dependencies(tgdb tgdb_lua_ls_bundle)
