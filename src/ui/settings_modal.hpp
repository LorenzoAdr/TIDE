#pragma once

#include <functional>

#include "app/app_settings.hpp"
#include "ftxui/component/component_base.hpp"

namespace tgdb {

struct SettingsModalState {
  bool open = false;
  int selected = 0;
  bool draft_lsp_enabled = true;
  bool draft_show_diagnostic_suffixes = true;
  bool draft_sticky_scroll_enabled = true;
  bool draft_secondary_panel_enabled = true;
  bool draft_force_bundled_clangd = false;
  bool draft_force_bundled_gdb = false;
};

using SettingsApplyCallback = std::function<void(const AppSettings&)>;

ftxui::Component MakeSettingsModalOverlay(ftxui::Component main, SettingsModalState* state,
                                          AppSettings* settings, SettingsApplyCallback on_apply);

void open_settings_modal(SettingsModalState* state, const AppSettings& settings);
void close_settings_modal(SettingsModalState* state, AppSettings* settings,
                          SettingsApplyCallback on_apply);

}  // namespace tgdb
