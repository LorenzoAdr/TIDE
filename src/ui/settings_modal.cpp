#include "ui/settings_modal.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "i18n/locale.hpp"
#include "i18n/tr.hpp"
#include "ui/panel.hpp"
#include "ui/glyphs.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/theme.hpp"
#include "util/clang_format_config.hpp"
#include "util/compile_commands_remap.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kSettingsBodyHeight = 24;

struct SettingsBodyContent {
  Elements header;
  Elements rows;
  int focus_row = 0;
};

void clamp_settings_body_scroll(SettingsModalState* state, int total_lines) {
  if (state == nullptr) {
    return;
  }
  const int max_scroll = std::max(0, total_lines - kSettingsBodyHeight);
  state->body_scroll = std::max(0, std::min(state->body_scroll, max_scroll));
}

void scroll_settings_body_to_row(SettingsModalState* state, int row, int total_lines) {
  if (state == nullptr || row < 0) {
    return;
  }
  if (row < state->body_scroll) {
    state->body_scroll = row;
  } else if (row >= state->body_scroll + kSettingsBodyHeight) {
    state->body_scroll = row - kSettingsBodyHeight + 1;
  }
  clamp_settings_body_scroll(state, total_lines);
}

Element render_settings_scroll_body(SettingsModalState* state, SettingsBodyContent content) {
  if (state == nullptr) {
    return vbox(std::move(content.rows));
  }

  if (state->body_scroll_panel != state->panel) {
    state->body_scroll = 0;
    state->body_scroll_panel = state->panel;
  }

  const int total = static_cast<int>(content.rows.size());
  scroll_settings_body_to_row(state, content.focus_row, total);
  clamp_settings_body_scroll(state, total);

  Elements visible;
  const int end = std::min(total, state->body_scroll + kSettingsBodyHeight);
  for (int i = state->body_scroll; i < end; ++i) {
    visible.push_back(content.rows[static_cast<std::size_t>(i)]);
  }

  Element scrollable = vbox(std::move(visible)) | flex;
  if (total > kSettingsBodyHeight) {
    scrollable = hbox({
        std::move(scrollable),
        vertical_scrollbar(total, state->body_scroll, kSettingsBodyHeight, kSettingsBodyHeight),
    });
  }
  scrollable =
      scrollable | size(HEIGHT, EQUAL, kSettingsBodyHeight) | bgcolor(theme::PanelBg());

  if (content.header.empty()) {
    return scrollable;
  }
  return vbox({vbox(std::move(content.header)), std::move(scrollable)});
}

bool handle_settings_body_scroll_keys(SettingsModalState* state, Event event) {
  if (state == nullptr) {
    return false;
  }
  if (event == Event::PageDown) {
    state->body_scroll += kSettingsBodyHeight;
    return true;
  }
  if (event == Event::PageUp) {
    state->body_scroll -= kSettingsBodyHeight;
    return true;
  }
  if (event.is_mouse()) {
    const auto& m = event.mouse();
    if (m.button == Mouse::WheelUp) {
      state->body_scroll -= 3;
      return true;
    }
    if (m.button == Mouse::WheelDown) {
      state->body_scroll += 3;
      return true;
    }
  }
  return false;
}

constexpr int kUiLocale = 0;
constexpr int kTheme = 1;
constexpr int kLsp = 2;
constexpr int kLiveLspCompletion = 3;
constexpr int kDiagnosticSuffixes = 4;
constexpr int kStickyScroll = 5;
constexpr int kIndentGuides = 6;
constexpr int kOverviewRuler = 7;
constexpr int kSecondaryPanel = 8;
constexpr int kShowAllFiles = 9;
constexpr int kMonitor = 10;
constexpr int kIcons = 11;
constexpr int kHelixMode = 12;
constexpr int kWorkspaceAutoDetect = 13;
constexpr int kBaseOptions = 14;

#ifdef TGDB_HAS_BUNDLED_CLANGD
constexpr int kForceBundledClangd = kBaseOptions;
constexpr int kAfterClangd = kBaseOptions + 1;
#else
constexpr int kAfterClangd = kBaseOptions;
#endif

#ifdef TGDB_HAS_BUNDLED_GDB
constexpr int kForceBundledGdb = kAfterClangd;
constexpr int kGlobalOptionCount = kAfterClangd + 1;
#else
constexpr int kGlobalOptionCount = kAfterClangd;
#endif

constexpr int kWorkspaceGccQueryDriver = 0;
constexpr int kWorkspaceBackgroundIndex = 1;
constexpr int kWorkspaceIncludePaths = 2;
constexpr int kWorkspaceCompileCommands = 3;
constexpr int kWorkspaceUiColors = 4;
constexpr int kWorkspaceOptionCount = 5;

constexpr int kFormatBasedOnStyle = 0;
constexpr int kFormatIndentWidth = 1;
constexpr int kFormatUseTab = 2;
constexpr int kFormatTabWidth = 3;
constexpr int kFormatColumnLimit = 4;
constexpr int kFormatBreakBeforeBraces = 5;
constexpr int kFormatPointerAlignment = 6;
constexpr int kFormatSortIncludes = 7;
constexpr int kFormatIncludeBlocks = 8;
constexpr int kFormatIndentCaseLabels = 9;
constexpr int kFormatShortFunctions = 10;
constexpr int kFormatOptionCount = 11;

constexpr int kUiColorsPreset = 0;
constexpr int kUiColorsPanelBg = 1;
constexpr int kUiColorsCodeBg = 2;
constexpr int kUiColorsText = 3;
constexpr int kUiColorsTitle = 4;
constexpr int kUiColorsDirectory = 5;
constexpr int kUiColorsFile = 6;
constexpr int kUiColorsRowCount = 7;

constexpr int kPaletteCols = 8;
constexpr std::array<theme::ColorRgb, 40> kUIColorPalette = {{
    theme::ColorRgb{0, 0, 0},       theme::ColorRgb{30, 30, 30},    theme::ColorRgb{45, 45, 48},
    theme::ColorRgb{60, 60, 60},    theme::ColorRgb{28, 32, 42},    theme::ColorRgb{38, 42, 52},
    theme::ColorRgb{26, 28, 36},    theme::ColorRgb{20, 20, 20},
    theme::ColorRgb{255, 255, 255}, theme::ColorRgb{250, 249, 245}, theme::ColorRgb{236, 234, 228},
    theme::ColorRgb{245, 245, 248}, theme::ColorRgb{235, 238, 245}, theme::ColorRgb{252, 246, 236},
    theme::ColorRgb{230, 230, 230}, theme::ColorRgb{220, 220, 220},
    theme::ColorRgb{90, 170, 255},  theme::ColorRgb{0, 102, 204},   theme::ColorRgb{156, 220, 254},
    theme::ColorRgb{0, 90, 158},    theme::ColorRgb{79, 193, 255},  theme::ColorRgb{86, 156, 214},
    theme::ColorRgb{75, 110, 175},  theme::ColorRgb{100, 149, 237},
    theme::ColorRgb{212, 212, 212}, theme::ColorRgb{180, 200, 255}, theme::ColorRgb{204, 204, 204},
    theme::ColorRgb{30, 40, 60},    theme::ColorRgb{43, 43, 40},    theme::ColorRgb{220, 223, 228},
    theme::ColorRgb{58, 58, 58},    theme::ColorRgb{133, 133, 133},
    theme::ColorRgb{133, 133, 133}, theme::ColorRgb{100, 110, 130}, theme::ColorRgb{112, 112, 104},
    theme::ColorRgb{130, 140, 160}, theme::ColorRgb{96, 96, 96},    theme::ColorRgb{80, 80, 80},
    theme::ColorRgb{106, 153, 85},  theme::ColorRgb{206, 145, 120},
}};

constexpr int kPaletteCount = static_cast<int>(kUIColorPalette.size());
constexpr int kPaletteRows = (kPaletteCount + kPaletteCols - 1) / kPaletteCols;

constexpr int kCompileMode = 0;
constexpr int kCompileDetectMounts = 1;
constexpr int kCompileDockerContainer = 2;
constexpr int kCompilePathMappings = 3;
constexpr int kCompileCommandsRowCount = 4;

bool is_top_level_panel(SettingsPanel panel) {
  return panel == SettingsPanel::kGeneral || panel == SettingsPanel::kWorkspace ||
         panel == SettingsPanel::kFormat;
}

int top_level_panel_count(const SettingsModalState* state) {
  if (state != nullptr && state->has_workspace) {
    return 3;
  }
  return 1;
}

SettingsPanel top_level_panel_at(const SettingsModalState* state, int index) {
  if (index <= 0) {
    return SettingsPanel::kGeneral;
  }
  if (state != nullptr && state->has_workspace) {
    if (index == 1) {
      return SettingsPanel::kWorkspace;
    }
    return SettingsPanel::kFormat;
  }
  return SettingsPanel::kGeneral;
}

int top_level_panel_index(const SettingsModalState* state, SettingsPanel panel) {
  switch (panel) {
    case SettingsPanel::kGeneral:
      return 0;
    case SettingsPanel::kWorkspace:
      return state != nullptr && state->has_workspace ? 1 : 0;
    case SettingsPanel::kFormat:
      return state != nullptr && state->has_workspace ? 2 : 0;
    default:
      return 0;
  }
}

