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
constexpr int kBundleGdb = 2;
constexpr int kForceGdb = 3;
constexpr int kOptionCount = 4;

struct BundleConfig {
  bool bundle_clangd = false;
  bool force_clangd = false;
  bool bundle_gdb = false;
  bool force_gdb = false;
};

struct WizardState {
  BundleConfig draft;
  int selected = 0;
};

std::string checkbox_label(bool checked, const std::string& text) {
  return std::string(checked ? "[x] " : "[ ] ") + text;
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
  while (std::getline(input, line)) {
    if (line.rfind("BUNDLE_CLANGD=", 0) == 0) {
      config->bundle_clangd = line.substr(14) == "1";
    } else if (line.rfind("BUNDLE_CLANGD_FORCE=", 0) == 0) {
      config->force_clangd = line.substr(20) == "1";
    } else if (line.rfind("BUNDLE_GDB=", 0) == 0) {
      config->bundle_gdb = line.substr(11) == "1";
    } else if (line.rfind("BUNDLE_GDB_FORCE=", 0) == 0) {
      config->force_gdb = line.substr(17) == "1";
    }
  }
}

bool save_bundle_config(const std::string& path, const BundleConfig& config) {
  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return false;
  }
  output << "BUNDLE_CLANGD=" << (config.bundle_clangd ? "1" : "0") << '\n';
  output << "BUNDLE_CLANGD_FORCE=" << (config.force_clangd ? "1" : "0") << '\n';
  output << "BUNDLE_GDB=" << (config.bundle_gdb ? "1" : "0") << '\n';
  output << "BUNDLE_GDB_FORCE=" << (config.force_gdb ? "1" : "0") << '\n';
  return static_cast<bool>(output);
}

bool option_enabled(const WizardState& state, int index) {
  switch (index) {
    case kBundleClangd:
    case kBundleGdb:
      return true;
    case kForceClangd:
      return state.draft.bundle_clangd;
    case kForceGdb:
      return state.draft.bundle_gdb;
    default:
      return false;
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
    case kBundleGdb:
      state->draft.bundle_gdb = !state->draft.bundle_gdb;
      if (state->draft.bundle_gdb && !state->draft.force_gdb) {
        state->draft.force_gdb = true;
      }
      if (!state->draft.bundle_gdb) {
        state->draft.force_gdb = false;
      }
      break;
    case kForceGdb:
      state->draft.force_gdb = !state->draft.force_gdb;
      break;
    default:
      break;
  }
}

bool option_checked(const WizardState& state, int index) {
  switch (index) {
    case kBundleClangd:
      return state.draft.bundle_clangd;
    case kForceClangd:
      return state.draft.force_clangd;
    case kBundleGdb:
      return state.draft.bundle_gdb;
    case kForceGdb:
      return state.draft.force_gdb;
    default:
      return false;
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
            {"Incluir gdb-static Full en el binario",
             "gdb-static portable con DAP (~+25-40 MB comprimido)"},
            {"Forzar gdb embebido (sin fallback al sistema)",
             "Ignora gdb en PATH salvo GDB_PATH"},
        };

        for (int i = 0; i < kOptionCount; ++i) {
          const bool enabled = option_enabled(state, i);
          const bool selected = i == state.selected;
          const bool checked = option_checked(state, i);
          Element title =
              text(checkbox_label(checked, options[i].first)) |
              color(enabled ? (selected ? Color::Cyan : Color::White) : Color::GrayDark) | bold;
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
