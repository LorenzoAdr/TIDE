#include "app/app_settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "i18n/locale.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

constexpr const char* kConfigDir = ".config/tuide";
constexpr const char* kConfigFile = "settings.json";

}  // namespace

std::string AppSettings::config_path() {
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    return {};
  }
  return (fs::path(home) / kConfigDir / kConfigFile).string();
}

AppSettings AppSettings::load() {
  AppSettings settings;
#ifdef TUIDE_DEFAULT_UI_LOCALE_TAG
  settings.ui_locale = i18n::parse_locale(TUIDE_DEFAULT_UI_LOCALE_TAG);
#endif
#ifdef TUIDE_DEFAULT_HELIX_MODE
  settings.helix_mode_enabled = true;
#endif
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_CLANGD
  settings.force_bundled_clangd = true;
#endif
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_GDB
  settings.force_bundled_gdb = true;
#endif
  const std::string path = config_path();
  if (path.empty()) {
    return settings;
  }

  std::ifstream input(path);
  if (!input) {
    return settings;
  }

  try {
    nlohmann::json doc;
    input >> doc;
    if (doc.contains("lsp_enabled") && doc["lsp_enabled"].is_boolean()) {
      settings.lsp_enabled = doc["lsp_enabled"].get<bool>();
    }
    if (doc.contains("live_lsp_completion_enabled") &&
        doc["live_lsp_completion_enabled"].is_boolean()) {
      settings.live_lsp_completion_enabled = doc["live_lsp_completion_enabled"].get<bool>();
    }
    if (doc.contains("show_diagnostic_suffixes") &&
        doc["show_diagnostic_suffixes"].is_boolean()) {
      settings.show_diagnostic_suffixes = doc["show_diagnostic_suffixes"].get<bool>();
    }
    if (doc.contains("sticky_scroll_enabled") &&
        doc["sticky_scroll_enabled"].is_boolean()) {
      settings.sticky_scroll_enabled = doc["sticky_scroll_enabled"].get<bool>();
    }
    if (doc.contains("indent_guides_enabled") && doc["indent_guides_enabled"].is_boolean()) {
      settings.indent_guides_enabled = doc["indent_guides_enabled"].get<bool>();
    }
    if (doc.contains("scope_highlight_enabled") && doc["scope_highlight_enabled"].is_boolean()) {
      settings.scope_highlight_enabled = doc["scope_highlight_enabled"].get<bool>();
    }
    if (doc.contains("rich_session_enabled") && doc["rich_session_enabled"].is_boolean()) {
      settings.rich_session_enabled = doc["rich_session_enabled"].get<bool>();
    }
    const bool legacy_visual =
        settings.scope_highlight_enabled && settings.rich_session_enabled;
    if (doc.contains("visual_highlight_enabled") &&
        doc["visual_highlight_enabled"].is_boolean()) {
      settings.visual_highlight_enabled = doc["visual_highlight_enabled"].get<bool>();
    } else {
      settings.visual_highlight_enabled = legacy_visual;
    }
    if (doc.contains("visual_brace_pair_colors_enabled") &&
        doc["visual_brace_pair_colors_enabled"].is_boolean()) {
      settings.visual_brace_pair_colors_enabled =
          doc["visual_brace_pair_colors_enabled"].get<bool>();
    } else {
      settings.visual_brace_pair_colors_enabled = legacy_visual;
    }
    if (doc.contains("visual_matching_bracket_enabled") &&
        doc["visual_matching_bracket_enabled"].is_boolean()) {
      settings.visual_matching_bracket_enabled =
          doc["visual_matching_bracket_enabled"].get<bool>();
    } else {
      settings.visual_matching_bracket_enabled = legacy_visual;
    }
    if (doc.contains("visual_scope_background_enabled") &&
        doc["visual_scope_background_enabled"].is_boolean()) {
      settings.visual_scope_background_enabled =
          doc["visual_scope_background_enabled"].get<bool>();
    } else {
      settings.visual_scope_background_enabled = settings.scope_highlight_enabled;
    }
    if (doc.contains("visual_scope_brace_highlight_enabled") &&
        doc["visual_scope_brace_highlight_enabled"].is_boolean()) {
      settings.visual_scope_brace_highlight_enabled =
          doc["visual_scope_brace_highlight_enabled"].get<bool>();
    } else {
      settings.visual_scope_brace_highlight_enabled = settings.scope_highlight_enabled;
    }
    if (doc.contains("visual_selection_occurrences_enabled") &&
        doc["visual_selection_occurrences_enabled"].is_boolean()) {
      settings.visual_selection_occurrences_enabled =
          doc["visual_selection_occurrences_enabled"].get<bool>();
    } else {
      settings.visual_selection_occurrences_enabled = settings.rich_session_enabled;
    }
    if (doc.contains("visual_code_folding_enabled") &&
        doc["visual_code_folding_enabled"].is_boolean()) {
      settings.visual_code_folding_enabled = doc["visual_code_folding_enabled"].get<bool>();
    } else {
      settings.visual_code_folding_enabled = settings.rich_session_enabled;
    }
    if (doc.contains("scope_highlight_strength") && doc["scope_highlight_strength"].is_number_integer()) {
      settings.scope_highlight_strength =
          std::max(10, std::min(85, doc["scope_highlight_strength"].get<int>()));
    }
    if (doc.contains("animations_enabled") && doc["animations_enabled"].is_boolean()) {
      settings.animations_enabled = doc["animations_enabled"].get<bool>();
    }
    if (doc.contains("overview_ruler_enabled") &&
        doc["overview_ruler_enabled"].is_boolean()) {
      settings.overview_ruler_enabled = doc["overview_ruler_enabled"].get<bool>();
    }
    if (doc.contains("secondary_panel_enabled") &&
        doc["secondary_panel_enabled"].is_boolean()) {
      settings.secondary_panel_enabled = doc["secondary_panel_enabled"].get<bool>();
    }
    if (doc.contains("force_bundled_clangd") && doc["force_bundled_clangd"].is_boolean()) {
      settings.force_bundled_clangd = doc["force_bundled_clangd"].get<bool>();
    }
    if (doc.contains("force_bundled_gdb") && doc["force_bundled_gdb"].is_boolean()) {
      settings.force_bundled_gdb = doc["force_bundled_gdb"].get<bool>();
    }
    if (doc.contains("monitor_enabled") && doc["monitor_enabled"].is_boolean()) {
      settings.monitor_enabled = doc["monitor_enabled"].get<bool>();
    }
    if (doc.contains("perf_dump_enabled") && doc["perf_dump_enabled"].is_boolean()) {
      settings.perf_dump_enabled = doc["perf_dump_enabled"].get<bool>();
    }
    if (doc.contains("development_options_enabled") &&
        doc["development_options_enabled"].is_boolean()) {
      settings.development_options_enabled = doc["development_options_enabled"].get<bool>();
    }
    if (doc.contains("passive_mode_enabled") && doc["passive_mode_enabled"].is_boolean()) {
      settings.passive_mode_enabled = doc["passive_mode_enabled"].get<bool>();
    }
    if (doc.contains("grace_window_ms") && doc["grace_window_ms"].is_number_integer()) {
      settings.grace_window_ms =
          std::max(100, std::min(10000, doc["grace_window_ms"].get<int>()));
    }
    if (doc.contains("lsp_hover_on_click_only") && doc["lsp_hover_on_click_only"].is_boolean()) {
      settings.lsp_hover_on_click_only = doc["lsp_hover_on_click_only"].get<bool>();
    }
    if (doc.contains("show_all_workspace_files") &&
        doc["show_all_workspace_files"].is_boolean()) {
      settings.show_all_workspace_files = doc["show_all_workspace_files"].get<bool>();
    }
    if (doc.contains("helix_mode_enabled") && doc["helix_mode_enabled"].is_boolean()) {
      settings.helix_mode_enabled = doc["helix_mode_enabled"].get<bool>();
    }
    if (doc.contains("workspace_auto_detect_enabled") &&
        doc["workspace_auto_detect_enabled"].is_boolean()) {
      settings.workspace_auto_detect_enabled = doc["workspace_auto_detect_enabled"].get<bool>();
    }
    if (doc.contains("icon_mode") && doc["icon_mode"].is_string()) {
      const std::string mode = doc["icon_mode"].get<std::string>();
      if (mode == "always") {
        settings.icon_mode = IconMode::Always;
      } else if (mode == "never") {
        settings.icon_mode = IconMode::Never;
      } else {
        settings.icon_mode = IconMode::Auto;
      }
    }
    if (doc.contains("ui_locale") && doc["ui_locale"].is_string()) {
      settings.ui_locale = i18n::parse_locale(doc["ui_locale"].get<std::string>());
    }
  } catch (...) {
    return AppSettings{};
  }
  return settings;
}