void cycle_top_level_panel(SettingsModalState* state, int delta) {
  if (state == nullptr || !is_top_level_panel(state->panel)) {
    return;
  }
  const int count = top_level_panel_count(state);
  if (count <= 1) {
    return;
  }
  int index = top_level_panel_index(state, state->panel) + delta;
  while (index < 0) {
    index += count;
  }
  index %= count;
  state->panel = top_level_panel_at(state, index);
  state->selected = 0;
}

std::string compile_commands_mode_label(CompileCommandsMode mode) {
  switch (mode) {
    case CompileCommandsMode::kAuto:
      return i18n::tr("settings.compile_commands.mode.auto");
    case CompileCommandsMode::kHost:
      return i18n::tr("settings.compile_commands.mode.host");
    case CompileCommandsMode::kRemap:
      return i18n::tr("settings.compile_commands.mode.remap");
    case CompileCommandsMode::kDockerSync:
      return i18n::tr("settings.compile_commands.mode.docker_sync");
  }
  return i18n::tr("settings.compile_commands.mode.auto");
}

void cycle_compile_commands_mode(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  switch (state->draft_compile_commands.mode) {
    case CompileCommandsMode::kAuto:
      state->draft_compile_commands.mode = CompileCommandsMode::kRemap;
      break;
    case CompileCommandsMode::kRemap:
      state->draft_compile_commands.mode = CompileCommandsMode::kDockerSync;
      break;
    case CompileCommandsMode::kDockerSync:
      state->draft_compile_commands.mode = CompileCommandsMode::kHost;
      break;
    case CompileCommandsMode::kHost:
      state->draft_compile_commands.mode = CompileCommandsMode::kAuto;
      break;
  }
}

void refresh_docker_container_names(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  state->docker_container_names = list_running_docker_containers();
  if (!state->draft_compile_commands.docker_container.empty()) {
    const auto it = std::find(state->docker_container_names.begin(),
                              state->docker_container_names.end(),
                              state->draft_compile_commands.docker_container);
    if (it != state->docker_container_names.end()) {
      state->docker_container_selected =
          static_cast<int>(std::distance(state->docker_container_names.begin(), it));
      return;
    }
  }
  state->docker_container_selected = 0;
}

void cycle_docker_container(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  if (state->docker_container_names.empty()) {
    state->draft_compile_commands.docker_container.clear();
    return;
  }
  state->docker_container_selected =
      (state->docker_container_selected + 1) %
      static_cast<int>(state->docker_container_names.size());
  state->draft_compile_commands.docker_container =
      state->docker_container_names[static_cast<std::size_t>(state->docker_container_selected)];
  invalidate_docker_mount_cache();
}

struct SettingsOption {
  std::string label;
  std::string description;
};

std::vector<SettingsOption> global_settings_options() {
  std::vector<SettingsOption> options = {
      {i18n::tr("settings.general.ui_locale.label"),
       i18n::tr("settings.general.ui_locale.description")},
      {i18n::tr("settings.general.light_theme.label"),
       i18n::tr("settings.general.light_theme.description")},
      {i18n::tr("settings.general.lsp.label"), i18n::tr("settings.general.lsp.description")},
      {i18n::tr("settings.general.live_completion.label"),
       i18n::tr("settings.general.live_completion.description")},
      {i18n::tr("settings.general.diagnostic_suffixes.label"),
       i18n::tr("settings.general.diagnostic_suffixes.description")},
      {i18n::tr("settings.general.sticky_scroll.label"),
       i18n::tr("settings.general.sticky_scroll.description")},
      {i18n::tr("settings.general.indent_guides.label"),
       i18n::tr("settings.general.indent_guides.description")},
      {i18n::tr("settings.general.overview_ruler.label"),
       i18n::tr("settings.general.overview_ruler.description")},
      {i18n::tr("settings.general.secondary_panel.label"),
       i18n::tr("settings.general.secondary_panel.description")},
      {i18n::tr("settings.general.show_all_files.label"),
       i18n::tr("settings.general.show_all_files.description")},
      {i18n::tr("settings.general.monitor.label"),
       i18n::tr("settings.general.monitor.description")},
      {i18n::tr("settings.general.icons.label"),
       i18n::tr("settings.general.icons.description")},
      {i18n::tr("settings.general.helix_mode.label"),
       i18n::tr("settings.general.helix_mode.description")},
      {i18n::tr("settings.general.workspace_auto_detect.label"),
       i18n::tr("settings.general.workspace_auto_detect.description")},
#ifdef TGDB_HAS_BUNDLED_CLANGD
      {i18n::tr("settings.general.force_bundled_clangd.label"),
       i18n::tr("settings.general.force_bundled_clangd.description")},
#endif
#ifdef TGDB_HAS_BUNDLED_GDB
      {i18n::tr("settings.general.force_bundled_gdb.label"),
       i18n::tr("settings.general.force_bundled_gdb.description")},
#endif
  };
  return options;
}

std::string checkbox_label(bool checked, const std::string& text) {
  return i18n::tr_fmt(checked ? "settings.checkbox.checked" : "settings.checkbox.unchecked",
                      {text});
}

std::string ui_locale_label(i18n::UiLocale locale) {
  switch (locale) {
    case i18n::UiLocale::kEs:
      return i18n::tr("settings.locale.es");
    case i18n::UiLocale::kEn:
      return i18n::tr("settings.locale.en");
    case i18n::UiLocale::kAuto:
    default:
      return i18n::tr("settings.locale.auto");
  }
}

std::string ui_locale_option_label(i18n::UiLocale locale, const std::string& text) {
  return i18n::tr_fmt("settings.icon_mode.checked", {ui_locale_label(locale), text});
}

void cycle_ui_locale(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  switch (state->draft_ui_locale) {
    case i18n::UiLocale::kAuto:
      state->draft_ui_locale = i18n::UiLocale::kEs;
      break;
    case i18n::UiLocale::kEs:
      state->draft_ui_locale = i18n::UiLocale::kEn;
      break;
    case i18n::UiLocale::kEn:
    default:
      state->draft_ui_locale = i18n::UiLocale::kAuto;
      break;
  }
  i18n::set_locale(state->draft_ui_locale);
}

std::string icon_mode_label(IconMode mode) {
  switch (mode) {
    case IconMode::Always:
      return i18n::tr("settings.icon.always");
    case IconMode::Never:
      return i18n::tr("settings.icon.never");
    case IconMode::Auto:
    default:
      return i18n::tr("settings.icon.auto");
  }
}

std::string icon_mode_option_label(IconMode mode, const std::string& text) {
  return i18n::tr_fmt("settings.icon_mode.checked", {icon_mode_label(mode), text});
}

bool option_checked(const SettingsModalState* state, int index) {
  if (state == nullptr) {
    return false;
  }
  switch (index) {
    case kUiLocale:
      return true;
    case kTheme:
      return state->draft_theme == theme::ThemeMode::kLight;
    case kLsp:
      return state->draft_lsp_enabled;
    case kLiveLspCompletion:
      return state->draft_live_lsp_completion_enabled;
    case kDiagnosticSuffixes:
      return state->draft_show_diagnostic_suffixes;
    case kStickyScroll:
      return state->draft_sticky_scroll_enabled;
    case kIndentGuides:
      return state->draft_indent_guides_enabled;
    case kOverviewRuler:
      return state->draft_overview_ruler_enabled;
    case kSecondaryPanel:
      return state->draft_secondary_panel_enabled;
    case kHelixMode:
      return state->draft_helix_mode_enabled;
    case kWorkspaceAutoDetect:
      return state->draft_workspace_auto_detect_enabled;
    case kShowAllFiles:
      return state->draft_show_all_workspace_files;
    case kMonitor:
      return state->draft_monitor_enabled;
    case kIcons:
      return state->draft_icon_mode == IconMode::Always;
#ifdef TGDB_HAS_BUNDLED_CLANGD
    case kForceBundledClangd:
      return state->draft_force_bundled_clangd;
#endif
#ifdef TGDB_HAS_BUNDLED_GDB
    case kForceBundledGdb:
      return state->draft_force_bundled_gdb;
#endif
    default:
      return false;
  }
}

