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
constexpr int kDapGdb = 9;
constexpr int kDapPython = 10;
constexpr int kDapBashDap = 11;
constexpr int kForceBundled = 12;
constexpr int kOptionCount = 13;

constexpr int kGdbNone = 0;
constexpr int kGdbStatic = 1;
constexpr int kGdbCoreAnalyzer = 2;

constexpr int kPythonNone = 0;
constexpr int kPythonLspMin = 1;
constexpr int kPythonFull = 2;

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
  int gdb_kind = kGdbNone;
  int python_kind = kPythonNone;
  bool bundle_bash_dap = false;
  bool force_bundled = false;
};

struct WizardState {
  BundleConfig draft;
  int selected = 0;
};

std::string gdb_kind_label(int kind) {
  switch (kind) {
    case kGdbStatic:
      return "gdb-static (musl)";
    case kGdbCoreAnalyzer:
      return "gdb + Core Analyzer";
    default:
      return "ninguno (gdb del sistema)";
  }
}

std::string python_kind_label(int kind) {
  switch (kind) {
    case kPythonLspMin:
      return "A: basedpyright (~+70–90 MB)";
    case kPythonFull:
      return "B: CPython + basedpyright + debugpy (~+90–120 MB)";
    default:
      return "ninguno (herramientas del sistema)";
  }
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
    } else if (line.rfind("BUNDLE_TSSERVER=", 0) == 0) {
      config->bundle_tsserver = parse_bool_value(line.substr(16));
    } else if (line.rfind("FORCE_BUNDLED=", 0) == 0) {
      config->force_bundled = parse_bool_value(line.substr(14));
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
  output << "FORCE_BUNDLED=" << (config.force_bundled ? "1" : "0") << '\n';
  return static_cast<bool>(output);
}

bool option_enabled(int /*index*/) {
  return true;
}

bool option_is_checkbox(int index) {
  return index != kDapGdb && index != kDapPython;
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
    case kDapBashDap:
      return state.draft.bundle_bash_dap;
    case kForceBundled:
      return state.draft.force_bundled;
    default:
      return false;
  }
}

std::string option_label(const WizardState& state, int index) {
  switch (index) {
    case kLspClangd:
      return "clangd";
    case kLspBashLs:
      return "bash-language-server";
    case kLspTexlab:
      return "TexLab";
    case kLspRustAnalyzer:
      return "rust-analyzer";
    case kLspGopls:
      return "gopls";
    case kLspZls:
      return "zls";
    case kLspFortls:
      return "fortls";
    case kLspLuaLs:
      return "lua-language-server";
    case kLspTsserver:
      return "typescript-ls";
    case kDapGdb:
      return "GDB: " + gdb_kind_label(state.draft.gdb_kind);
    case kDapPython:
      return "Python: " + python_kind_label(state.draft.python_kind);
    case kDapBashDap:
      return "Bash DAP";
    case kForceBundled:
      return "Forzar todos los componentes ON";
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
    default:
      break;
  }
}

ftxui::Element render_option_row(const WizardState& state, int index, bool checkbox) {
  using namespace ftxui;
  const bool selected = index == state.selected;
  const bool enabled = option_enabled(index);
  std::string label = option_label(state, index);
  if (checkbox) {
    label = std::string(option_checked(state, index) ? "[x] " : "[ ] ") + label;
  }
  Element row = text(label) |
                color(enabled ? (selected ? Color::Cyan : Color::White) : Color::GrayDark) |
                bold;
  if (selected && enabled) {
    row = row | inverted;
  }
  return row;
}

ftxui::Element render_panel(const char* title, const std::vector<int>& indices,
                            const WizardState& state) {
  using namespace ftxui;
  Elements rows;
  rows.push_back(text(title) | bold);
  rows.push_back(separator());
  for (int index : indices) {
    rows.push_back(render_option_row(state, index, option_is_checkbox(index)));
  }
  return vbox(std::move(rows)) | border;
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
                                        kLspFortls,     kLspLuaLs,    kLspTsserver};
  const std::vector<int> kDapIndices = {kDapGdb, kDapPython, kDapBashDap};
  const std::vector<int> kForceIndices = {kForceBundled};

  auto component = CatchEvent(
      Renderer([&] {
        const Element lsp_panel = render_panel("LSP", kLspIndices, state);
        const Element dap_panel = render_panel("DAP", kDapIndices, state);
        const Element force_panel = render_panel("Forzar embebido", kForceIndices, state);
        const Element right_column = vbox({dap_panel, text(""), force_panel});

        return window(text("tgdb — componentes embebidos") | bold | center,
                      vbox({
                          text("Selecciona qué incluir en el binario de tgdb:"),
                          separator(),
                          hbox({
                              lsp_panel | flex,
                              text("  "),
                              right_column | flex,
                          }),
                          separator(),
                          text("↑↓ j/k  Espacio alternar   Enter confirmar   Esc cancelar") |
                              color(Color::GrayLight),
                      })) |
               border;
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
    std::fprintf(stderr, "tgdb-bundle-wizard: no se pudo escribir %s\n", config_path.c_str());
    return 1;
  }
  return 0;
}
