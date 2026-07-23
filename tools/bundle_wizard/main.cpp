#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

namespace {

constexpr int kLspClangd = 0;
constexpr int kLspBashLs = 1;
constexpr int kLspTexlab = 2;
constexpr int kLspRustAnalyzer = 3;
constexpr int kLspGopls = 4;
constexpr int kLspZls = 5;
constexpr int kLspFortls = 6;
constexpr int kLspLuaLs = 7;
constexpr int kLspTsserver = 8;
constexpr int kLspNeocmakelsp = 9;
constexpr int kLspMakeLs = 10;
constexpr int kLspYamlLs = 11;
constexpr int kDapGdb = 12;
constexpr int kDapPython = 13;
constexpr int kDapBashDap = 14;
constexpr int kForceBundled = 15;
constexpr int kUiLocale = 16;
constexpr int kEditorMode = 17;
constexpr int kBuildBackend = 18;
constexpr int kStaticLibstdcxx = 19;
constexpr int kOptionCount = 20;

constexpr int kGdbNone = 0;
constexpr int kGdbStatic = 1;
constexpr int kGdbCoreAnalyzer = 2;

constexpr int kPythonNone = 0;
constexpr int kPythonLspMin = 1;
constexpr int kPythonFull = 2;

constexpr int kLocaleEs = 0;
constexpr int kLocaleEn = 1;

constexpr int kEditorNormal = 0;
constexpr int kEditorHelix = 1;

constexpr int kBackendHost = 0;
constexpr int kBackendDockerFocal = 1;
constexpr int kBackendDockerBionic = 2;

struct BundleConfig {
  bool bundle_clangd = false;
  bool bundle_bash_ls = false;
  bool bundle_texlab = false;
  bool bundle_rust_analyzer = false;
  bool bundle_gopls = false;
  bool bundle_zls = false;
  bool bundle_fortls = false;
  bool bundle_lua_ls = false;
  bool bundle_tsserver = false;
  bool bundle_neocmakelsp = false;
  bool bundle_make_ls = false;
  bool bundle_yaml_ls = false;
  int gdb_kind = kGdbNone;
  int python_kind = kPythonNone;
  bool bundle_bash_dap = false;
  bool force_bundled = false;
  int ui_locale = kLocaleEn;
  int editor_mode = kEditorNormal;
  int build_backend = kBackendHost;
  bool static_libstdcxx = false;
};

struct WizardState {
  BundleConfig draft;
  int selected = 0;
};

// Tamaños aproximados del blob .zst embebido (MB). Base incluye tuide + ripgrep.
constexpr int kBaseBinaryMb = 50;
constexpr int kSizeClangdMb = 34;
constexpr int kSizeBashLsMb = 38;
constexpr int kSizeTexlabMb = 10;
constexpr int kSizeRustAnalyzerMb = 16;
constexpr int kSizeGoplsMb = 19;
constexpr int kSizeZlsMb = 5;
constexpr int kSizeFortlsMb = 3;
constexpr int kSizeLuaLsMb = 3;
constexpr int kSizeTsserverMb = 40;
constexpr int kSizeNeocmakelspMb = 3;
constexpr int kSizeMakeLsMb = 3;
constexpr int kSizeYamlLsMb = 40;
constexpr int kSizeGdbStaticMb = 16;
constexpr int kSizeGdbCoreAnalyzerMb = 30;
constexpr int kSizePythonLspMinMb = 42;
constexpr int kSizePythonFullMb = 90;
constexpr int kSizeBashDapMb = 1;

int estimated_binary_mb(const BundleConfig& config) {
  int mb = kBaseBinaryMb;
  if (config.bundle_clangd) {
    mb += kSizeClangdMb;
  }
  if (config.bundle_bash_ls) {
    mb += kSizeBashLsMb;
  }
  if (config.bundle_texlab) {
    mb += kSizeTexlabMb;
  }
  if (config.bundle_rust_analyzer) {
    mb += kSizeRustAnalyzerMb;
  }
  if (config.bundle_gopls) {
    mb += kSizeGoplsMb;
  }
  if (config.bundle_zls) {
    mb += kSizeZlsMb;
  }
  if (config.bundle_fortls) {
    mb += kSizeFortlsMb;
  }
  if (config.bundle_lua_ls) {
    mb += kSizeLuaLsMb;
  }
  if (config.bundle_tsserver) {
    mb += kSizeTsserverMb;
  }
  if (config.bundle_neocmakelsp) {
    mb += kSizeNeocmakelspMb;
  }
  if (config.bundle_make_ls) {
    mb += kSizeMakeLsMb;
  }
  if (config.bundle_yaml_ls) {
    mb += kSizeYamlLsMb;
  }
  if (config.gdb_kind == kGdbStatic) {
    mb += kSizeGdbStaticMb;
  } else if (config.gdb_kind == kGdbCoreAnalyzer) {
    mb += kSizeGdbCoreAnalyzerMb;
  }
  if (config.python_kind == kPythonLspMin) {
    mb += kSizePythonLspMinMb;
  } else if (config.python_kind == kPythonFull) {
    mb += kSizePythonFullMb;
  }
  if (config.bundle_bash_dap) {
    mb += kSizeBashDapMb;
  }
  return mb;
}

std::string format_estimated_size(const BundleConfig& config) {
  const char* prefix = config.ui_locale == kLocaleEs
                           ? "Tamaño estimado del binario tuide: ~"
                           : "Estimated tuide binary size: ~";
  return std::string(prefix) + std::to_string(estimated_binary_mb(config)) + " MB";
}

const char* tr(int locale, const char* es, const char* en) {
  return locale == kLocaleEs ? es : en;
}

std::string gdb_kind_label(int locale, int kind) {
  switch (kind) {
    case kGdbStatic:
      return "gdb-static (musl) (~+" + std::to_string(kSizeGdbStaticMb) + " MB)";
    case kGdbCoreAnalyzer:
      return "gdb + Core Analyzer (~+" + std::to_string(kSizeGdbCoreAnalyzerMb) + " MB)";
    default:
      return tr(locale, "ninguno (gdb del sistema)", "none (system gdb)");
  }
}

std::string python_kind_label(int locale, int kind) {
  switch (kind) {
    case kPythonLspMin:
      return "A: basedpyright (~+" + std::to_string(kSizePythonLspMinMb) + " MB)";
    case kPythonFull:
      return "B: CPython + basedpyright + debugpy (~+" + std::to_string(kSizePythonFullMb) +
             " MB)";
    default:
      return tr(locale, "ninguno (herramientas del sistema)", "none (system tools)");
  }
}

std::string ui_locale_label(int locale) {
  switch (locale) {
    case kLocaleEs:
      return "Español";
    case kLocaleEn:
    default:
      return "English";
  }
}

const char* ui_locale_tag(int locale) {
  switch (locale) {
    case kLocaleEs:
      return "es";
    case kLocaleEn:
    default:
      return "en";
  }
}

int parse_ui_locale(const std::string& tag) {
  if (tag == "es") {
    return kLocaleEs;
  }
  return kLocaleEn;
}

std::string editor_mode_label(int mode) {
  switch (mode) {
    case kEditorHelix:
      return "Helix";
    case kEditorNormal:
    default:
      return "Normal";
  }
}

const char* editor_mode_tag(int mode) {
  switch (mode) {
    case kEditorHelix:
      return "helix";
    case kEditorNormal:
    default:
      return "normal";
  }
}

int parse_editor_mode(const std::string& tag) {
  if (tag == "helix") {
    return kEditorHelix;
  }
  return kEditorNormal;
}

std::string build_backend_label(int locale, int backend) {
  switch (backend) {
    case kBackendDockerFocal:
      return tr(locale, "Docker (glibc 2.31 / Ubuntu 20.04)",
                "Docker (glibc 2.31 / Ubuntu 20.04)");
    case kBackendDockerBionic:
      return tr(locale, "Docker (glibc 2.27 / Ubuntu 18.04)",
                "Docker (glibc 2.27 / Ubuntu 18.04)");
    case kBackendHost:
    default:
      return tr(locale, "host (sistema local)", "host (local system)");
  }
}

const char* build_backend_tag(int backend) {
  switch (backend) {
    case kBackendDockerFocal:
      return "docker_focal";
    case kBackendDockerBionic:
      return "docker_bionic";
    case kBackendHost:
    default:
      return "host";
  }
}

int parse_build_backend(const std::string& tag) {
  if (tag == "docker_focal" || tag == "docker" || tag == "portable") {
    return kBackendDockerFocal;
  }
  if (tag == "docker_bionic" || tag == "bionic") {
    return kBackendDockerBionic;
  }
  return kBackendHost;
}

bool parse_bool_value(const std::string& value) {
  return value == "1";
}

void load_bundle_config(const std::string& path, BundleConfig* config) {
  if (config == nullptr) {
    return;
  }
  std::ifstream input(path);
  if (!input) {
    return;
  }
  std::string line;
  bool legacy_bundle_gdb = false;
  bool legacy_force = false;
  while (std::getline(input, line)) {
    if (line.rfind("BUNDLE_CLANGD=", 0) == 0) {
      config->bundle_clangd = parse_bool_value(line.substr(14));
    } else if (line.rfind("BUNDLE_CLANGD_FORCE=", 0) == 0) {
      legacy_force = legacy_force || parse_bool_value(line.substr(20));
    } else if (line.rfind("GDB_BUNDLE_KIND=", 0) == 0) {
      const std::string kind = line.substr(16);
      if (kind == "static") {
        config->gdb_kind = kGdbStatic;
      } else if (kind == "core_analyzer") {
        config->gdb_kind = kGdbCoreAnalyzer;
      } else {
        config->gdb_kind = kGdbNone;
      }
    } else if (line.rfind("BUNDLE_GDB=", 0) == 0) {
      legacy_bundle_gdb = parse_bool_value(line.substr(11));
    } else if (line.rfind("BUNDLE_GDB_FORCE=", 0) == 0) {
      legacy_force = legacy_force || parse_bool_value(line.substr(17));
    } else if (line.rfind("PYTHON_BUNDLE_KIND=", 0) == 0) {
      const std::string kind = line.substr(19);
      if (kind == "lsp_min") {
        config->python_kind = kPythonLspMin;
      } else if (kind == "full") {
        config->python_kind = kPythonFull;
      } else {
        config->python_kind = kPythonNone;
      }
    } else if (line.rfind("BUNDLE_PYTHON_FORCE=", 0) == 0) {
      legacy_force = legacy_force || parse_bool_value(line.substr(20));
    } else if (line.rfind("BUNDLE_BASH_LS=", 0) == 0) {
      config->bundle_bash_ls = parse_bool_value(line.substr(15));
    } else if (line.rfind("BUNDLE_BASH_LS_FORCE=", 0) == 0) {
      legacy_force = legacy_force || parse_bool_value(line.substr(21));
    } else if (line.rfind("BUNDLE_TEXLAB=", 0) == 0) {
      config->bundle_texlab = parse_bool_value(line.substr(14));
    } else if (line.rfind("BUNDLE_TEXLAB_FORCE=", 0) == 0) {
      legacy_force = legacy_force || parse_bool_value(line.substr(20));
    } else if (line.rfind("BUNDLE_BASH_DAP=", 0) == 0) {
      config->bundle_bash_dap = parse_bool_value(line.substr(16));
    } else if (line.rfind("BUNDLE_BASH_DAP_FORCE=", 0) == 0) {
      legacy_force = legacy_force || parse_bool_value(line.substr(22));
    } else if (line.rfind("BUNDLE_RUST_ANALYZER=", 0) == 0) {
      config->bundle_rust_analyzer = parse_bool_value(line.substr(21));
    } else if (line.rfind("BUNDLE_GOPLS=", 0) == 0) {
      config->bundle_gopls = parse_bool_value(line.substr(13));
    } else if (line.rfind("BUNDLE_ZLS=", 0) == 0) {
      config->bundle_zls = parse_bool_value(line.substr(11));
    } else if (line.rfind("BUNDLE_FORTLS=", 0) == 0) {
      config->bundle_fortls = parse_bool_value(line.substr(14));
    } else if (line.rfind("BUNDLE_LUA_LS=", 0) == 0) {
      config->bundle_lua_ls = parse_bool_value(line.substr(14));
    } else if (line.rfind("BUNDLE_NEOCMAKELSP=", 0) == 0) {
      config->bundle_neocmakelsp = parse_bool_value(line.substr(19));
    } else if (line.rfind("BUNDLE_MAKE_LS=", 0) == 0) {
      config->bundle_make_ls = parse_bool_value(line.substr(15));
    } else if (line.rfind("BUNDLE_YAML_LS=", 0) == 0) {
      config->bundle_yaml_ls = parse_bool_value(line.substr(15));
    } else if (line.rfind("BUNDLE_TSSERVER=", 0) == 0) {
      config->bundle_tsserver = parse_bool_value(line.substr(16));
    } else if (line.rfind("FORCE_BUNDLED=", 0) == 0) {
      config->force_bundled = parse_bool_value(line.substr(14));
    } else if (line.rfind("UI_LOCALE=", 0) == 0) {
      config->ui_locale = parse_ui_locale(line.substr(10));
    } else if (line.rfind("EDITOR_MODE=", 0) == 0) {
      config->editor_mode = parse_editor_mode(line.substr(12));
    } else if (line.rfind("BUILD_BACKEND=", 0) == 0) {
      config->build_backend = parse_build_backend(line.substr(14));
    } else if (line.rfind("STATIC_LIBSTDCXX=", 0) == 0) {
      config->static_libstdcxx = parse_bool_value(line.substr(17));
    }
  }
  if (config->gdb_kind == kGdbNone && legacy_bundle_gdb) {
    config->gdb_kind = kGdbStatic;
  }
  if (legacy_force) {
    config->force_bundled = true;
  }
}

bool save_bundle_config(const std::string& path, const BundleConfig& config) {
  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return false;
  }
  const char* kind_name = "none";
  if (config.gdb_kind == kGdbStatic) {
    kind_name = "static";
  } else if (config.gdb_kind == kGdbCoreAnalyzer) {
    kind_name = "core_analyzer";
  }
  const char* python_kind_name = "none";
  if (config.python_kind == kPythonLspMin) {
    python_kind_name = "lsp_min";
  } else if (config.python_kind == kPythonFull) {
    python_kind_name = "full";
  }
  output << "BUNDLE_CLANGD=" << (config.bundle_clangd ? "1" : "0") << '\n';
  output << "GDB_BUNDLE_KIND=" << kind_name << '\n';
  output << "BUNDLE_GDB=" << (config.gdb_kind != kGdbNone ? "1" : "0") << '\n';
  output << "PYTHON_BUNDLE_KIND=" << python_kind_name << '\n';
  output << "BUNDLE_BASH_LS=" << (config.bundle_bash_ls ? "1" : "0") << '\n';
  output << "BUNDLE_TEXLAB=" << (config.bundle_texlab ? "1" : "0") << '\n';
  output << "BUNDLE_BASH_DAP=" << (config.bundle_bash_dap ? "1" : "0") << '\n';
  output << "BUNDLE_RUST_ANALYZER=" << (config.bundle_rust_analyzer ? "1" : "0") << '\n';
  output << "BUNDLE_GOPLS=" << (config.bundle_gopls ? "1" : "0") << '\n';
  output << "BUNDLE_ZLS=" << (config.bundle_zls ? "1" : "0") << '\n';
  output << "BUNDLE_FORTLS=" << (config.bundle_fortls ? "1" : "0") << '\n';
  output << "BUNDLE_LUA_LS=" << (config.bundle_lua_ls ? "1" : "0") << '\n';
  output << "BUNDLE_TSSERVER=" << (config.bundle_tsserver ? "1" : "0") << '\n';
  output << "BUNDLE_NEOCMAKELSP=" << (config.bundle_neocmakelsp ? "1" : "0") << '\n';
  output << "BUNDLE_MAKE_LS=" << (config.bundle_make_ls ? "1" : "0") << '\n';
  output << "BUNDLE_YAML_LS=" << (config.bundle_yaml_ls ? "1" : "0") << '\n';
  output << "FORCE_BUNDLED=" << (config.force_bundled ? "1" : "0") << '\n';
  output << "UI_LOCALE=" << ui_locale_tag(config.ui_locale) << '\n';
  output << "EDITOR_MODE=" << editor_mode_tag(config.editor_mode) << '\n';
  output << "BUILD_BACKEND=" << build_backend_tag(config.build_backend) << '\n';
  output << "STATIC_LIBSTDCXX=" << (config.static_libstdcxx ? "1" : "0") << '\n';
  return static_cast<bool>(output);
}