void toggle_option(SettingsModalState* state, int index) {
  if (state == nullptr) {
    return;
  }
  switch (index) {
    case kUiLocale:
      cycle_ui_locale(state);
      break;
    case kTheme:
      state->draft_theme = state->draft_theme == theme::ThemeMode::kLight
                               ? theme::ThemeMode::kDark
                               : theme::ThemeMode::kLight;
      state->draft_ui_colors_preset = theme::UiColorPreset::kCustom;
      theme::set_mode(state->draft_theme);
      break;
    case kLsp:
      state->draft_lsp_enabled = !state->draft_lsp_enabled;
      break;
    case kLiveLspCompletion:
      state->draft_live_lsp_completion_enabled = !state->draft_live_lsp_completion_enabled;
      break;
    case kDiagnosticSuffixes:
      state->draft_show_diagnostic_suffixes = !state->draft_show_diagnostic_suffixes;
      break;
    case kStickyScroll:
      state->draft_sticky_scroll_enabled = !state->draft_sticky_scroll_enabled;
      break;
    case kIndentGuides:
      state->draft_indent_guides_enabled = !state->draft_indent_guides_enabled;
      break;
    case kOverviewRuler:
      state->draft_overview_ruler_enabled = !state->draft_overview_ruler_enabled;
      break;
    case kSecondaryPanel:
      state->draft_secondary_panel_enabled = !state->draft_secondary_panel_enabled;
      break;
    case kHelixMode:
      state->draft_helix_mode_enabled = !state->draft_helix_mode_enabled;
      break;
    case kWorkspaceAutoDetect:
      state->draft_workspace_auto_detect_enabled =
          !state->draft_workspace_auto_detect_enabled;
      break;
    case kShowAllFiles:
      state->draft_show_all_workspace_files = !state->draft_show_all_workspace_files;
      break;
    case kMonitor:
      state->draft_monitor_enabled = !state->draft_monitor_enabled;
      break;
    case kIcons:
      switch (state->draft_icon_mode) {
        case IconMode::Auto:
          state->draft_icon_mode = IconMode::Always;
          break;
        case IconMode::Always:
          state->draft_icon_mode = IconMode::Never;
          break;
        case IconMode::Never:
        default:
          state->draft_icon_mode = IconMode::Auto;
          break;
      }
      configure_glyphs(resolve_icon_mode(state->draft_icon_mode));
      break;
#ifdef TGDB_HAS_BUNDLED_CLANGD
    case kForceBundledClangd:
      state->draft_force_bundled_clangd = !state->draft_force_bundled_clangd;
      break;
#endif
#ifdef TGDB_HAS_BUNDLED_GDB
    case kForceBundledGdb:
      state->draft_force_bundled_gdb = !state->draft_force_bundled_gdb;
      break;
#endif
    default:
      break;
  }
}

void toggle_workspace_option(SettingsModalState* state, int index) {
  if (state == nullptr) {
    return;
  }
  switch (index) {
    case kWorkspaceGccQueryDriver:
      state->draft_clangd_use_gcc_query_driver = !state->draft_clangd_use_gcc_query_driver;
      break;
    case kWorkspaceBackgroundIndex:
      state->draft_clangd_background_index = !state->draft_clangd_background_index;
      break;
    default:
      break;
  }
}

void toggle_format_option(SettingsModalState* state, int index) {
  if (state == nullptr) {
    return;
  }
  switch (index) {
    case kFormatBasedOnStyle:
      cycle_clang_based_on_style(&state->draft_clang_format);
      break;
    case kFormatIndentWidth:
      cycle_clang_indent_width(&state->draft_clang_format);
      break;
    case kFormatUseTab:
      cycle_clang_use_tab(&state->draft_clang_format);
      break;
    case kFormatTabWidth:
      cycle_clang_tab_width(&state->draft_clang_format);
      break;
    case kFormatColumnLimit:
      cycle_clang_column_limit(&state->draft_clang_format);
      break;
    case kFormatBreakBeforeBraces:
      cycle_clang_break_before_braces(&state->draft_clang_format);
      break;
    case kFormatPointerAlignment:
      cycle_clang_pointer_alignment(&state->draft_clang_format);
      break;
    case kFormatSortIncludes:
      state->draft_clang_format.sort_includes = !state->draft_clang_format.sort_includes;
      break;
    case kFormatIncludeBlocks:
      cycle_clang_include_blocks(&state->draft_clang_format);
      break;
    case kFormatIndentCaseLabels:
      state->draft_clang_format.indent_case_labels = !state->draft_clang_format.indent_case_labels;
      break;
    case kFormatShortFunctions:
      cycle_clang_short_functions(&state->draft_clang_format);
      break;
    default:
      break;
  }
  normalize_clang_format_config(&state->draft_clang_format);
  editor_indent::apply(state->draft_clang_format);
  if (state->has_workspace && !state->workspace_root.empty()) {
    save_clang_format(state->workspace_root, state->draft_clang_format);
    state->clang_format_file_exists = true;
    if (state->clang_format_changed_callback) {
      state->clang_format_changed_callback(state->draft_clang_format);
    }
  }
}

void clamp_general_selection(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  state->selected = std::max(0, std::min(state->selected, kGlobalOptionCount - 1));
}

void clamp_workspace_selection(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  state->selected = std::max(0, std::min(state->selected, kWorkspaceOptionCount - 1));
}

void clamp_format_selection(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  state->selected = std::max(0, std::min(state->selected, kFormatOptionCount - 1));
}

void clamp_top_level_selection(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  switch (state->panel) {
    case SettingsPanel::kGeneral:
      clamp_general_selection(state);
      break;
    case SettingsPanel::kWorkspace:
      clamp_workspace_selection(state);
      break;
    case SettingsPanel::kFormat:
      clamp_format_selection(state);
      break;
    default:
      break;
  }
}

void clamp_include_path_selection(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  const int count = static_cast<int>(state->draft_clangd_extra_include_paths.size());
  if (count == 0) {
    state->include_path_selected = 0;
    return;
  }
  state->include_path_selected = std::max(0, std::min(state->include_path_selected, count - 1));
}

void clamp_mapping_selection(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  const int count = static_cast<int>(state->draft_compile_commands.path_mappings.size());
  if (count == 0) {
    state->mapping_selected = 0;
    return;
  }
  state->mapping_selected = std::max(0, std::min(state->mapping_selected, count - 1));
}

void clamp_compile_commands_selection(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  state->compile_commands_selected =
      std::max(0, std::min(state->compile_commands_selected, kCompileCommandsRowCount - 1));
}

bool path_already_listed(const SettingsModalState* state, const std::string& path) {
  if (state == nullptr) {
    return false;
  }
  return std::find(state->draft_clangd_extra_include_paths.begin(),
                   state->draft_clangd_extra_include_paths.end(),
                   path) != state->draft_clangd_extra_include_paths.end();
}

void open_include_paths_panel(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  state->panel = SettingsPanel::kIncludePaths;
  state->include_path_selected = 0;
}

void open_compile_commands_panel(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  refresh_docker_container_names(state);
  state->panel = SettingsPanel::kCompileCommands;
  state->compile_commands_selected = 0;
}

void open_path_mappings_panel(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  state->panel = SettingsPanel::kPathMappings;
  state->mapping_selected = 0;
}

void open_path_browser_panel(SettingsModalState* state, PathBrowserPurpose purpose) {
  if (state == nullptr) {
    return;
  }
  const std::string start =
      state->workspace_root.empty() ? state->path_browser.launch_root : state->workspace_root;
  state->path_browser.reset(start);
  state->path_browser_purpose = purpose;
  state->panel = SettingsPanel::kPathBrowser;
}

void apply_draft_ui_colors(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  theme::set_mode(state->draft_theme);
  theme::set_ui_overrides(state->draft_ui_colors);
}

void ensure_draft_ui_colors_complete(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  apply_draft_ui_colors(state);
  const theme::UiColorOverrides snapshot = theme::snapshot_effective_ui_colors();
  auto fill = [&](std::optional<theme::ColorRgb>& slot, const std::optional<theme::ColorRgb>& value) {
    if (!slot && value) {
      slot = *value;
    }
  };
  fill(state->draft_ui_colors.panel_bg, snapshot.panel_bg);
  fill(state->draft_ui_colors.code_bg, snapshot.code_bg);
  fill(state->draft_ui_colors.text, snapshot.text);
  fill(state->draft_ui_colors.title, snapshot.title);
  fill(state->draft_ui_colors.directory, snapshot.directory);
  fill(state->draft_ui_colors.file, snapshot.file);
}

void apply_ui_color_preset(SettingsModalState* state, theme::UiColorPreset preset) {
  if (state == nullptr) {
    return;
  }
  state->draft_ui_colors_preset = preset;
  state->draft_ui_colors = theme::overrides_for_preset(preset);
  state->draft_theme = theme::theme_mode_for_preset(preset);
  apply_draft_ui_colors(state);
}

void cycle_ui_color_preset(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  theme::UiColorPreset next = theme::UiColorPreset::kDarkClassic;
  switch (state->draft_ui_colors_preset) {
    case theme::UiColorPreset::kDarkClassic:
      next = theme::UiColorPreset::kDarkSoft;
      break;
    case theme::UiColorPreset::kDarkSoft:
      next = theme::UiColorPreset::kLightClassic;
      break;
    case theme::UiColorPreset::kLightClassic:
      next = theme::UiColorPreset::kLightPaper;
      break;
    case theme::UiColorPreset::kLightPaper:
    case theme::UiColorPreset::kCustom:
    default:
      next = theme::UiColorPreset::kDarkClassic;
      break;
  }
  apply_ui_color_preset(state, next);
}

void open_ui_colors_panel(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  ensure_draft_ui_colors_complete(state);
  state->panel = SettingsPanel::kUiColors;
  state->ui_colors_selected = 0;
  state->ui_colors_editing = false;
  state->ui_colors_edit_row = -1;
  state->ui_colors_palette_selected = 0;
  state->ui_colors_edit_original.reset();
}

