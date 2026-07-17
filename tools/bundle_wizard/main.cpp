#include <cstdio>
#include <fstream>
#include <string>
#include <utility>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

namespace {

constexpr int kBundleClangd = 0;
constexpr int kForceClangd = 1;
constexpr int kGdbKind = 2;
constexpr int kForceGdb = 3;
constexpr int kPythonKind = 4;
constexpr int kForcePython = 5;
constexpr int kBundleBashLs = 6;
constexpr int kForceBashLs = 7;
constexpr int kBundleTexlab = 8;
constexpr int kForceTexlab = 9;
constexpr int kBundleBashDap = 10;
constexpr int kForceBashDap = 11;
constexpr int kOptionCount = 12;

constexpr int kGdbNone = 0;
constexpr int kGdbStatic = 1;
constexpr int kGdbCoreAnalyzer = 2;

constexpr int kPythonNone = 0;
constexpr int kPythonLspMin = 1;
constexpr int kPythonFull = 2;

struct BundleConfig {
  bool bundle_clangd = false;
  bool force_clangd = false;
  int gdb_kind = kGdbNone;
  bool force_gdb = false;
  int python_kind = kPythonNone;
  bool force_python = false;
  bool bundle_bash_ls = false;
  bool force_bash_ls = false;
  bool bundle_texlab = false;
  bool force_texlab = false;
  bool bundle_bash_dap = false;
  bool force_bash_dap = false;
};

struct WizardState {
  BundleConfig draft;
  int selected = 0;
};

std::string gdb_kind_label(int kind) {
  switch (kind) {
    case kGdbStatic:
      return "gdb-static (musl, sin deps, sin Core Analyzer)";
    case kGdbCoreAnalyzer:
      return "gdb + Core Analyzer (obj/ref/heap, deps dinámicas)";
    default:
      return "ninguno (gdb del sistema)";
  }
}

std::string python_kind_label(int kind) {
  switch (kind) {
    case kPythonLspMin:
      return "A: basedpyright (~+70–90 MB; Python del host)";
    case kPythonFull:
      return "B: CPython + basedpyright + debugpy (~+90–120 MB)";
    default:
      return "ninguno (herramientas Python del sistema)";
  }
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
  while (std::getline(input, line)) {
    if (line.rfind("BUNDLE_CLANGD=", 0) == 0) {
      config->bundle_clangd = line.substr(14) == "1";
    } else if (line.rfind("BUNDLE_CLANGD_FORCE=", 0) == 0) {
      config->force_clangd = line.substr(20) == "1";
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
      legacy_bundle_gdb = line.substr(11) == "1";
    } else if (line.rfind("BUNDLE_GDB_FORCE=", 0) == 0) {
      config->force_gdb = line.substr(17) == "1";
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
      config->force_python = line.substr(20) == "1";
    } else if (line.rfind("BUNDLE_BASH_LS=", 0) == 0) {
      config->bundle_bash_ls = line.substr(15) == "1";
    } else if (line.rfind("BUNDLE_BASH_LS_FORCE=", 0) == 0) {
      config->force_bash_ls = line.substr(21) == "1";
    } else if (line.rfind("BUNDLE_TEXLAB=", 0) == 0) {
      config->bundle_texlab = line.substr(14) == "1";
    } else if (line.rfind("BUNDLE_TEXLAB_FORCE=", 0) == 0) {
      config->force_texlab = line.substr(20) == "1";
    } else if (line.rfind("BUNDLE_BASH_DAP=", 0) == 0) {
      config->bundle_bash_dap = line.substr(16) == "1";
    } else if (line.rfind("BUNDLE_BASH_DAP_FORCE=", 0) == 0) {
      config->force_bash_dap = line.substr(22) == "1";
    }
  }
  if (config->gdb_kind == kGdbNone && legacy_bundle_gdb) {
    config->gdb_kind = kGdbStatic;
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
  output << "BUNDLE_CLANGD_FORCE=" << (config.force_clangd ? "1" : "0") << '\n';
  output << "GDB_BUNDLE_KIND=" << kind_name << '\n';
  output << "BUNDLE_GDB=" << (config.gdb_kind != kGdbNone ? "1" : "0") << '\n';
  output << "BUNDLE_GDB_FORCE=" << (config.force_gdb ? "1" : "0") << '\n';
  output << "PYTHON_BUNDLE_KIND=" << python_kind_name << '\n';
  output << "BUNDLE_PYTHON_FORCE=" << (config.force_python ? "1" : "0") << '\n';
  output << "BUNDLE_BASH_LS=" << (config.bundle_bash_ls ? "1" : "0") << '\n';
  output << "BUNDLE_BASH_LS_FORCE=" << (config.force_bash_ls ? "1" : "0") << '\n';
  output << "BUNDLE_TEXLAB=" << (config.bundle_texlab ? "1" : "0") << '\n';
  output << "BUNDLE_TEXLAB_FORCE=" << (config.force_texlab ? "1" : "0") << '\n';
  output << "BUNDLE_BASH_DAP=" << (config.bundle_bash_dap ? "1" : "0") << '\n';
  output << "BUNDLE_BASH_DAP_FORCE=" << (config.force_bash_dap ? "1" : "0") << '\n';
  return static_cast<bool>(output);
}

bool option_enabled(const WizardState& state, int index) {
  switch (index) {
    case kBundleClangd:
    case kGdbKind:
    case kPythonKind:
      return true;
    case kForceClangd:
      return state.draft.bundle_clangd;
    case kForceGdb:
      return state.draft.gdb_kind != kGdbNone;
    case kForcePython:
      return state.draft.python_kind != kPythonNone;
    case kBundleBashLs:
    case kBundleTexlab:
    case kBundleBashDap:
      return true;
    case kForceBashLs:
      return state.draft.bundle_bash_ls;
    case kForceTexlab:
      return state.draft.bundle_texlab;
    case kForceBashDap:
      return state.draft.bundle_bash_dap;
    default:
      return false;
  }
}

void cycle_gdb_kind(WizardState* state) {
  if (state == nullptr) {
    return;
  }
  state->draft.gdb_kind = (state->draft.gdb_kind + 1) % 3;
  if (state->draft.gdb_kind != kGdbNone && !state->draft.force_gdb) {
    state->draft.force_gdb = true;
  }
  if (state->draft.gdb_kind == kGdbNone) {
    state->draft.force_gdb = false;
  }
}

void cycle_python_kind(WizardState* state) {
  if (state == nullptr) {
    return;
  }
  state->draft.python_kind = (state->draft.python_kind + 1) % 3;
  if (state->draft.python_kind != kPythonNone && !state->draft.force_python) {
    state->draft.force_python = true;
  }
  if (state->draft.python_kind == kPythonNone) {
    state->draft.force_python = false;
  }
}

void toggle_option(WizardState* state, int index) {
  if (state == nullptr || !option_enabled(*state, index)) {
    return;
  }
  switch (index) {
    case kBundleClangd:
      state->draft.bundle_clangd = !state->draft.bundle_clangd;
      if (state->draft.bundle_clangd && !state->draft.force_clangd) {
        state->draft.force_clangd = true;
      }
      if (!state->draft.bundle_clangd) {
        state->draft.force_clangd = false;
      }
      break;
    case kForceClangd:
      state->draft.force_clangd = !state->draft.force_clangd;
      break;
    case kGdbKind:
      cycle_gdb_kind(state);
      break;
    case kForceGdb:
      state->draft.force_gdb = !state->draft.force_gdb;
      break;
    case kPythonKind:
      cycle_python_kind(state);
      break;
    case kForcePython:
      state->draft.force_python = !state->draft.force_python;
      break;
    case kBundleBashLs:
      state->draft.bundle_bash_ls = !state->draft.bundle_bash_ls;
      if (state->draft.bundle_bash_ls && !state->draft.force_bash_ls) {
        state->draft.force_bash_ls = true;
      }
      if (!state->draft.bundle_bash_ls) {
        state->draft.force_bash_ls = false;
      }
      break;
    case kForceBashLs:
      state->draft.force_bash_ls = !state->draft.force_bash_ls;
      break;
    case kBundleTexlab:
      state->draft.bundle_texlab = !state->draft.bundle_texlab;
      if (state->draft.bundle_texlab && !state->draft.force_texlab) {
        state->draft.force_texlab = true;
      }
      if (!state->draft.bundle_texlab) {
        state->draft.force_texlab = false;
      }
      break;
    case kForceTexlab:
      state->draft.force_texlab = !state->draft.force_texlab;
      break;
    case kBundleBashDap:
      state->draft.bundle_bash_dap = !state->draft.bundle_bash_dap;
      if (state->draft.bundle_bash_dap && !state->draft.force_bash_dap) {
        state->draft.force_bash_dap = true;
      }
      if (!state->draft.bundle_bash_dap) {
        state->draft.force_bash_dap = false;
      }
      break;
    case kForceBashDap:
      state->draft.force_bash_dap = !state->draft.force_bash_dap;
      break;
    default:
      break;
  }
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

  auto component = CatchEvent(
      Renderer([&] {
        Elements rows;
        const std::pair<const char*, const char*> options[kOptionCount] = {
            {"Incluir clangd en el binario",
             "Release oficial (~+35 MB comprimido, ~87 MB total con clangd)"},
            {"Forzar clangd embebido (sin fallback al sistema)",
             "Ignora clangd en PATH salvo CLANGD_PATH"},
            {"GDB embebido",
             "Espacio alterna: ninguno → gdb-static → gdb+Core Analyzer"},
            {"Forzar gdb embebido (sin fallback al sistema)",
             "Ignora gdb en PATH salvo GDB_PATH"},
            {"Herramientas Python embebidas",
             "Espacio alterna: ninguna → A (LSP min) → B (completo)"},
            {"Forzar herramientas Python embebidas",
             "Ignora basedpyright/debugpy del sistema salvo overrides de entorno"},
            {"Incluir bash-language-server en el binario",
             "LSP para .sh/.bash (~+Node + npm package)"},
            {"Forzar bash-language-server embebido",
             "Ignora bash-language-server en PATH salvo BASH_LANGUAGE_SERVER_PATH"},
            {"Incluir TexLab en el binario",
             "LSP LaTeX + chktex (.tex, .sty, .cls)"},
            {"Forzar TexLab embebido",
             "Ignora texlab en PATH salvo TEXLAB_PATH"},
            {"Incluir adaptador Bash DAP en el binario",
             "Depuración de scripts shell (bashdb + Node; requiere bash del host)"},
            {"Forzar Bash DAP embebido",
             "Ignora adaptador Bash DAP del sistema salvo overrides de entorno"},
        };

        for (int i = 0; i < kOptionCount; ++i) {
          const bool enabled = option_enabled(state, i);
          const bool selected = i == state.selected;
          std::string label = options[i].first;
          if (i == kGdbKind) {
            label += ": " + gdb_kind_label(state.draft.gdb_kind);
          } else if (i == kPythonKind) {
            label += ": " + python_kind_label(state.draft.python_kind);
          } else {
            const bool checked =
                i == kBundleClangd    ? state.draft.bundle_clangd
                : i == kForceClangd   ? state.draft.force_clangd
                : i == kForceGdb      ? state.draft.force_gdb
                : i == kForcePython   ? state.draft.force_python
                : i == kBundleBashLs  ? state.draft.bundle_bash_ls
                : i == kForceBashLs   ? state.draft.force_bash_ls
                : i == kBundleTexlab  ? state.draft.bundle_texlab
                : i == kForceTexlab   ? state.draft.force_texlab
                : i == kBundleBashDap ? state.draft.bundle_bash_dap
                : i == kForceBashDap  ? state.draft.force_bash_dap
                                      : false;
            label = std::string(checked ? "[x] " : "[ ] ") + label;
          }
          Element title = text(label) |
                          color(enabled ? (selected ? Color::Cyan : Color::White)
                                        : Color::GrayDark) |
                          bold;
          if (selected && enabled) {
            title = title | inverted;
          }
          rows.push_back(title);
          rows.push_back(text(std::string("    ") + options[i].second) | color(Color::GrayLight));
          rows.push_back(text(""));
        }
        if (!rows.empty()) {
          rows.pop_back();
        }

        return window(text("tgdb — componentes embebidos") | bold | center,
                      vbox({
                          text("Selecciona qué incluir en el binario de tgdb:"),
                          separator(),
                          vbox(std::move(rows)),
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
