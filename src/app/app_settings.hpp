#pragma once

#include <string>

#include "ui/glyphs.hpp"

namespace tgdb {

struct AppSettings {
  bool lsp_enabled = true;
  bool live_lsp_completion_enabled = true;
  bool show_diagnostic_suffixes = true;
  bool sticky_scroll_enabled = true;
  bool indent_guides_enabled = true;
  bool overview_ruler_enabled = true;
  bool secondary_panel_enabled = true;
  bool force_bundled_clangd = false;
  bool force_bundled_gdb = false;
  bool monitor_enabled = false;
  bool show_all_workspace_files = false;
  IconMode icon_mode = IconMode::Auto;

  static AppSettings load();
  bool save() const;

  static std::string config_path();
};

}  // namespace tgdb