std::optional<theme::ColorRgb>* mutable_ui_color_field(SettingsModalState* state, int row) {
  if (state == nullptr) {
    return nullptr;
  }
  switch (row) {
    case kUiColorsPanelBg:
      return &state->draft_ui_colors.panel_bg;
    case kUiColorsCodeBg:
      return &state->draft_ui_colors.code_bg;
    case kUiColorsText:
      return &state->draft_ui_colors.text;
    case kUiColorsTitle:
      return &state->draft_ui_colors.title;
    case kUiColorsDirectory:
      return &state->draft_ui_colors.directory;
    case kUiColorsFile:
      return &state->draft_ui_colors.file;
    default:
      return nullptr;
  }
}

int find_palette_index(const theme::ColorRgb& color) {
  int best_index = 0;
  int best_distance = 1 << 30;
  for (int i = 0; i < kPaletteCount; ++i) {
    const theme::ColorRgb& candidate = kUIColorPalette[static_cast<std::size_t>(i)];
    const int dr = static_cast<int>(candidate.r) - static_cast<int>(color.r);
    const int dg = static_cast<int>(candidate.g) - static_cast<int>(color.g);
    const int db = static_cast<int>(candidate.b) - static_cast<int>(color.b);
    const int distance = dr * dr + dg * dg + db * db;
    if (distance < best_distance) {
      best_distance = distance;
      best_index = i;
    }
  }
  return best_index;
}

void apply_palette_selection(SettingsModalState* state) {
  if (state == nullptr || !state->ui_colors_editing) {
    return;
  }
  const theme::ColorRgb& chosen =
      kUIColorPalette[static_cast<std::size_t>(state->ui_colors_palette_selected)];
  if (auto* field = mutable_ui_color_field(state, state->ui_colors_edit_row)) {
    *field = chosen;
    state->draft_ui_colors_preset = theme::UiColorPreset::kCustom;
    apply_draft_ui_colors(state);
  }
}

std::string ui_color_row_label(int row) {
  switch (row) {
    case kUiColorsPreset:
      return i18n::tr("settings.ui_colors.row.preset");
    case kUiColorsPanelBg:
      return i18n::tr("settings.ui_colors.row.panel_bg");
    case kUiColorsCodeBg:
      return i18n::tr("settings.ui_colors.row.code_bg");
    case kUiColorsText:
      return i18n::tr("settings.ui_colors.row.text");
    case kUiColorsTitle:
      return i18n::tr("settings.ui_colors.row.title");
    case kUiColorsDirectory:
      return i18n::tr("settings.ui_colors.row.directory");
    case kUiColorsFile:
      return i18n::tr("settings.ui_colors.row.file");
    default:
      return {};
  }
}

std::string ui_color_row_value(const SettingsModalState* state, int row) {
  if (state == nullptr) {
    return {};
  }
  if (row == kUiColorsPreset) {
    return theme::ui_color_preset_label(state->draft_ui_colors_preset);
  }
  const auto* field = mutable_ui_color_field(const_cast<SettingsModalState*>(state), row);
  if (field != nullptr && field->has_value()) {
    return theme::format_hex_color(**field);
  }
  return i18n::tr("settings.ui_colors.dash");
}

void begin_ui_color_edit(SettingsModalState* state, int row) {
  if (state == nullptr) {
    return;
  }
  ensure_draft_ui_colors_complete(state);
  state->ui_colors_editing = true;
  state->ui_colors_edit_row = row;
  if (auto* field = mutable_ui_color_field(state, row)) {
    if (field->has_value()) {
      state->ui_colors_edit_original = **field;
      state->ui_colors_palette_selected = find_palette_index(**field);
    } else {
      state->ui_colors_edit_original.reset();
      state->ui_colors_palette_selected = 0;
    }
  }
}

bool commit_ui_color_edit(SettingsModalState* state) {
  if (state == nullptr || !state->ui_colors_editing) {
    return false;
  }
  apply_palette_selection(state);
  state->ui_colors_editing = false;
  state->ui_colors_edit_row = -1;
  state->ui_colors_edit_original.reset();
  return true;
}

void cancel_ui_color_edit(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  if (state->ui_colors_editing && state->ui_colors_edit_original.has_value()) {
    if (auto* field = mutable_ui_color_field(state, state->ui_colors_edit_row)) {
      *field = *state->ui_colors_edit_original;
      apply_draft_ui_colors(state);
    }
  }
  state->ui_colors_editing = false;
  state->ui_colors_edit_row = -1;
  state->ui_colors_edit_original.reset();
}

void move_palette_selection(SettingsModalState* state, int delta_row, int delta_col) {
  if (state == nullptr || !state->ui_colors_editing) {
    return;
  }
  const int row = state->ui_colors_palette_selected / kPaletteCols;
  const int col = state->ui_colors_palette_selected % kPaletteCols;
  const int next_row = std::max(0, std::min(kPaletteRows - 1, row + delta_row));
  const int next_col = std::max(0, std::min(kPaletteCols - 1, col + delta_col));
  int next_index = next_row * kPaletteCols + next_col;
  next_index = std::max(0, std::min(kPaletteCount - 1, next_index));
  if (next_index == state->ui_colors_palette_selected) {
    return;
  }
  state->ui_colors_palette_selected = next_index;
  apply_palette_selection(state);
}

void clamp_ui_colors_selection(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  state->ui_colors_selected =
      std::max(0, std::min(state->ui_colors_selected, kUiColorsRowCount - 1));
}

bool handle_ui_colors_keys(SettingsModalState* state, Event event) {
  if (state == nullptr) {
    return false;
  }

  if (state->ui_colors_editing) {
    if (event == Event::Escape) {
      cancel_ui_color_edit(state);
      return true;
    }
    if (event == Event::Return) {
      commit_ui_color_edit(state);
      return true;
    }
    if (event == Event::ArrowLeft || event == Event::Character('h')) {
      move_palette_selection(state, 0, -1);
      return true;
    }
    if (event == Event::ArrowRight || event == Event::Character('l')) {
      move_palette_selection(state, 0, 1);
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      move_palette_selection(state, -1, 0);
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      move_palette_selection(state, 1, 0);
      return true;
    }
    return true;
  }

  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->ui_colors_selected += 1;
    clamp_ui_colors_selection(state);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->ui_colors_selected -= 1;
    clamp_ui_colors_selection(state);
    return true;
  }
  if (event == Event::Character('p') || event == Event::Character('P')) {
    cycle_ui_color_preset(state);
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    if (state->ui_colors_selected == kUiColorsPreset) {
      cycle_ui_color_preset(state);
      return true;
    }
    begin_ui_color_edit(state, state->ui_colors_selected);
    return true;
  }
  return true;
}

SettingsBodyContent build_ui_colors_panel(const SettingsModalState* state) {
  SettingsBodyContent content;
  content.rows.push_back(text(i18n::tr("settings.title.ui_colors")) | color(theme::Accent()) | bold);
  content.rows.push_back(text(i18n::tr("settings.ui_colors.syntax_note")) | color(theme::Muted()));
  content.rows.push_back(separator());

  if (state->ui_colors_editing) {
    content.rows.push_back(text(i18n::tr_fmt("settings.ui_colors.choose",
                                      {ui_color_row_label(state->ui_colors_edit_row)})) |
                   color(theme::Accent()) | bold);
    const theme::ColorRgb& current =
        kUIColorPalette[static_cast<std::size_t>(state->ui_colors_palette_selected)];
    content.rows.push_back(text(i18n::tr_fmt("settings.ui_colors.selected",
                                      {theme::format_hex_color(current)})) |
                   color(theme::Header()) | bold);

    for (int row = 0; row < kPaletteRows; ++row) {
      const int row_start = row * kPaletteCols;
      const int row_end = std::min(row_start + kPaletteCols, kPaletteCount);
      if (state->ui_colors_palette_selected >= row_start &&
          state->ui_colors_palette_selected < row_end) {
        content.focus_row = static_cast<int>(content.rows.size());
      }
      Elements cells;
      for (int col = 0; col < kPaletteCols; ++col) {
        const int index = row * kPaletteCols + col;
        if (index >= kPaletteCount) {
          break;
        }
        const theme::ColorRgb& swatch =
            kUIColorPalette[static_cast<std::size_t>(index)];
        const bool selected = index == state->ui_colors_palette_selected;
        const Color ftxui_color = Color::RGB(swatch.r, swatch.g, swatch.b);
        Element cell = text(" ██ ") | color(ftxui_color) | bgcolor(ftxui_color);
        if (selected) {
          cell = cell | inverted | bold;
        }
        cells.push_back(cell);
      }
      content.rows.push_back(hbox(std::move(cells)));
    }

    content.rows.push_back(separator());
    content.rows.push_back(text(i18n::tr("settings.ui_colors.edit_footer")) | color(theme::Muted()));
    return content;
  }

  for (int i = 0; i < kUiColorsRowCount; ++i) {
    const bool selected = i == state->ui_colors_selected;
    const std::string label = ui_color_row_label(i);
    const std::string value = ui_color_row_value(state, i);
    Element row_el =
        text(i18n::tr_fmt("settings.ui_colors.row.value", {label, value})) |
        color(selected ? theme::Accent() : theme::Header()) | bold;
    if (selected) {
      content.focus_row = static_cast<int>(content.rows.size());
      row_el = row_el | inverted;
    }
    content.rows.push_back(row_el);
  }

  content.rows.push_back(separator());
  content.rows.push_back(text(i18n::tr("settings.ui_colors.list_footer")) | color(theme::Muted()));
  return content;
}