bool option_enabled(int /*index*/) {
  return true;
}

bool option_is_checkbox(int index) {
  return index != kDapGdb && index != kDapPython && index != kUiLocale && index != kEditorMode &&
         index != kBuildBackend;
}

bool option_checked(const WizardState& state, int index) {
  switch (index) {
    case kLspClangd:
      return state.draft.bundle_clangd;
    case kLspBashLs:
      return state.draft.bundle_bash_ls;
    case kLspTexlab:
      return state.draft.bundle_texlab;
    case kLspRustAnalyzer:
      return state.draft.bundle_rust_analyzer;
    case kLspGopls:
      return state.draft.bundle_gopls;
    case kLspZls:
      return state.draft.bundle_zls;
    case kLspFortls:
      return state.draft.bundle_fortls;
    case kLspLuaLs:
      return state.draft.bundle_lua_ls;
    case kLspTsserver:
      return state.draft.bundle_tsserver;
    case kLspNeocmakelsp:
      return state.draft.bundle_neocmakelsp;
    case kLspMakeLs:
      return state.draft.bundle_make_ls;
    case kLspYamlLs:
      return state.draft.bundle_yaml_ls;
    case kDapBashDap:
      return state.draft.bundle_bash_dap;
    case kForceBundled:
      return state.draft.force_bundled;
    case kStaticLibstdcxx:
      return state.draft.static_libstdcxx;
    default:
      return false;
  }
}

