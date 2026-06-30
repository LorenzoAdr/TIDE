#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app/app_settings.hpp"
#include "app/workspace_config.hpp"
#include "ftxui/component/component_base.hpp"
#include "ui/path_browser.hpp"

namespace tgdb {

enum class SettingsPanel {
  kMain,
  kIncludePaths,
  kCompileCommands,
  kPathMappings,
  kPathBrowser,
};

enum class PathBrowserPurpose {
  kIncludePath,
  kMappingHostPath,
};

struct SettingsModalState {
  bool open = false;
  SettingsPanel panel = SettingsPanel::kMain;
  PathBrowserPurpose path_browser_purpose = PathBrowserPurpose::kIncludePath;
  int selected = 0;
  int include_path_selected = 0;
  int compile_commands_selected = 0;
  int mapping_selected = 0;
  int docker_container_selected = 0;
  bool draft_lsp_enabled = true;
  bool draft_show_diagnostic_suffixes = true;
  bool draft_sticky_scroll_enabled = true;
  bool draft_secondary_panel_enabled = true;
  bool draft_force_bundled_clangd = false;
  bool draft_force_bundled_gdb = false;
  bool draft_clangd_use_gcc_query_driver = true;
  bool draft_clangd_background_index = false;
  theme::ThemeMode draft_theme = theme::ThemeMode::kDark;
  CompileCommandsSettings draft_compile_commands;
  std::vector<std::string> draft_clangd_extra_include_paths;
  std::vector<std::string> docker_container_names;
  PathBrowserState path_browser;
  bool has_workspace = false;
  std::string workspace_root;
};

using SettingsApplyCallback = std::function<void(const AppSettings&)>;
using WorkspaceSettingsApplyCallback = std::function<void(const WorkspaceConfig&)>;

ftxui::Component MakeSettingsModalOverlay(ftxui::Component main, SettingsModalState* state,
                                          AppSettings* settings, SettingsApplyCallback on_apply,
                                          WorkspaceSettingsApplyCallback on_workspace_apply);

void open_settings_modal(SettingsModalState* state, const AppSettings& settings,
                         const std::string& workspace_root,
                         const WorkspaceConfig& workspace_config);
void close_settings_modal(SettingsModalState* state, AppSettings* settings,
                          SettingsApplyCallback on_apply,
                          WorkspaceSettingsApplyCallback on_workspace_apply);

}  // namespace tgdb