bool AppSettings::save() const {
  const std::string path = config_path();
  if (path.empty()) {
    return false;
  }

  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);

  nlohmann::json doc;
  doc["lsp_enabled"] = lsp_enabled;
  doc["live_lsp_completion_enabled"] = live_lsp_completion_enabled;
  doc["show_diagnostic_suffixes"] = show_diagnostic_suffixes;
  doc["sticky_scroll_enabled"] = sticky_scroll_enabled;
  doc["indent_guides_enabled"] = indent_guides_enabled;
  doc["visual_highlight_enabled"] = visual_highlight_enabled;
  doc["visual_brace_pair_colors_enabled"] = visual_brace_pair_colors_enabled;
  doc["visual_matching_bracket_enabled"] = visual_matching_bracket_enabled;
  doc["visual_scope_background_enabled"] = visual_scope_background_enabled;
  doc["visual_scope_brace_highlight_enabled"] = visual_scope_brace_highlight_enabled;
  doc["visual_selection_occurrences_enabled"] = visual_selection_occurrences_enabled;
  doc["visual_code_folding_enabled"] = visual_code_folding_enabled;
  doc["scope_highlight_enabled"] = scope_highlight_enabled;
  doc["rich_session_enabled"] = rich_session_enabled;
  doc["scope_highlight_strength"] = scope_highlight_strength;
  doc["animations_enabled"] = animations_enabled;
  doc["overview_ruler_enabled"] = overview_ruler_enabled;
  doc["secondary_panel_enabled"] = secondary_panel_enabled;
  doc["force_bundled_clangd"] = force_bundled_clangd;
  doc["force_bundled_gdb"] = force_bundled_gdb;
  doc["monitor_enabled"] = monitor_enabled;
  doc["perf_dump_enabled"] = perf_dump_enabled;
  doc["development_options_enabled"] = development_options_enabled;
  doc["passive_mode_enabled"] = passive_mode_enabled;
  doc["grace_window_ms"] = grace_window_ms;
  doc["lsp_hover_on_click_only"] = lsp_hover_on_click_only;
  doc["show_all_workspace_files"] = show_all_workspace_files;
  doc["helix_mode_enabled"] = helix_mode_enabled;
  doc["workspace_auto_detect_enabled"] = workspace_auto_detect_enabled;
  switch (icon_mode) {
    case IconMode::Always:
      doc["icon_mode"] = "always";
      break;
    case IconMode::Never:
      doc["icon_mode"] = "never";
      break;
    case IconMode::Auto:
    default:
      doc["icon_mode"] = "auto";
      break;
  }
  doc["ui_locale"] = i18n::locale_tag(ui_locale);

  std::ofstream output(path);
  if (!output) {
    return false;
  }
  output << doc.dump(2) << '\n';
  return static_cast<bool>(output);
}

}  // namespace tuide
