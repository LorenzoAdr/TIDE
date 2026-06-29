#include "ui/settings_modal.hpp"

#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kLsp = 0;
constexpr int kDiagnosticSuffixes = 1;
constexpr int kStickyScroll = 2;
constexpr int kSecondaryPanel = 3;
constexpr int kOptionCount = 4;

struct SettingsOption {
  const char* label;
  const char* description;
};

const std::vector<SettingsOption>& settings_options() {
  static const std::vector<SettingsOption> options = {
      {"clangd / LSP activo",
       "Outline, completado, diagnósticos y resaltado semántico vía clangd"},
      {"Sufijos de avisos en el código",
       "Muestra mensajes cortos de clangd al final de cada línea"},
      {"Sticky scroll en el editor",
       "Muestra encabezados de ámbito fijos al hacer scroll en el código"},
      {"Panel secundario (outline / búsqueda)",
       "Muestra la tercera columna con outline y búsqueda en el workspace"},
  };
  return options;
}

std::string checkbox_label(bool checked, const std::string& text) {
  return std::string(checked ? "[x] " : "[ ] ") + text;
}

bool option_checked(const SettingsModalState* state, int index) {
  if (state == nullptr) {
    return false;
  }
  switch (index) {
    case kLsp:
      return state->draft_lsp_enabled;
    case kDiagnosticSuffixes:
      return state->draft_show_diagnostic_suffixes;
    case kStickyScroll:
      return state->draft_sticky_scroll_enabled;
    case kSecondaryPanel:
      return state->draft_secondary_panel_enabled;
    default:
      return false;
  }
}

void toggle_option(SettingsModalState* state, int index) {
  if (state == nullptr) {
    return;
  }
  switch (index) {
    case kLsp:
      state->draft_lsp_enabled = !state->draft_lsp_enabled;
      break;
    case kDiagnosticSuffixes:
      state->draft_show_diagnostic_suffixes = !state->draft_show_diagnostic_suffixes;
      break;
    case kStickyScroll:
      state->draft_sticky_scroll_enabled = !state->draft_sticky_scroll_enabled;
      break;
    case kSecondaryPanel:
      state->draft_secondary_panel_enabled = !state->draft_secondary_panel_enabled;
      break;
    default:
      break;
  }
}

void clamp_selection(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  state->selected = std::max(0, std::min(state->selected, kOptionCount - 1));
}

bool handle_settings_keys(SettingsModalState* state, Event event) {
  if (state == nullptr || !state->open) {
    return false;
  }

  if (event == Event::Escape || event == Event::F10) {
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->selected += 1;
    clamp_selection(state);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->selected -= 1;
    clamp_selection(state);
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    toggle_option(state, state->selected);
    return true;
  }
  return true;
}

}  // namespace

void open_settings_modal(SettingsModalState* state, const AppSettings& settings) {
  if (state == nullptr) {
    return;
  }
  state->open = true;
  state->selected = 0;
  state->draft_lsp_enabled = settings.lsp_enabled;
  state->draft_show_diagnostic_suffixes = settings.show_diagnostic_suffixes;
  state->draft_sticky_scroll_enabled = settings.sticky_scroll_enabled;
  state->draft_secondary_panel_enabled = settings.secondary_panel_enabled;
}

void close_settings_modal(SettingsModalState* state, AppSettings* settings,
                          SettingsApplyCallback on_apply) {
  if (state == nullptr || settings == nullptr) {
    return;
  }
  settings->lsp_enabled = state->draft_lsp_enabled;
  settings->show_diagnostic_suffixes = state->draft_show_diagnostic_suffixes;
  settings->sticky_scroll_enabled = state->draft_sticky_scroll_enabled;
  settings->secondary_panel_enabled = state->draft_secondary_panel_enabled;
  settings->save();
  if (on_apply) {
    on_apply(*settings);
  }
  state->open = false;
  state->selected = 0;
}

Component MakeSettingsModalOverlay(Component main, SettingsModalState* state,
                                 AppSettings* settings, SettingsApplyCallback on_apply) {
  return Renderer(
      CatchEvent(main, [state, settings, on_apply](Event event) {
        if (state == nullptr || !state->open) {
          return false;
        }
        if (event == Event::Escape || event == Event::F10) {
          close_settings_modal(state, settings, on_apply);
          return true;
        }
        return handle_settings_keys(state, event);
      }),
      [main, state] {
        Element base = main->Render();
        if (state == nullptr || !state->open) {
          return base;
        }

        clamp_selection(state);

        Elements rows;
        const auto& options = settings_options();
        for (int i = 0; i < kOptionCount; ++i) {
          const auto& option = options[static_cast<std::size_t>(i)];
          const bool selected = i == state->selected;
          const bool checked = option_checked(state, i);

          Element title = text(checkbox_label(checked, option.label)) |
                          color(selected ? theme::Accent() : theme::Header()) | bold;
          if (selected) {
            title = title | inverted;
          }
          rows.push_back(title);
          rows.push_back(text("    " + std::string(option.description)) | color(theme::Muted()));
          rows.push_back(text(""));
        }
        if (!rows.empty()) {
          rows.pop_back();
        }

        std::string config_note;
        const std::string path = AppSettings::config_path();
        if (!path.empty()) {
          config_note = "Guardado en: " + path;
        } else {
          config_note = "HOME no definido; la configuración no se guardará en disco";
        }

        Element dialog = ModalWindow(
            text("Configuración") | color(theme::Accent()),
            vbox({
                vbox(std::move(rows)),
                separator() | color(theme::AccentDim()),
                text(config_note) | color(theme::Muted()),
                text("↑↓ j/k  Espacio/Enter alternar   F10/Esc cerrar y guardar") |
                    color(theme::Muted()),
            }));

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
