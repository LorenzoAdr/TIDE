#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "app/app_settings.hpp"
#include "app/workspace_config.hpp"
#include "i18n/locale.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ui/keybind/key_binding_registry.hpp"
#include "ui/path_browser.hpp"
#include "util/clang_format_config.hpp"
#include "util/tools_status.hpp"

namespace tuide {

enum class SettingsPanel {
  kGeneral,
  kVisualHighlight,
  kWorkspace,
  kFormat,
  kStatus,
  kShortcuts,
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
using ToolsStatusProvider = std::function<ToolsStatusSnapshot()>;

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
  bool draft_visual_highlight_enabled = true;
  bool draft_visual_brace_pair_colors_enabled = true;
  bool draft_visual_matching_bracket_enabled = true;
  bool draft_visual_scope_background_enabled = true;
  bool draft_visual_scope_brace_highlight_enabled = true;
  bool draft_visual_selection_occurrences_enabled = true;
  bool draft_visual_code_folding_enabled = true;
  bool draft_rich_session_enabled = true;
  int draft_scope_highlight_strength = 58;
  bool draft_animations_enabled = true;
  bool draft_overview_ruler_enabled = true;
  bool draft_secondary_panel_enabled = true;
  bool draft_helix_mode_enabled = false;
  bool draft_workspace_auto_detect_enabled = true;
  bool draft_force_bundled_clangd = false;
  bool draft_force_bundled_gdb = false;
  bool draft_monitor_enabled = false;
  bool draft_perf_dump_enabled = false;
  bool draft_show_all_workspace_files = false;
  i18n::UiLocale draft_ui_locale = i18n::UiLocale::kAuto;
  IconMode draft_icon_mode = IconMode::Auto;
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
  theme::UiColorPreset ui_colors_edit_original_preset = theme::UiColorPreset::kDarkClassic;
  CompileCommandsSettings draft_compile_commands;
  std::vector<std::string> draft_clangd_extra_include_paths;
  ClangFormatConfig draft_clang_format;
  bool clang_format_file_exists = false;
  ClangFormatApplyCallback clang_format_changed_callback;
  std::vector<std::string> docker_container_names;
  PathBrowserState path_browser;
  bool has_workspace = false;
  std::string workspace_root;
  int body_scroll = 0;
  int body_scroll_total = 0;
  SettingsPanel body_scroll_panel = SettingsPanel::kGeneral;

  ftxui::Box tab_general_box;
  ftxui::Box tab_visual_highlight_box;
  ftxui::Box tab_workspace_box;
  ftxui::Box tab_theme_box;
  ftxui::Box tab_format_box;
  ftxui::Box tab_shortcuts_box;
  ftxui::Box tab_status_box;
  ftxui::Box body_box;
  SettingsPanel click_layout_panel = SettingsPanel::kGeneral;
  struct ClickTarget {
    int row = -1;
    int index = -1;
  };
  std::vector<ClickTarget> click_targets;
  int ui_palette_row_start = -1;
  int ui_palette_row_count = 0;
  ToolsStatusProvider tools_status_provider;
  ToolsStatusSnapshot tools_status_cache;
  bool tools_status_cache_valid = false;
  std::chrono::steady_clock::time_point tools_status_fetched_at{};
  // Snapshot at open — used to avoid restarting LSP/shell/index on Escape with no edits.
  WorkspaceConfig workspace_baseline;
  ClangFormatConfig clang_format_baseline;
  bool show_all_workspace_files_baseline = false;

  // Shortcuts tab (non-Helix keybindings).
  std::vector<KeyBindingOverride> draft_key_overrides;
  int shortcuts_selected = 0;
  bool shortcuts_recording = false;
  std::string shortcuts_message;
};

bool settings_modal_handle_mouse(SettingsModalState* state, ftxui::Event event);

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

}  // namespace tuide