std::string option_label(const WizardState& state, int index) {
  const int locale = state.draft.ui_locale;
  switch (index) {
    case kLspClangd:
      return "clangd (C/C++)";
    case kLspBashLs:
      return "bash-language-server (Bash)";
    case kLspTexlab:
      return "TexLab (LaTeX)";
    case kLspRustAnalyzer:
      return "rust-analyzer (Rust)";
    case kLspGopls:
      return "gopls (Go)";
    case kLspZls:
      return "zls (Zig)";
    case kLspFortls:
      return "fortls (Fortran)";
    case kLspLuaLs:
      return "lua-language-server (Lua)";
    case kLspTsserver:
      return "typescript-ls (JS/TS)";
    case kLspNeocmakelsp:
      return "neocmakelsp (CMake)";
    case kLspMakeLs:
      return "make-ls (Makefile)";
    case kLspYamlLs:
      return "yaml-language-server (YAML)";
    case kDapGdb:
      return std::string("GDB: ") + gdb_kind_label(locale, state.draft.gdb_kind);
    case kDapPython:
      return std::string("Python: ") + python_kind_label(locale, state.draft.python_kind);
    case kDapBashDap:
      return "Bash DAP";
    case kForceBundled:
      return tr(locale, "Forzar todos los componentes ON", "Force all selected components ON");
    case kUiLocale:
      return std::string(tr(locale, "Idioma: ", "Language: ")) +
             ui_locale_label(state.draft.ui_locale);
    case kEditorMode:
      return std::string(tr(locale, "Editor: ", "Editor: ")) +
             editor_mode_label(state.draft.editor_mode);
    case kBuildBackend:
      return std::string(tr(locale, "Compilación: ", "Build: ")) +
             build_backend_label(locale, state.draft.build_backend);
    case kStaticLibstdcxx:
      return tr(locale, "libstdc++ estático (menos deps runtime)",
                "static libstdc++ (fewer runtime deps)");
    default:
      return "";
  }
}

