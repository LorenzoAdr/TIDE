#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app/app_settings.hpp"
#include "app/workspace_config.hpp"
#include "ftxui/component/component_base.hpp"
#include "ui/path_browser.hpp"
#include "util/clang_format_config.hpp"

namespace tgdb {

enum class SettingsPanel {
  kGeneral,
  kWorkspace,
  kFormat,
  kIncludePaths,
  kCompileCommands,
  kPathMappings,
  kPathBrowser,
  kUiColors,
};

enum class PathBrowserPurpose {
  kIncludePath,
  kMappingHostPath,
};

using SettingsApplyCallback = std::function<void(const AppSettings&)>;
using WorkspaceSettingsApplyCallback = std::function<void(const WorkspaceConfig&)>;
using ClangFormatApplyCallback = std::function<void(const ClangFormatConfig&)>;

struct SettingsModalState {
  bool open = false;
  SettingsPanel panel = SettingsPanel::kGeneral;
  PathBrowserPurpose path_browser_purpose = PathBrowserPurpose::kIncludePath;
  int selected = 0;
  int include_path_selected = 0;
  int compile_commands_selected = 0;
  int mapping_selected = 0;
  int docker_container_selected = 0;
  bool draft_lsp_enabled = true;
  bool draft_live_lsp_completion_enabled = true;
  bool draft_show_diagnostic_suffixes = true;
  bool draft_sticky_scroll_enabled = true;
  bool draft_indent_guides_enabled = true;
  bool draft_overview_ruler_enabled = true;
  bool draft_secondary_panel_enabled = true;
  bool draft_force_bundled_clangd = false;
  bool draft_force_bundled_gdb = false;
  bool draft_monitor_enabled = false;
  bool draft_clangd_use_gcc_query_driver = true;
  bool draft_clangd_background_index = false;
  theme::ThemeMode draft_theme = theme::ThemeMode::kDark;
  theme::UiColorPreset draft_ui_colors_preset = theme::UiColorPreset::kDarkClassic;
  theme::UiColorOverrides draft_ui_colors;
  int ui_colors_selected = 0;
  bool ui_colors_editing = false;
  int ui_colors_edit_row = -1;
  int ui_colors_palette_selected = 0;
  std::optional<theme::ColorRgb> ui_colors_edit_original;
  CompileCommandsSettings draft_compile_commands;
  std::vector<std::string> draft_clangd_extra_include_paths;
  ClangFormatConfig draft_clang_format;
  bool clang_format_file_exists = false;
  ClangFormatApplyCallback clang_format_changed_callback;
  std::vector<std::string> docker_container_names;
  PathBrowserState path_browser;
  bool has_workspace = false;
  std::string workspace_root;
};

ftxui::Component MakeSettingsModalOverlay(ftxui::Component main, SettingsModalState* state,
                                          AppSettings* settings, SettingsApplyCallback on_apply,
                                          WorkspaceSettingsApplyCallback on_workspace_apply,
                                          ClangFormatApplyCallback on_clang_format_apply = {});

void open_settings_modal(SettingsModalState* state, const AppSettings& settings,
                         const std::string& workspace_root,
                         const WorkspaceConfig& workspace_config);
void close_settings_modal(SettingsModalState* state, AppSettings* settings,
                          SettingsApplyCallback on_apply,
                          WorkspaceSettingsApplyCallback on_workspace_apply,
                          ClangFormatApplyCallback on_clang_format_apply = {});

}  // namespace tgdb