void confirm_path_browser_selection(SettingsModalState* state) {
  if (state == nullptr || !is_directory_path(state->path_browser.browser_path)) {
    return;
  }
  const std::string chosen = state->path_browser.browser_path;
  if (state->path_browser_purpose == PathBrowserPurpose::kMappingHostPath) {
    std::vector<PathMapping> mount_hints = state->draft_compile_commands.path_mappings;
    if (state->draft_compile_commands.docker_detect_mounts &&
        !state->draft_compile_commands.docker_container.empty()) {
      const auto detected =
          detect_docker_mount_mappings(state->draft_compile_commands.docker_container);
      mount_hints.insert(mount_hints.end(), detected.begin(), detected.end());
    }
    PathMapping mapping;
    mapping.to = chosen;
    mapping.from = container_path_for_host_path(chosen, mount_hints);
    const auto duplicate = std::find_if(
        state->draft_compile_commands.path_mappings.begin(),
        state->draft_compile_commands.path_mappings.end(),
        [&](const PathMapping& existing) {
          return existing.from == mapping.from && existing.to == mapping.to;
        });
    if (duplicate == state->draft_compile_commands.path_mappings.end()) {
      state->draft_compile_commands.path_mappings.push_back(std::move(mapping));
    }
    state->panel = SettingsPanel::kPathMappings;
    clamp_mapping_selection(state);
    return;
  }

  if (!path_already_listed(state, chosen)) {
    state->draft_clangd_extra_include_paths.push_back(chosen);
  }
  state->panel = SettingsPanel::kIncludePaths;
  clamp_include_path_selection(state);
}

void delete_selected_include_path(SettingsModalState* state) {
  if (state == nullptr || state->draft_clangd_extra_include_paths.empty()) {
    return;
  }
  clamp_include_path_selection(state);
  const auto index = static_cast<std::size_t>(state->include_path_selected);
  if (index >= state->draft_clangd_extra_include_paths.size()) {
    return;
  }
  state->draft_clangd_extra_include_paths.erase(
      state->draft_clangd_extra_include_paths.begin() +
      static_cast<std::ptrdiff_t>(index));
  clamp_include_path_selection(state);
}

bool handle_top_level_tab_keys(SettingsModalState* state, Event event) {
  if (state == nullptr || !is_top_level_panel(state->panel)) {
    return false;
  }
  if (top_level_panel_count(state) <= 1) {
    return false;
  }
  if (event == Event::Tab) {
    cycle_top_level_panel(state, 1);
    return true;
  }
  if (event == Event::TabReverse) {
    cycle_top_level_panel(state, -1);
    return true;
  }
  if (event == Event::ArrowRight || event == Event::Character('l')) {
    cycle_top_level_panel(state, 1);
    return true;
  }
  if (event == Event::ArrowLeft || event == Event::Character('h')) {
    cycle_top_level_panel(state, -1);
    return true;
  }
  return false;
}

bool handle_general_settings_keys(SettingsModalState* state, Event event) {
  if (state == nullptr) {
    return false;
  }
  if (handle_top_level_tab_keys(state, event)) {
    return true;
  }

  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->selected += 1;
    clamp_general_selection(state);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->selected -= 1;
    clamp_general_selection(state);
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    toggle_option(state, state->selected);
    return true;
  }
  return true;
}

bool handle_workspace_settings_keys(SettingsModalState* state, Event event) {
  if (state == nullptr) {
    return false;
  }
  if (handle_top_level_tab_keys(state, event)) {
    return true;
  }

  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->selected += 1;
    clamp_workspace_selection(state);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->selected -= 1;
    clamp_workspace_selection(state);
    return true;
  }
  if (event == Event::Return) {
    if (state->selected == kWorkspaceIncludePaths) {
      open_include_paths_panel(state);
      return true;
    }
    if (state->selected == kWorkspaceCompileCommands) {
      open_compile_commands_panel(state);
      return true;
    }
    if (state->selected == kWorkspaceUiColors) {
      open_ui_colors_panel(state);
      return true;
    }
    toggle_workspace_option(state, state->selected);
    return true;
  }
  if (event == Event::Character(' ')) {
    if (state->selected == kWorkspaceIncludePaths) {
      open_include_paths_panel(state);
      return true;
    }
    if (state->selected == kWorkspaceCompileCommands) {
      open_compile_commands_panel(state);
      return true;
    }
    if (state->selected == kWorkspaceUiColors) {
      open_ui_colors_panel(state);
      return true;
    }
    toggle_workspace_option(state, state->selected);
    return true;
  }
  return true;
}

bool handle_format_settings_keys(SettingsModalState* state, Event event) {
  if (state == nullptr) {
    return false;
  }
  if (handle_top_level_tab_keys(state, event)) {
    return true;
  }

  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->selected += 1;
    clamp_format_selection(state);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->selected -= 1;
    clamp_format_selection(state);
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    toggle_format_option(state, state->selected);
    return true;
  }
  return true;
}

void delete_selected_path_mapping(SettingsModalState* state) {
  if (state == nullptr || state->draft_compile_commands.path_mappings.empty()) {
    return;
  }
  clamp_mapping_selection(state);
  const auto index = static_cast<std::size_t>(state->mapping_selected);
  if (index >= state->draft_compile_commands.path_mappings.size()) {
    return;
  }
  state->draft_compile_commands.path_mappings.erase(
      state->draft_compile_commands.path_mappings.begin() +
      static_cast<std::ptrdiff_t>(index));
  clamp_mapping_selection(state);
}

void detect_docker_mounts_into_settings(SettingsModalState* state) {
  if (state == nullptr || state->draft_compile_commands.docker_container.empty()) {
    return;
  }
  const auto detected = detect_docker_mount_mappings(
      state->draft_compile_commands.docker_container, true);
  for (const auto& mapping : detected) {
    const auto duplicate = std::find_if(
        state->draft_compile_commands.path_mappings.begin(),
        state->draft_compile_commands.path_mappings.end(),
        [&](const PathMapping& existing) { return existing.from == mapping.from; });
    if (duplicate == state->draft_compile_commands.path_mappings.end()) {
      state->draft_compile_commands.path_mappings.push_back(mapping);
    }
  }
  clamp_mapping_selection(state);
}

bool handle_compile_commands_keys(SettingsModalState* state, Event event) {
  if (state == nullptr) {
    return false;
  }

  if (event == Event::Escape) {
    state->panel = SettingsPanel::kWorkspace;
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->compile_commands_selected += 1;
    clamp_compile_commands_selection(state);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->compile_commands_selected -= 1;
    clamp_compile_commands_selection(state);
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    switch (state->compile_commands_selected) {
      case kCompileMode:
        cycle_compile_commands_mode(state);
        return true;
      case kCompileDetectMounts:
        state->draft_compile_commands.docker_detect_mounts =
            !state->draft_compile_commands.docker_detect_mounts;
        return true;
      case kCompileDockerContainer:
        cycle_docker_container(state);
        return true;
      case kCompilePathMappings:
        open_path_mappings_panel(state);
        return true;
      default:
        return true;
    }
  }
  if (event == Event::Character('m') || event == Event::Character('M')) {
    cycle_compile_commands_mode(state);
    return true;
  }
  if (event == Event::Character('n') || event == Event::Character('N')) {
    cycle_docker_container(state);
    return true;
  }
  return true;
}

bool handle_path_mappings_keys(SettingsModalState* state, Event event) {
  if (state == nullptr) {
    return false;
  }

  if (event == Event::Escape) {
    state->panel = SettingsPanel::kCompileCommands;
    return true;
  }
  if (event == Event::Character('a') || event == Event::Character('A')) {
    open_path_browser_panel(state, PathBrowserPurpose::kMappingHostPath);
    return true;
  }
  if (event == Event::Character('d') || event == Event::Character('D') ||
      event == Event::Delete) {
    delete_selected_path_mapping(state);
    return true;
  }
  if (event == Event::Character('r') || event == Event::Character('R')) {
    detect_docker_mounts_into_settings(state);
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->mapping_selected += 1;
    clamp_mapping_selection(state);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->mapping_selected -= 1;
    clamp_mapping_selection(state);
    return true;
  }
  return true;
}

bool handle_include_paths_keys(SettingsModalState* state, Event event) {
  if (state == nullptr) {
    return false;
  }

  if (event == Event::Escape) {
    state->panel = SettingsPanel::kWorkspace;
    return true;
  }
  if (event == Event::Character('a') || event == Event::Character('A')) {
    open_path_browser_panel(state, PathBrowserPurpose::kIncludePath);
    return true;
  }
  if (event == Event::Character('d') || event == Event::Character('D') ||
      event == Event::Delete) {
    delete_selected_include_path(state);
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->include_path_selected += 1;
    clamp_include_path_selection(state);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->include_path_selected -= 1;
    clamp_include_path_selection(state);
    return true;
  }
  return true;
}