void cycle_gdb_kind(WizardState* state) {
  if (state == nullptr) {
    return;
  }
  state->draft.gdb_kind = (state->draft.gdb_kind + 1) % 3;
}

void cycle_python_kind(WizardState* state) {
  if (state == nullptr) {
    return;
  }
  state->draft.python_kind = (state->draft.python_kind + 1) % 3;
}

void cycle_ui_locale(WizardState* state) {
  if (state == nullptr) {
    return;
  }
  state->draft.ui_locale = (state->draft.ui_locale + 1) % 2;
}

void cycle_editor_mode(WizardState* state) {
  if (state == nullptr) {
    return;
  }
  state->draft.editor_mode = (state->draft.editor_mode + 1) % 2;
}

void cycle_build_backend(WizardState* state) {
  if (state == nullptr) {
    return;
  }
  state->draft.build_backend = (state->draft.build_backend + 1) % 3;
}

void toggle_option(WizardState* state, int index) {
  if (state == nullptr || !option_enabled(index)) {
    return;
  }
  switch (index) {
    case kLspClangd:
      state->draft.bundle_clangd = !state->draft.bundle_clangd;
      break;
    case kLspBashLs:
      state->draft.bundle_bash_ls = !state->draft.bundle_bash_ls;
      break;
    case kLspTexlab:
      state->draft.bundle_texlab = !state->draft.bundle_texlab;
      break;
    case kLspRustAnalyzer:
      state->draft.bundle_rust_analyzer = !state->draft.bundle_rust_analyzer;
      break;
    case kLspGopls:
      state->draft.bundle_gopls = !state->draft.bundle_gopls;
      break;
    case kLspZls:
      state->draft.bundle_zls = !state->draft.bundle_zls;
      break;
    case kLspFortls:
      state->draft.bundle_fortls = !state->draft.bundle_fortls;
      break;
    case kLspLuaLs:
      state->draft.bundle_lua_ls = !state->draft.bundle_lua_ls;
      break;
    case kLspTsserver:
      state->draft.bundle_tsserver = !state->draft.bundle_tsserver;
      break;
    case kLspNeocmakelsp:
      state->draft.bundle_neocmakelsp = !state->draft.bundle_neocmakelsp;
      break;
    case kLspMakeLs:
      state->draft.bundle_make_ls = !state->draft.bundle_make_ls;
      break;
    case kLspYamlLs:
      state->draft.bundle_yaml_ls = !state->draft.bundle_yaml_ls;
      break;
    case kDapGdb:
      cycle_gdb_kind(state);
      break;
    case kDapPython:
      cycle_python_kind(state);
      break;
    case kDapBashDap:
      state->draft.bundle_bash_dap = !state->draft.bundle_bash_dap;
      break;
    case kForceBundled:
      state->draft.force_bundled = !state->draft.force_bundled;
      break;
    case kUiLocale:
      cycle_ui_locale(state);
      break;
    case kEditorMode:
      cycle_editor_mode(state);
      break;
    case kBuildBackend:
      cycle_build_backend(state);
      break;
    case kStaticLibstdcxx:
      state->draft.static_libstdcxx = !state->draft.static_libstdcxx;
      break;
    default:
      break;
  }
}

