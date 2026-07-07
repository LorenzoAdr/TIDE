#pragma once

#include <string>

#include "i18n/locale.hpp"
#include "ui/glyphs.hpp"

namespace tgdb {

struct AppSettings {
  bool lsp_enabled = true;
  bool live_lsp_completion_enabled = true;
  bool show_diagnostic_suffixes = true;
  bool sticky_scroll_enabled = true;
  bool indent_guides_enabled = true;
  bool scope_highlight_enabled = true;
  int scope_highlight_strength = 58;
  bool animations_enabled = true;
  bool overview_ruler_enabled = true;
  bool secondary_panel_enabled = true;
  bool force_bundled_clangd = false;
  bool force_bundled_gdb = false;
  bool monitor_enabled = false;
  bool perf_dump_enabled = false;
  bool passive_mode_enabled = true;
  int grace_window_ms = 1000;
  bool lsp_hover_on_click_only = true;
  bool show_all_workspace_files = false;
  bool helix_mode_enabled = false;
  bool workspace_auto_detect_enabled = true;
  IconMode icon_mode = IconMode::Auto;
  i18n::UiLocale ui_locale = i18n::UiLocale::kAuto;

  static AppSettings load();
  bool save() const;

  static std::string config_path();
};

}  // namespace tgdb
