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
constexpr int kOptionCount = 4;

constexpr int kGdbNone = 0;
constexpr int kGdbStatic = 1;
constexpr int kGdbCoreAnalyzer = 2;

struct BundleConfig {
  bool bundle_clangd = false;
  bool force_clangd = false;
  int gdb_kind = kGdbNone;
  bool force_gdb = false;
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
  output << "BUNDLE_CLANGD=" << (config.bundle_clangd ? "1" : "0") << '\n';
  output << "BUNDLE_CLANGD_FORCE=" << (config.force_clangd ? "1" : "0") << '\n';
  output << "GDB_BUNDLE_KIND=" << kind_name << '\n';
  output << "BUNDLE_GDB=" << (config.gdb_kind != kGdbNone ? "1" : "0") << '\n';
  output << "BUNDLE_GDB_FORCE=" << (config.force_gdb ? "1" : "0") << '\n';
  return static_cast<bool>(output);
}

bool option_enabled(const WizardState& state, int index) {
  switch (index) {
    case kBundleClangd:
    case kGdbKind:
      return true;
    case kForceClangd:
      return state.draft.bundle_clangd;
    case kForceGdb:
      return state.draft.gdb_kind != kGdbNone;
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
        };

        for (int i = 0; i < kOptionCount; ++i) {
          const bool enabled = option_enabled(state, i);
          const bool selected = i == state.selected;
          std::string label = options[i].first;
          if (i == kGdbKind) {
            label += ": " + gdb_kind_label(state.draft.gdb_kind);
          } else {
            const bool checked =
                i == kBundleClangd   ? state.draft.bundle_clangd
                : i == kForceClangd  ? state.draft.force_clangd
                : i == kForceGdb     ? state.draft.force_gdb
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