// Paleta Dark Classic (tema por defecto de la pantalla de bienvenida sin workspace).
namespace wiz_theme {
using ftxui::Color;
inline Color PanelBg() { return Color::RGB(28, 32, 42); }
inline Color Accent() { return Color::RGB(90, 170, 255); }
inline Color AccentDim() { return Color::RGB(50, 90, 140); }
inline Color Header() { return Color::RGB(180, 200, 255); }
inline Color Muted() { return Color::RGB(130, 140, 160); }
inline Color Warning() { return Color::RGB(255, 200, 80); }
inline Color TabHover() { return Color::RGB(48, 58, 72); }
}  // namespace wiz_theme

ftxui::Element centered_row(ftxui::Element row) {
  using namespace ftxui;
  return hbox({filler(), std::move(row), filler()});
}

ftxui::Element render_option_row(const WizardState& state, int index, bool checkbox) {
  using namespace ftxui;
  const bool selected = index == state.selected;
  const bool enabled = option_enabled(index);
  std::string label = option_label(state, index);
  if (checkbox) {
    label = std::string(option_checked(state, index) ? "[x] " : "[ ] ") + label;
  }
  Color fg = wiz_theme::Muted();
  if (enabled) {
    fg = selected ? wiz_theme::Accent() : wiz_theme::Header();
  }
  Element row = text(label) | color(fg);
  if (enabled) {
    row = row | bold;
  }
  if (selected && enabled) {
    row = row | bgcolor(wiz_theme::TabHover());
  }
  return row;
}

