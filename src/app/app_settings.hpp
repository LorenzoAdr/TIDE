#pragma once

#include <string>

namespace tgdb {

struct AppSettings {
  bool lsp_enabled = true;
  bool show_diagnostic_suffixes = true;

  static AppSettings load();
  bool save() const;

  static std::string config_path();
};

}  // namespace tgdb