void activate_path_browser_row(SettingsModalState* state, int row) {
  if (state == nullptr) {
    return;
  }
  if (row < 0 || row >= static_cast<int>(state->path_browser.entries.size())) {
    return;
  }
  state->path_browser.selected = row;
  const auto& entry = state->path_browser.entries[static_cast<std::size_t>(row)];
  if (entry.is_directory) {
    state->path_browser.browser_path = entry.path;
    state->path_browser.reload_browser_entries(true);
  }
}

bool handle_path_browser_keys(SettingsModalState* state, Event event) {
  if (state == nullptr) {
    return false;
  }

  state->path_browser.ensure_browser_entries();

  if (event == Event::Escape) {
    state->panel = state->path_browser_purpose == PathBrowserPurpose::kMappingHostPath
                       ? SettingsPanel::kPathMappings
                       : SettingsPanel::kIncludePaths;
    return true;
  }
  if (event == Event::Character('a') || event == Event::Character('A')) {
    confirm_path_browser_selection(state);
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->path_browser.selected = std::min(
        state->path_browser.selected + 1,
        std::max(0, static_cast<int>(state->path_browser.entries.size()) - 1));
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->path_browser.selected = std::max(0, state->path_browser.selected - 1);
    return true;
  }
  if (event == Event::Return) {
    activate_path_browser_row(state, state->path_browser.selected);
    return true;
  }
  return true;
}

bool handle_settings_keys(SettingsModalState* state, Event event) {
  if (state == nullptr || !state->open) {
    return false;
  }

  if (handle_settings_body_scroll_keys(state, event)) {
    return true;
  }

  if (event == Event::Escape || event == Event::F10) {
    return true;
  }

  switch (state->panel) {
    case SettingsPanel::kGeneral:
      return handle_general_settings_keys(state, event);
    case SettingsPanel::kWorkspace:
      return handle_workspace_settings_keys(state, event);
    case SettingsPanel::kFormat:
      return handle_format_settings_keys(state, event);
    case SettingsPanel::kIncludePaths:
      return handle_include_paths_keys(state, event);
    case SettingsPanel::kCompileCommands:
      return handle_compile_commands_keys(state, event);
    case SettingsPanel::kPathMappings:
      return handle_path_mappings_keys(state, event);
    case SettingsPanel::kPathBrowser:
      return handle_path_browser_keys(state, event);
    case SettingsPanel::kUiColors:
      return handle_ui_colors_keys(state, event);
  }
  return true;
}

std::string format_column_limit_label(int column_limit) {
  if (column_limit <= 0) {
    return i18n::tr("settings.format.no_limit");
  }
  return std::to_string(column_limit);
}

Element render_top_level_tabs(const SettingsModalState* state) {
  if (state == nullptr || !state->has_workspace) {
    return text("");
  }

  const auto render_tab = [&](SettingsPanel panel, const char* label) {
    const bool active = state->panel == panel;
    Element tab = text(std::string(active ? "▸ " : "  ") + label) |
                    color(active ? theme::Accent() : theme::Muted()) | bold;
    if (active) {
      tab = tab | underlined;
    }
    return tab;
  };

  return hbox({render_tab(SettingsPanel::kGeneral, i18n::tr("settings.tab.general").c_str()),
               text("  "),
               render_tab(SettingsPanel::kWorkspace, i18n::tr("settings.tab.workspace").c_str()),
               text("  "),
               render_tab(SettingsPanel::kFormat, i18n::tr("settings.tab.format").c_str())});
}

void append_top_level_tabs_header(SettingsBodyContent* content, const SettingsModalState* state) {
  if (content == nullptr || state == nullptr || !state->has_workspace) {
    return;
  }
  content->header.push_back(render_top_level_tabs(state));
  content->header.push_back(separator() | color(theme::AccentDim()));
  content->header.push_back(text(""));
}

SettingsBodyContent build_general_settings(const SettingsModalState* state) {
  SettingsBodyContent content;
  append_top_level_tabs_header(&content, state);

  const auto options = global_settings_options();
  for (int i = 0; i < kGlobalOptionCount; ++i) {
    const auto& option = options[static_cast<std::size_t>(i)];
    const bool selected = i == state->selected;
    const bool checked = option_checked(state, i);

    Element title;
    if (i == kUiLocale) {
      title = text(ui_locale_option_label(state->draft_ui_locale, option.label)) |
              color(selected ? theme::Accent() : theme::Header()) | bold;
    } else if (i == kIcons) {
      title = text(icon_mode_option_label(state->draft_icon_mode, option.label)) |
              color(selected ? theme::Accent() : theme::Header()) | bold;
    } else {
      title = text(checkbox_label(checked, option.label)) |
              color(selected ? theme::Accent() : theme::Header()) | bold;
    }
    if (selected) {
      content.focus_row = static_cast<int>(content.rows.size());
      title = title | inverted;
    }
    content.rows.push_back(title);
    content.rows.push_back(text("    " + option.description) | color(theme::Muted()));
    content.rows.push_back(text(""));
  }

  if (!content.rows.empty()) {
    content.rows.pop_back();
  }
  return content;
}

SettingsBodyContent build_workspace_settings(const SettingsModalState* state) {
  SettingsBodyContent content;
  append_top_level_tabs_header(&content, state);

  {
    const bool selected = state->selected == kWorkspaceGccQueryDriver;
    const bool checked = state->draft_clangd_use_gcc_query_driver;
    Element title =
        text(checkbox_label(checked, i18n::tr("settings.workspace.gcc_query_driver.label"))) |
        color(selected ? theme::Accent() : theme::Header()) | bold;
    if (selected) {
      content.focus_row = static_cast<int>(content.rows.size());
      title = title | inverted;
    }
    content.rows.push_back(title);
    content.rows.push_back(
        text("    " + i18n::tr("settings.workspace.gcc_query_driver.description")) |
        color(theme::Muted()));
    content.rows.push_back(text(""));
  }

  {
    const bool selected = state->selected == kWorkspaceBackgroundIndex;
    const bool checked = state->draft_clangd_background_index;
    Element title = text(checkbox_label(
        checked, i18n::tr("settings.workspace.background_index.label"))) |
                    color(selected ? theme::Accent() : theme::Header()) | bold;
    if (selected) {
      content.focus_row = static_cast<int>(content.rows.size());
      title = title | inverted;
    }
    content.rows.push_back(title);
    content.rows.push_back(
        text("    " + i18n::tr("settings.workspace.background_index.description")) |
        color(theme::Muted()));
    content.rows.push_back(text(""));
  }

  {
    const bool selected = state->selected == kWorkspaceIncludePaths;
    const std::size_t count = state->draft_clangd_extra_include_paths.size();
    const std::string label =
        i18n::tr_fmt("settings.workspace.include_paths.label", {std::to_string(count)});
    Element title = text(label) | color(selected ? theme::Accent() : theme::Header()) | bold;
    if (selected) {
      content.focus_row = static_cast<int>(content.rows.size());
      title = title | inverted;
    }
    content.rows.push_back(title);
    content.rows.push_back(text("    " + i18n::tr("settings.workspace.include_paths.description")) |
                   color(theme::Muted()));
    content.rows.push_back(text(""));
  }

  {
    const bool selected = state->selected == kWorkspaceCompileCommands;
    const std::string label = i18n::tr_fmt(
        "settings.workspace.compile_commands.label",
        {compile_commands_mode_label(state->draft_compile_commands.mode)});
    Element title = text(label) | color(selected ? theme::Accent() : theme::Header()) | bold;
    if (selected) {
      content.focus_row = static_cast<int>(content.rows.size());
      title = title | inverted;
    }
    content.rows.push_back(title);
    content.rows.push_back(text("    " + i18n::tr("settings.workspace.compile_commands.description")) |
                   color(theme::Muted()));
    content.rows.push_back(text(""));
  }

  {
    const bool selected = state->selected == kWorkspaceUiColors;
    const std::string label = i18n::tr_fmt(
        "settings.workspace.ui_colors.label",
        {theme::ui_color_preset_label(state->draft_ui_colors_preset)});
    Element title = text(label) | color(selected ? theme::Accent() : theme::Header()) | bold;
    if (selected) {
      content.focus_row = static_cast<int>(content.rows.size());
      title = title | inverted;
    }
    content.rows.push_back(title);
    content.rows.push_back(text("    " + i18n::tr("settings.workspace.ui_colors.description")) |
                   color(theme::Muted()));
    content.rows.push_back(text(""));
  }

  if (!content.rows.empty()) {
    content.rows.pop_back();
  }
  return content;
}

