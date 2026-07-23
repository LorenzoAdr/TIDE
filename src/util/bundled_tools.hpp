#pragma once

#include <optional>
#include <string>

namespace tuide {

struct ClangdLocation {
  std::string binary_path;
  std::string resource_dir;
  enum class Source { Env, SystemPath, Bundled } source = Source::Bundled;
};

struct GdbLocation {
  std::string binary_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::Bundled;
};

struct RgLocation {
  std::string binary_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::Bundled;
};

struct BasedpyrightLocation {
  std::string binary_path;
  bool needs_stdio_flag = true;
  bool use_python_module = false;
  std::string python_module;  // e.g. basedpyright.langserver when use_python_module
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct DebugpyLocation {
  std::string python_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct BashLsLocation {
  std::string binary_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct TexlabLocation {
  std::string binary_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct RustAnalyzerLocation {
  std::string binary_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct GoplsLocation {
  std::string binary_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct ZlsLocation {
  std::string binary_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct FortlsLocation {
  std::string binary_path;
  bool use_python_module = false;
  std::string python_module;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct LuaLsLocation {
  std::string binary_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct TypescriptLsLocation {
  std::string binary_path;
  bool needs_stdio_flag = true;
  bool use_node_script = false;
  std::string script_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct NeocmakelspLocation {
  std::string binary_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct MakeLsLocation {
  std::string binary_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct YamlLsLocation {
  std::string binary_path;
  bool needs_stdio_flag = true;
  bool use_node_script = false;
  std::string script_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

struct BashDebugAdapterLocation {
  std::string node_path;
  std::string adapter_js_path;
  std::string bash_path;
  std::string bashdb_path;
  std::string bashdb_lib_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::SystemPath;
};

bool has_bundled_clangd();
bool has_bundled_gdb();
bool has_bundled_rg();
bool has_bundled_python_tools();

void set_runtime_force_bundled_clangd(bool value);
void set_runtime_force_bundled_gdb(bool value);
void set_runtime_force_bundled_rg(bool value);
void set_runtime_force_bundled_python_tools(bool value);

bool should_force_bundled_clangd();
bool should_force_bundled_gdb();
bool should_force_bundled_rg();
bool should_force_bundled_python_tools();

std::optional<ClangdLocation> resolve_clangd();
std::optional<GdbLocation> resolve_gdb();
std::optional<RgLocation> resolve_rg();
std::optional<BasedpyrightLocation> resolve_basedpyright();
std::optional<DebugpyLocation> resolve_debugpy();
std::optional<BashLsLocation> resolve_bash_language_server();
std::optional<TexlabLocation> resolve_texlab();
std::optional<RustAnalyzerLocation> resolve_rust_analyzer();
std::optional<GoplsLocation> resolve_gopls();
std::optional<ZlsLocation> resolve_zls();
std::optional<FortlsLocation> resolve_fortls();
std::optional<LuaLsLocation> resolve_lua_language_server();
std::optional<TypescriptLsLocation> resolve_typescript_language_server();
std::optional<NeocmakelspLocation> resolve_neocmakelsp();
std::optional<MakeLsLocation> resolve_make_ls();
std::optional<YamlLsLocation> resolve_yaml_language_server();
std::optional<BashDebugAdapterLocation> resolve_bash_debug_adapter();
std::optional<std::string> resolve_shellcheck();
std::optional<std::string> resolve_chktex();
std::optional<std::string> resolve_gfortran();

}  // namespace tuide