ftxui::Element render_panel(const char* title, const std::vector<int>& indices,
                            const WizardState& state) {
  using namespace ftxui;
  Elements rows;
  rows.push_back(text(title) | bold | color(wiz_theme::Accent()));
  rows.push_back(separator() | color(wiz_theme::AccentDim()));
  for (int index : indices) {
    rows.push_back(render_option_row(state, index, option_is_checkbox(index)));
  }
  return vbox(std::move(rows)) | border | color(wiz_theme::AccentDim()) |
         bgcolor(wiz_theme::PanelBg());
}

ftxui::Element render_tuide_logo() {
  using namespace ftxui;
  static const std::vector<std::string> lines = {
      "████████╗██╗   ██╗██╗██████╗ ███████╗",
      "╚══██╔══╝██║   ██║██║██╔══██╗██╔════╝",
      "   ██║   ██║   ██║██║██║  ██║█████╗  ",
      "   ██║   ██║   ██║██║██║  ██║██╔══╝  ",
      "   ██║   ╚██████╔╝██║██████╔╝███████╗",
      "   ╚═╝    ╚═════╝ ╚═╝╚═════╝ ╚══════╝",
  };
  Elements logo_rows;
  for (const auto& line : lines) {
    logo_rows.push_back(text(line) | color(wiz_theme::Accent()) | bold);
  }
  return vbox(std::move(logo_rows));
}