SettingsBodyContent build_format_settings(const SettingsModalState* state) {
  SettingsBodyContent content;
  append_top_level_tabs_header(&content, state);

  const std::string file_note =
      state->clang_format_file_exists ? i18n::tr("settings.format.file_exists")
                                      : i18n::tr("settings.format.file_will_create");
  content.rows.push_back(text(file_note) | color(theme::Muted()));
  content.rows.push_back(text(""));

  const auto& config = state->draft_clang_format;
  const auto row = [&](int index, const std::string& label, const std::string& value) {
    const bool selected = index == state->selected;
    Element line = text(label + value) | color(selected ? theme::Accent() : theme::Header()) | bold;
    if (selected) {
      content.focus_row = static_cast<int>(content.rows.size());
      line = line | inverted;
    }
    content.rows.push_back(line);
  };

  row(kFormatBasedOnStyle, i18n::tr("settings.format.base_style"),
      clang_based_on_style_name(config.based_on_style));
  row(kFormatIndentWidth, i18n::tr("settings.format.indent_width"),
      std::to_string(config.indent_width));
  row(kFormatUseTab, i18n::tr("settings.format.use_tab"), clang_use_tab_name(config.use_tab));
  row(kFormatTabWidth, i18n::tr("settings.format.tab_width"), std::to_string(config.tab_width));
  row(kFormatColumnLimit, i18n::tr("settings.format.column_limit"),
      format_column_limit_label(config.column_limit));
  row(kFormatBreakBeforeBraces, i18n::tr("settings.format.break_before_braces"),
      clang_break_before_braces_name(config.break_before_braces));
  row(kFormatPointerAlignment, i18n::tr("settings.format.pointer_alignment"),
      clang_pointer_alignment_name(config.pointer_alignment));
  row(kFormatSortIncludes, checkbox_label(config.sort_includes, i18n::tr("settings.format.sort_includes")),
      "");
  row(kFormatIncludeBlocks, i18n::tr("settings.format.include_blocks"),
      clang_include_blocks_name(config.include_blocks));
  row(kFormatIndentCaseLabels,
      checkbox_label(config.indent_case_labels, i18n::tr("settings.format.indent_case_labels")),
      "");
  row(kFormatShortFunctions, i18n::tr("settings.format.short_functions"),
      clang_short_functions_name(config.allow_short_functions_on_a_single_line));

  return content;
}

SettingsBodyContent build_compile_commands_panel(const SettingsModalState* state) {
  SettingsBodyContent content;
  content.rows.push_back(text(i18n::tr("settings.compile_commands.title")) | color(theme::Accent()) | bold);
  content.rows.push_back(text(i18n::tr("settings.compile_commands.output")) | color(theme::Muted()));
  content.rows.push_back(separator());

  auto row = [&](int index, const std::string& label, const std::string& value) {
    const bool selected = index == state->compile_commands_selected;
    Element line = text(label + value) | color(selected ? theme::Accent() : theme::Header()) | bold;
    if (selected) {
      content.focus_row = static_cast<int>(content.rows.size());
      line = line | inverted;
    }
    content.rows.push_back(line);
  };

  row(kCompileMode, i18n::tr("settings.compile_commands.mode"),
      compile_commands_mode_label(state->draft_compile_commands.mode));
  row(kCompileDetectMounts,
      checkbox_label(state->draft_compile_commands.docker_detect_mounts,
                     i18n::tr("settings.compile_commands.detect_mounts")),
      "");
  const std::string container = state->draft_compile_commands.docker_container.empty()
                                  ? i18n::tr("common.none")
                                  : state->draft_compile_commands.docker_container;
  row(kCompileDockerContainer, i18n::tr("settings.compile_commands.docker_container"), container);
  row(kCompilePathMappings, i18n::tr("settings.compile_commands.path_mappings"),
      std::to_string(state->draft_compile_commands.path_mappings.size()) + ")");

  content.rows.push_back(separator());
  content.rows.push_back(text(i18n::tr_fmt("settings.compile_commands.host_source",
                                    {state->draft_compile_commands.source_path})) |
                 color(theme::Muted()));
  if (!state->draft_compile_commands.docker_compile_commands_path.empty()) {
    content.rows.push_back(text(i18n::tr_fmt(
                     "settings.compile_commands.docker_path",
                     {state->draft_compile_commands.docker_compile_commands_path})) |
                   color(theme::Muted()));
  }
  content.rows.push_back(separator());
  content.rows.push_back(text(i18n::tr("settings.compile_commands.footer")) | color(theme::Muted()));
  return content;
}

SettingsBodyContent build_path_mappings_panel(const SettingsModalState* state) {
  SettingsBodyContent content;
  content.rows.push_back(text(i18n::tr("settings.path_mappings.title")) | color(theme::Accent()) | bold);
  content.rows.push_back(separator());
  if (state->draft_compile_commands.path_mappings.empty()) {
    content.rows.push_back(text(i18n::tr("common.no_mappings")) | color(theme::Muted()));
  } else {
    for (int i = 0; i < static_cast<int>(state->draft_compile_commands.path_mappings.size()); ++i) {
      const bool selected = i == state->mapping_selected;
      const auto& mapping = state->draft_compile_commands.path_mappings[static_cast<std::size_t>(i)];
      const std::string line = mapping.from + "  →  " + mapping.to;
      Element row = text(line) | color(selected ? theme::Accent() : theme::Header());
      if (selected) {
        content.focus_row = static_cast<int>(content.rows.size());
        row = row | inverted;
      }
      content.rows.push_back(row);
    }
  }
  content.rows.push_back(separator());
  content.rows.push_back(text(i18n::tr("settings.path_mappings.footer")) | color(theme::Muted()));
  return content;
}

SettingsBodyContent build_include_paths_panel(const SettingsModalState* state) {
  SettingsBodyContent content;
  content.rows.push_back(text(i18n::tr("settings.include_paths.title")) | color(theme::Accent()) | bold);
  content.rows.push_back(separator());
  if (state->draft_clangd_extra_include_paths.empty()) {
    content.rows.push_back(text(i18n::tr("common.no_extra_paths")) | color(theme::Muted()));
  } else {
    for (int i = 0; i < static_cast<int>(state->draft_clangd_extra_include_paths.size()); ++i) {
      const bool selected = i == state->include_path_selected;
      const std::string& path = state->draft_clangd_extra_include_paths[static_cast<std::size_t>(i)];
      Element line = text(path) | color(selected ? theme::Accent() : theme::Header());
      if (selected) {
        content.focus_row = static_cast<int>(content.rows.size());
        line = line | inverted;
      }
      content.rows.push_back(line);
    }
  }
  content.rows.push_back(separator());
  content.rows.push_back(text(i18n::tr("settings.include_paths.footer")) | color(theme::Muted()));
  return content;
}

SettingsBodyContent build_path_browser_panel(SettingsModalState* state) {
  SettingsBodyContent content;
  state->path_browser.ensure_browser_entries();

  content.rows.push_back(text(state->path_browser_purpose == PathBrowserPurpose::kMappingHostPath
                          ? i18n::tr("settings.path_browser.add_mapping")
                          : i18n::tr("settings.path_browser.add_include")) |
                 color(theme::Accent()) | bold);
  content.rows.push_back(separator());
  content.rows.push_back(text(state->path_browser.browser_path) | color(theme::Muted()));
  content.rows.push_back(separator());

  if (state->path_browser.entries.empty()) {
    content.rows.push_back(text(i18n::tr("common.empty")) | color(theme::Muted()));
  } else {
    for (int i = 0; i < static_cast<int>(state->path_browser.entries.size()); ++i) {
      const auto& row = state->path_browser.entries[static_cast<std::size_t>(i)];
      const std::string prefix = row.is_directory ? i18n::tr("common.browser.dir_prefix")
                                                  : i18n::tr("common.browser.file_indent");
      const bool selected = i == state->path_browser.selected;
      Element line = text(prefix + row.name);
      if (row.is_directory) {
        line = line | color(theme::Accent());
      }
      if (selected) {
        content.focus_row = static_cast<int>(content.rows.size());
        line = line | inverted;
      }
      content.rows.push_back(line);
    }
  }
  content.rows.push_back(separator());
  content.rows.push_back(text(i18n::tr("settings.path_browser.footer")) | color(theme::Muted()));
  return content;
}

}  // namespace

