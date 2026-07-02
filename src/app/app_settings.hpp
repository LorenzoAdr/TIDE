#pragma once

#include <string>

namespace tgdb {

struct AppSettings {
  bool lsp_enabled = true;
  bool show_diagnostic_suffixes = true;
  bool sticky_scroll_enabled = true;
  bool overview_ruler_enabled = true;
  bool secondary_panel_enabled = true;
  bool force_bundled_clangd = false;
  bool force_bundled_gdb = false;

  static AppSettings load();
  bool save() const;

  static std::string config_path();
};

}  // namespace tgdb