ftxui::Element render_author_footer() {
  using namespace ftxui;
  auto line = [](const char* s) {
    return text(s) | color(wiz_theme::Muted());
  };
  return vbox({
      hbox({filler(), line("Lorenzo Arias del Real")}),
      hbox({filler(), line("lorenzo.adr@proton.me")}),
      hbox({filler(), line("Apache License 2.0")}),
  });
}

}  // namespace

int main(int argc, char** argv) {
  std::string config_path = ".bundle-config";
  if (argc >= 2) {
    config_path = argv[1];
  }

  WizardState state;
  load_bundle_config(config_path, &state.draft);

  using namespace ftxui;

  bool cancelled = false;

  auto screen = ScreenInteractive::Fullscreen();

  const std::vector<int> kLspIndices = {kLspClangd,     kLspBashLs,   kLspTexlab,
                                        kLspRustAnalyzer, kLspGopls,    kLspZls,
                                        kLspFortls,     kLspLuaLs,    kLspTsserver,
                                        kLspNeocmakelsp, kLspMakeLs, kLspYamlLs};
  const std::vector<int> kDapIndices = {kDapGdb, kDapPython, kDapBashDap};
  const std::vector<int> kForceIndices = {kForceBundled};
  const std::vector<int> kDefaultsIndices = {kUiLocale, kEditorMode, kBuildBackend,
                                             kStaticLibstdcxx};

  auto component = CatchEvent(
      Renderer([&] {
        const int locale = state.draft.ui_locale;
        const Element lsp_panel = render_panel("LSP", kLspIndices, state);
        const Element dap_panel = render_panel("DAP", kDapIndices, state);
        const Element force_panel =
            render_panel(tr(locale, "Forzar embebido", "Force bundled"), kForceIndices, state);
        const Element defaults_panel = render_panel(
            tr(locale, "Preferencias por defecto", "Default preferences"), kDefaultsIndices,
            state);
        const Element right_column =
            vbox({dap_panel, text(""), force_panel, text(""), defaults_panel});

        const Element menu_body = vbox({
            centered_row(text(tr(locale, "tuide — componentes embebidos",
                                 "tuide — bundled components")) |
                         bold | color(wiz_theme::Accent())),
            text(""),
            centered_row(text(tr(locale, "Selecciona qué incluir en el binario de tuide:",
                                 "Select what to include in the tuide binary:")) |
                         color(wiz_theme::Muted())),
            text(""),
            render_author_footer(),
            hbox({
                lsp_panel | flex,
                text("  "),
                right_column | flex,
            }),
            text(""),
            separator() | color(wiz_theme::AccentDim()),
            text(format_estimated_size(state.draft)) | bold | color(wiz_theme::Warning()),
            text(tr(locale, "↑↓ j/k  Espacio alternar   Enter confirmar   Esc cancelar",
                    "↑↓ j/k  Space toggle   Enter confirm   Esc cancel")) |
                color(wiz_theme::Muted()),
        });

        const Element hero = vbox({
            centered_row(render_tuide_logo()),
            text(""),
            centered_row(text(tr(locale, "asistente de compilación", "build assistant")) |
                         color(wiz_theme::Muted())),
        });

        // Logo centrado arriba; menú pegado abajo.
        return vbox({
                   filler(),
                   std::move(hero),
                   filler(),
                   std::move(menu_body),
               }) |
               flex | bgcolor(wiz_theme::PanelBg());
      }),
      [&](Event event) {
        if (event == Event::Escape) {
          cancelled = true;
          screen.ExitLoopClosure()();
          return true;
        }
        if (event == Event::Return) {
          screen.ExitLoopClosure()();
          return true;
        }
        if (event == Event::ArrowDown || event == Event::Character('j')) {
          state.selected = (state.selected + 1) % kOptionCount;
          return true;
        }
        if (event == Event::ArrowUp || event == Event::Character('k')) {
          state.selected = (state.selected + kOptionCount - 1) % kOptionCount;
          return true;
        }
        if (event == Event::Character(' ')) {
          toggle_option(&state, state.selected);
          return true;
        }
        return false;
      });

  screen.Loop(component);

  if (cancelled) {
    return 1;
  }
  if (!save_bundle_config(config_path, state.draft)) {
    const char* msg = state.draft.ui_locale == kLocaleEs
                          ? "tuide-bundle-wizard: no se pudo escribir %s\n"
                          : "tuide-bundle-wizard: could not write %s\n";
    std::fprintf(stderr, msg, config_path.c_str());
    return 1;
  }
  return 0;
}