void open_settings_modal(SettingsModalState* state, const AppSettings& settings,
                         const std::string& workspace_root,
                         const WorkspaceConfig& workspace_config) {
  if (state == nullptr) {
    return;
  }
  state->open = true;
  state->panel = SettingsPanel::kGeneral;
  state->body_scroll = 0;
  state->body_scroll_panel = SettingsPanel::kGeneral;
  state->selected = 0;
  state->include_path_selected = 0;
  state->compile_commands_selected = 0;
  state->mapping_selected = 0;
  state->draft_lsp_enabled = settings.lsp_enabled;
  state->draft_live_lsp_completion_enabled = settings.live_lsp_completion_enabled;
  state->draft_show_diagnostic_suffixes = settings.show_diagnostic_suffixes;
  state->draft_sticky_scroll_enabled = settings.sticky_scroll_enabled;
  state->draft_indent_guides_enabled = settings.indent_guides_enabled;
  state->draft_overview_ruler_enabled = settings.overview_ruler_enabled;
  state->draft_secondary_panel_enabled = settings.secondary_panel_enabled;
  state->draft_helix_mode_enabled = settings.helix_mode_enabled;
  state->draft_workspace_auto_detect_enabled = settings.workspace_auto_detect_enabled;
  state->draft_show_all_workspace_files = settings.show_all_workspace_files;
  state->draft_force_bundled_clangd = settings.force_bundled_clangd;
  state->draft_force_bundled_gdb = settings.force_bundled_gdb;
  state->draft_monitor_enabled = settings.monitor_enabled;
  state->draft_icon_mode = settings.icon_mode;
  state->draft_ui_locale = settings.ui_locale;
  i18n::set_locale(state->draft_ui_locale);
  state->draft_theme = workspace_config.theme;
  state->workspace_root = workspace_root;
  state->has_workspace = !workspace_root.empty();
  if (state->has_workspace) {
    state->draft_ui_colors_preset = workspace_config.ui_colors_preset;
    if (!workspace_config.ui_colors.empty()) {
      state->draft_ui_colors = workspace_config.ui_colors;
    } else if (workspace_config.ui_colors_preset != theme::UiColorPreset::kCustom) {
      state->draft_ui_colors = theme::overrides_for_preset(workspace_config.ui_colors_preset);
    } else {
      state->draft_ui_colors = theme::overrides_for_preset(theme::UiColorPreset::kDarkClassic);
    }
    apply_draft_ui_colors(state);
  } else {
    theme::set_mode(state->draft_theme);
  }
  state->draft_clangd_use_gcc_query_driver = workspace_config.clangd_use_gcc_query_driver;
  state->draft_clangd_background_index = workspace_config.clangd_background_index;
  state->draft_clangd_extra_include_paths = workspace_config.clangd_extra_include_paths;
  state->draft_compile_commands = workspace_config.compile_commands;
  state->path_browser.launch_root = workspace_root.empty() ? canonical_browser_root("")
                                                             : workspace_root;
  if (state->has_workspace) {
    const std::string format_path = clang_format_path(workspace_root);
    state->clang_format_file_exists =
        !format_path.empty() && std::ifstream(format_path).good();
    state->draft_clang_format = load_clang_format(workspace_root);
  } else {
    state->clang_format_file_exists = false;
    state->draft_clang_format = default_clang_format_config();
  }
}

void close_settings_modal(SettingsModalState* state, AppSettings* settings,
                          SettingsApplyCallback on_apply,
                          WorkspaceSettingsApplyCallback on_workspace_apply,
                          ClangFormatApplyCallback on_clang_format_apply) {
  if (state == nullptr || settings == nullptr) {
    return;
  }
  settings->lsp_enabled = state->draft_lsp_enabled;
  settings->live_lsp_completion_enabled = state->draft_live_lsp_completion_enabled;
  settings->show_diagnostic_suffixes = state->draft_show_diagnostic_suffixes;
  settings->sticky_scroll_enabled = state->draft_sticky_scroll_enabled;
  settings->indent_guides_enabled = state->draft_indent_guides_enabled;
  settings->overview_ruler_enabled = state->draft_overview_ruler_enabled;
  settings->secondary_panel_enabled = state->draft_secondary_panel_enabled;
  settings->helix_mode_enabled = state->draft_helix_mode_enabled;
  settings->workspace_auto_detect_enabled = state->draft_workspace_auto_detect_enabled;
  settings->show_all_workspace_files = state->draft_show_all_workspace_files;
  settings->force_bundled_clangd = state->draft_force_bundled_clangd;
  settings->force_bundled_gdb = state->draft_force_bundled_gdb;
  settings->monitor_enabled = state->draft_monitor_enabled;
  settings->icon_mode = state->draft_icon_mode;
  settings->ui_locale = state->draft_ui_locale;
  settings->save();
  if (on_apply) {
    on_apply(*settings);
  }

  if (state->has_workspace && on_workspace_apply) {
    WorkspaceConfig workspace;
    workspace.clangd_use_gcc_query_driver = state->draft_clangd_use_gcc_query_driver;
    workspace.clangd_background_index = state->draft_clangd_background_index;
    workspace.clangd_extra_include_paths = state->draft_clangd_extra_include_paths;
    workspace.compile_commands = state->draft_compile_commands;
    workspace.theme = state->draft_theme;
    workspace.ui_colors_preset = state->draft_ui_colors_preset;
    workspace.ui_colors = state->draft_ui_colors;
    on_workspace_apply(workspace);
  } else {
    theme::set_mode(state->draft_theme);
    theme::set_ui_overrides(state->draft_ui_colors);
  }

  if (state->has_workspace) {
    save_clang_format(state->workspace_root, state->draft_clang_format);
    if (on_clang_format_apply) {
      on_clang_format_apply(state->draft_clang_format);
    }
  }

  state->open = false;
  state->panel = SettingsPanel::kGeneral;
  state->body_scroll = 0;
  state->body_scroll_panel = SettingsPanel::kGeneral;
  state->selected = 0;
  state->include_path_selected = 0;
  state->compile_commands_selected = 0;
  state->mapping_selected = 0;
}

Component MakeSettingsModalOverlay(Component main, SettingsModalState* state,
                                 AppSettings* settings, SettingsApplyCallback on_apply,
                                 WorkspaceSettingsApplyCallback on_workspace_apply,
                                 ClangFormatApplyCallback on_clang_format_apply) {
  if (state != nullptr) {
    state->clang_format_changed_callback = on_clang_format_apply;
  }
  return Renderer(
      CatchEvent(main, [state, settings, on_apply, on_workspace_apply,
                        on_clang_format_apply](Event event) {
        if (state == nullptr || !state->open) {
          return false;
        }
        if (event == Event::Escape || event == Event::F10) {
          switch (state->panel) {
            case SettingsPanel::kPathBrowser:
              state->panel = state->path_browser_purpose == PathBrowserPurpose::kMappingHostPath
                                 ? SettingsPanel::kPathMappings
                                 : SettingsPanel::kIncludePaths;
              return true;
            case SettingsPanel::kPathMappings:
              state->panel = SettingsPanel::kCompileCommands;
              return true;
            case SettingsPanel::kIncludePaths:
            case SettingsPanel::kCompileCommands:
            case SettingsPanel::kUiColors:
              state->panel = SettingsPanel::kWorkspace;
              return true;
            case SettingsPanel::kGeneral:
            case SettingsPanel::kWorkspace:
            case SettingsPanel::kFormat:
              close_settings_modal(state, settings, on_apply, on_workspace_apply,
                                   on_clang_format_apply);
              return true;
          }
        }
        return handle_settings_keys(state, event);
      }),
      [main, state] {
        Element base = main->Render();
        if (state == nullptr || !state->open) {
          return base;
        }

        clamp_top_level_selection(state);
        clamp_include_path_selection(state);
        clamp_compile_commands_selection(state);
        clamp_mapping_selection(state);
        clamp_ui_colors_selection(state);

        SettingsBodyContent content;
        std::string title = i18n::tr("settings.title");
        std::string footer;
        if (is_top_level_panel(state->panel)) {
          footer = state->has_workspace ? i18n::tr("settings.footer.with_tabs")
                                        : i18n::tr("settings.footer.no_tabs");
        }
        switch (state->panel) {
          case SettingsPanel::kGeneral:
            content = build_general_settings(state);
            break;
          case SettingsPanel::kWorkspace:
            content = build_workspace_settings(state);
            break;
          case SettingsPanel::kFormat:
            title = i18n::tr("settings.title.format");
            content = build_format_settings(state);
            break;
          case SettingsPanel::kIncludePaths:
            title = i18n::tr("settings.title.include_paths");
            footer = "";
            content = build_include_paths_panel(state);
            break;
          case SettingsPanel::kCompileCommands:
            title = i18n::tr("settings.title.compile_commands");
            footer = "";
            content = build_compile_commands_panel(state);
            break;
          case SettingsPanel::kPathMappings:
            title = i18n::tr("settings.title.path_mappings");
            footer = "";
            content = build_path_mappings_panel(state);
            break;
          case SettingsPanel::kPathBrowser:
            title = i18n::tr("settings.title.select_directory");
            footer = "";
            content = build_path_browser_panel(state);
            break;
          case SettingsPanel::kUiColors:
            title = i18n::tr("settings.title.ui_colors");
            footer = "";
            content = build_ui_colors_panel(state);
            break;
        }

        const int scrollable_lines = static_cast<int>(content.rows.size());
        Element body = render_settings_scroll_body(state, std::move(content));

        std::string config_note;
        const std::string global_path = AppSettings::config_path();
        if (!global_path.empty()) {
          config_note = i18n::tr_fmt("settings.config.global", {global_path});
        }
        if (state->has_workspace) {
          const std::string workspace_path = WorkspaceConfig::config_path(state->workspace_root);
          if (!config_note.empty()) {
            config_note += "  |  ";
          }
          config_note += i18n::tr_fmt("settings.config.workspace", {workspace_path});
          const std::string format_path = clang_format_path(state->workspace_root);
          if (!format_path.empty()) {
            config_note += "  |  ";
            config_note += i18n::tr_fmt("settings.config.clang_format", {format_path});
          }
        }
        if (config_note.empty()) {
          config_note = i18n::tr("settings.config.no_home");
        }

        if (scrollable_lines > kSettingsBodyHeight && !footer.empty()) {
          footer += i18n::tr("modal.shortcuts.footer.scroll");
        }

        Elements dialog_body = {std::move(body), separator() | color(theme::AccentDim()),
                              text(config_note) | color(theme::Muted())};
        if (!footer.empty()) {
          dialog_body.push_back(text(footer) | color(theme::Muted()));
        }

        Element dialog = ModalWindow(text(title) | color(theme::Accent()),
                                     vbox(std::move(dialog_body)));

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
