#include "ui/settings_modal.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"
#include "util/compile_commands_remap.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kTheme = 0;
constexpr int kLsp = 1;
constexpr int kDiagnosticSuffixes = 2;
constexpr int kStickyScroll = 3;
constexpr int kSecondaryPanel = 4;
constexpr int kBaseOptions = 5;

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

constexpr int kWorkspaceGccQueryDriver = kGlobalOptionCount;
constexpr int kWorkspaceBackgroundIndex = kGlobalOptionCount + 1;
constexpr int kWorkspaceIncludePaths = kGlobalOptionCount + 2;
constexpr int kWorkspaceCompileCommands = kGlobalOptionCount + 3;

constexpr int kCompileMode = 0;
constexpr int kCompileDetectMounts = 1;
constexpr int kCompileDockerContainer = 2;
constexpr int kCompilePathMappings = 3;
constexpr int kCompileCommandsRowCount = 4;

int main_option_count(const SettingsModalState* state) {
  if (state != nullptr && state->has_workspace) {
    return kGlobalOptionCount + 4;
  }
  return kGlobalOptionCount;
}

std::string compile_commands_mode_label(CompileCommandsMode mode) {
  switch (mode) {
    case CompileCommandsMode::kAuto:
      return "automático";
    case CompileCommandsMode::kHost:
      return "solo host";
    case CompileCommandsMode::kRemap:
      return "remapear";
    case CompileCommandsMode::kDockerSync:
      return "sincronizar Docker";
  }
  return "automático";
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
}

struct SettingsOption {
  const char* label;
  const char* description;
};

const std::vector<SettingsOption>& global_settings_options() {
  static const std::vector<SettingsOption> options = {
      {"Tema claro",
       "Interfaz con fondos claros (se guarda en .tgdb/config.json del workspace)"},
      {"clangd / LSP activo",
       "Outline, completado, diagnósticos y resaltado semántico vía clangd"},
      {"Sufijos de avisos en el código",
       "Muestra mensajes cortos de clangd al final de cada línea"},
      {"Sticky scroll en el editor",
       "Muestra encabezados de ámbito fijos al hacer scroll en el código"},
      {"Panel secundario (outline / búsqueda)",
       "Muestra la tercera columna con outline y búsqueda en el workspace"},
#ifdef TGDB_HAS_BUNDLED_CLANGD
      {"Forzar clangd embebido",
       "Usa solo el clangd del binario (ignora clangd en PATH salvo CLANGD_PATH)"},
#endif
#ifdef TGDB_HAS_BUNDLED_GDB
      {"Forzar gdb embebido",
       "Usa solo el gdb del binario (ignora gdb en PATH salvo GDB_PATH)"},
#endif
  };
  return options;
}

std::string checkbox_label(bool checked, const std::string& text) {
  return std::string(checked ? "[x] " : "[ ] ") + text;
}

bool option_checked(const SettingsModalState* state, int index) {
  if (state == nullptr) {
    return false;
  }
  switch (index) {
    case kTheme:
      return state->draft_theme == theme::ThemeMode::kLight;
    case kLsp:
      return state->draft_lsp_enabled;
    case kDiagnosticSuffixes:
      return state->draft_show_diagnostic_suffixes;
    case kStickyScroll:
      return state->draft_sticky_scroll_enabled;
    case kSecondaryPanel:
      return state->draft_secondary_panel_enabled;
#ifdef TGDB_HAS_BUNDLED_CLANGD
    case kForceBundledClangd:
      return state->draft_force_bundled_clangd;
#endif
#ifdef TGDB_HAS_BUNDLED_GDB
    case kForceBundledGdb:
      return state->draft_force_bundled_gdb;
#endif
    case kWorkspaceGccQueryDriver:
      return state->draft_clangd_use_gcc_query_driver;
    case kWorkspaceBackgroundIndex:
      return state->draft_clangd_background_index;
    default:
      return false;
  }
}

void toggle_option(SettingsModalState* state, int index) {
  if (state == nullptr) {
    return;
  }
  switch (index) {
    case kTheme:
      state->draft_theme = state->draft_theme == theme::ThemeMode::kLight
                               ? theme::ThemeMode::kDark
                               : theme::ThemeMode::kLight;
      theme::set_mode(state->draft_theme);
      break;
    case kLsp:
      state->draft_lsp_enabled = !state->draft_lsp_enabled;
      break;
    case kDiagnosticSuffixes:
      state->draft_show_diagnostic_suffixes = !state->draft_show_diagnostic_suffixes;
      break;
    case kStickyScroll:
      state->draft_sticky_scroll_enabled = !state->draft_sticky_scroll_enabled;
      break;
    case kSecondaryPanel:
      state->draft_secondary_panel_enabled = !state->draft_secondary_panel_enabled;
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

void clamp_main_selection(SettingsModalState* state) {
  if (state == nullptr) {
    return;
  }
  const int count = main_option_count(state);
  state->selected = std::max(0, std::min(state->selected, count - 1));
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

bool handle_main_settings_keys(SettingsModalState* state, Event event) {
  if (state == nullptr) {
    return false;
  }

  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->selected += 1;
    clamp_main_selection(state);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->selected -= 1;
    clamp_main_selection(state);
    return true;
  }
  if (event == Event::Return) {
    if (state->has_workspace && state->selected == kWorkspaceIncludePaths) {
      open_include_paths_panel(state);
      return true;
    }
    if (state->has_workspace && state->selected == kWorkspaceCompileCommands) {
      open_compile_commands_panel(state);
      return true;
    }
    toggle_option(state, state->selected);
    return true;
  }
  if (event == Event::Character(' ')) {
    if (state->has_workspace && state->selected == kWorkspaceIncludePaths) {
      open_include_paths_panel(state);
      return true;
    }
    if (state->has_workspace && state->selected == kWorkspaceCompileCommands) {
      open_compile_commands_panel(state);
      return true;
    }
    toggle_option(state, state->selected);
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
  const auto detected =
      detect_docker_mount_mappings(state->draft_compile_commands.docker_container);
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
    state->panel = SettingsPanel::kMain;
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
    state->panel = SettingsPanel::kMain;
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

  if (event == Event::Escape || event == Event::F10) {
    return true;
  }

  switch (state->panel) {
    case SettingsPanel::kMain:
      return handle_main_settings_keys(state, event);
    case SettingsPanel::kIncludePaths:
      return handle_include_paths_keys(state, event);
    case SettingsPanel::kCompileCommands:
      return handle_compile_commands_keys(state, event);
    case SettingsPanel::kPathMappings:
      return handle_path_mappings_keys(state, event);
    case SettingsPanel::kPathBrowser:
      return handle_path_browser_keys(state, event);
  }
  return true;
}

Element render_main_settings(const SettingsModalState* state) {
  Elements rows;
  const auto& options = global_settings_options();
  for (int i = 0; i < kGlobalOptionCount; ++i) {
    const auto& option = options[static_cast<std::size_t>(i)];
    const bool selected = i == state->selected;
    const bool checked = option_checked(state, i);

    Element title = text(checkbox_label(checked, option.label)) |
                    color(selected ? theme::Accent() : theme::Header()) | bold;
    if (selected) {
      title = title | inverted;
    }
    rows.push_back(title);
    rows.push_back(text("    " + std::string(option.description)) | color(theme::Muted()));
    rows.push_back(text(""));
  }

  if (state->has_workspace) {
    rows.push_back(separator() | color(theme::AccentDim()));
    rows.push_back(text("Workspace") | color(theme::Accent()) | bold);
    rows.push_back(text(""));

    {
      const bool selected = state->selected == kWorkspaceGccQueryDriver;
      const bool checked = state->draft_clangd_use_gcc_query_driver;
      Element title =
          text(checkbox_label(checked, "Consultar compilador GCC (query-driver)")) |
          color(selected ? theme::Accent() : theme::Header()) | bold;
      if (selected) {
        title = title | inverted;
      }
      rows.push_back(title);
      rows.push_back(
          text("    Pide a clangd los includes del sistema vía gcc/g++ cuando compile_commands "
               "no los cubre") |
          color(theme::Muted()));
      rows.push_back(text(""));
    }

    {
      const bool selected = state->selected == kWorkspaceBackgroundIndex;
      const bool checked = state->draft_clangd_background_index;
      Element title =
          text(checkbox_label(checked, "Indexado completo con clangd (background-index)")) |
          color(selected ? theme::Accent() : theme::Header()) | bold;
      if (selected) {
        title = title | inverted;
      }
      rows.push_back(title);
      rows.push_back(
          text("    Indexa todo el proyecto vía compile_commands (más RAM/CPU; mejor "
               "autocompletado global). Desactivado: solo archivos abiertos") |
          color(theme::Muted()));
      rows.push_back(text(""));
    }

    {
      const bool selected = state->selected == kWorkspaceIncludePaths;
      const std::size_t count = state->draft_clangd_extra_include_paths.size();
      const std::string label =
          "> Rutas include extra de clangd (" + std::to_string(count) + ")";
      Element title = text(label) | color(selected ? theme::Accent() : theme::Header()) | bold;
      if (selected) {
        title = title | inverted;
      }
      rows.push_back(title);
      rows.push_back(text("    Enter: gestionar rutas (recursivas) además de compile_commands") |
                     color(theme::Muted()));
      rows.push_back(text(""));
    }

    {
      const bool selected = state->selected == kWorkspaceCompileCommands;
      const std::string label = std::string("> compile_commands (") +
                                compile_commands_mode_label(state->draft_compile_commands.mode) +
                                ")";
      Element title = text(label) | color(selected ? theme::Accent() : theme::Header()) | bold;
      if (selected) {
        title = title | inverted;
      }
      rows.push_back(title);
      rows.push_back(text("    Enter: Docker/remap → .tgdb/compile_commands.json para clangd") |
                     color(theme::Muted()));
      rows.push_back(text(""));
    }
  }

  if (!rows.empty()) {
    rows.pop_back();
  }
  return vbox(std::move(rows));
}

Element render_compile_commands_panel(const SettingsModalState* state) {
  Elements rows;
  rows.push_back(text("compile_commands para clangd") | color(theme::Accent()) | bold);
  rows.push_back(text("Salida privada: .tgdb/compile_commands.json") | color(theme::Muted()));
  rows.push_back(separator());

  auto row = [&](int index, const std::string& label, const std::string& value) {
    const bool selected = index == state->compile_commands_selected;
    Element line = text(label + value) | color(selected ? theme::Accent() : theme::Header()) | bold;
    if (selected) {
      line = line | inverted;
    }
    rows.push_back(line);
  };

  row(kCompileMode, "Modo: ",
      compile_commands_mode_label(state->draft_compile_commands.mode));
  row(kCompileDetectMounts, checkbox_label(state->draft_compile_commands.docker_detect_mounts,
                                           "Detectar mounts de Docker"),
      "");
  const std::string container = state->draft_compile_commands.docker_container.empty()
                                  ? "(ninguno)"
                                  : state->draft_compile_commands.docker_container;
  row(kCompileDockerContainer, "Contenedor Docker: ", container);
  row(kCompilePathMappings, "> Mapeos de rutas (",
      std::to_string(state->draft_compile_commands.path_mappings.size()) + ")");

  rows.push_back(separator());
  rows.push_back(text("Fuente host: " + state->draft_compile_commands.source_path) |
                 color(theme::Muted()));
  if (!state->draft_compile_commands.docker_compile_commands_path.empty()) {
    rows.push_back(text("Ruta en Docker: " +
                        state->draft_compile_commands.docker_compile_commands_path) |
                   color(theme::Muted()));
  }
  rows.push_back(separator());
  rows.push_back(text("m modo   n contenedor   Esc volver") | color(theme::Muted()));
  return vbox(std::move(rows));
}

Element render_path_mappings_panel(const SettingsModalState* state) {
  Elements rows;
  rows.push_back(text("Mapeos de rutas (contenedor → host)") | color(theme::Accent()) | bold);
  rows.push_back(separator());
  if (state->draft_compile_commands.path_mappings.empty()) {
    rows.push_back(text("(sin mapeos)") | color(theme::Muted()));
  } else {
    for (int i = 0; i < static_cast<int>(state->draft_compile_commands.path_mappings.size()); ++i) {
      const bool selected = i == state->mapping_selected;
      const auto& mapping = state->draft_compile_commands.path_mappings[static_cast<std::size_t>(i)];
      const std::string line = mapping.from + "  →  " + mapping.to;
      Element row = text(line) | color(selected ? theme::Accent() : theme::Header());
      if (selected) {
        row = row | inverted;
      }
      rows.push_back(row);
    }
  }
  rows.push_back(separator());
  rows.push_back(text("a añadir (host)   r detectar Docker   d borrar   Esc volver") |
                 color(theme::Muted()));
  return vbox(std::move(rows));
}

Element render_include_paths_panel(const SettingsModalState* state) {
  Elements rows;
  rows.push_back(text("Rutas include extra (recursivas)") | color(theme::Accent()) | bold);
  rows.push_back(separator());
  if (state->draft_clangd_extra_include_paths.empty()) {
    rows.push_back(text("(sin rutas extra)") | color(theme::Muted()));
  } else {
    for (int i = 0; i < static_cast<int>(state->draft_clangd_extra_include_paths.size()); ++i) {
      const bool selected = i == state->include_path_selected;
      const std::string& path = state->draft_clangd_extra_include_paths[static_cast<std::size_t>(i)];
      Element line = text(path) | color(selected ? theme::Accent() : theme::Header());
      if (selected) {
        line = line | inverted;
      }
      rows.push_back(line);
    }
  }
  rows.push_back(separator());
  rows.push_back(text("a añadir   d borrar seleccionada   Esc volver") | color(theme::Muted()));
  return vbox(std::move(rows));
}

Element render_path_browser_panel(SettingsModalState* state) {
  state->path_browser.ensure_browser_entries();

  Elements body;
  body.push_back(text(state->path_browser_purpose == PathBrowserPurpose::kMappingHostPath
                          ? "Añadir ruta host para mapeo"
                          : "Añadir ruta include") |
                 color(theme::Accent()) | bold);
  body.push_back(separator());
  body.push_back(text(state->path_browser.browser_path) | color(theme::Muted()));
  body.push_back(separator());

  const int max_rows = 12;
  state->path_browser.browser_list_start = std::max(
      0, std::min(state->path_browser.selected,
                  std::max(0, static_cast<int>(state->path_browser.entries.size()) - max_rows)));
  const int start = state->path_browser.browser_list_start;
  const int end =
      std::min(static_cast<int>(state->path_browser.entries.size()), start + max_rows);
  Elements list_rows;
  for (int i = start; i < end; ++i) {
    const auto& row = state->path_browser.entries[static_cast<std::size_t>(i)];
    const std::string prefix = row.is_directory ? "[dir] " : "      ";
    const bool selected = i == state->path_browser.selected;
    Element line = text(prefix + row.name);
    if (row.is_directory) {
      line = line | color(theme::Accent());
    }
    if (selected) {
      line = line | inverted;
    }
    list_rows.push_back(line);
  }
  if (list_rows.empty()) {
    list_rows.push_back(text("(vacío)") | color(theme::Muted()));
  }
  body.push_back(vbox(std::move(list_rows)));
  body.push_back(separator());
  body.push_back(text("j/k  Enter carpeta  a=usar carpeta  Esc cancelar") | color(theme::Muted()));
  return vbox(std::move(body));
}

}  // namespace

void open_settings_modal(SettingsModalState* state, const AppSettings& settings,
                         const std::string& workspace_root,
                         const WorkspaceConfig& workspace_config) {
  if (state == nullptr) {
    return;
  }
  state->open = true;
  state->panel = SettingsPanel::kMain;
  state->selected = 0;
  state->include_path_selected = 0;
  state->compile_commands_selected = 0;
  state->mapping_selected = 0;
  state->draft_lsp_enabled = settings.lsp_enabled;
  state->draft_show_diagnostic_suffixes = settings.show_diagnostic_suffixes;
  state->draft_sticky_scroll_enabled = settings.sticky_scroll_enabled;
  state->draft_secondary_panel_enabled = settings.secondary_panel_enabled;
  state->draft_force_bundled_clangd = settings.force_bundled_clangd;
  state->draft_force_bundled_gdb = settings.force_bundled_gdb;
  state->draft_theme = workspace_config.theme;
  theme::set_mode(state->draft_theme);
  state->workspace_root = workspace_root;
  state->has_workspace = !workspace_root.empty();
  state->draft_clangd_use_gcc_query_driver = workspace_config.clangd_use_gcc_query_driver;
  state->draft_clangd_background_index = workspace_config.clangd_background_index;
  state->draft_clangd_extra_include_paths = workspace_config.clangd_extra_include_paths;
  state->draft_compile_commands = workspace_config.compile_commands;
  state->path_browser.launch_root = workspace_root.empty() ? canonical_browser_root("")
                                                             : workspace_root;
}

void close_settings_modal(SettingsModalState* state, AppSettings* settings,
                          SettingsApplyCallback on_apply,
                          WorkspaceSettingsApplyCallback on_workspace_apply) {
  if (state == nullptr || settings == nullptr) {
    return;
  }
  settings->lsp_enabled = state->draft_lsp_enabled;
  settings->show_diagnostic_suffixes = state->draft_show_diagnostic_suffixes;
  settings->sticky_scroll_enabled = state->draft_sticky_scroll_enabled;
  settings->secondary_panel_enabled = state->draft_secondary_panel_enabled;
  settings->force_bundled_clangd = state->draft_force_bundled_clangd;
  settings->force_bundled_gdb = state->draft_force_bundled_gdb;
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
    on_workspace_apply(workspace);
  } else {
    theme::set_mode(state->draft_theme);
  }

  state->open = false;
  state->panel = SettingsPanel::kMain;
  state->selected = 0;
  state->include_path_selected = 0;
  state->compile_commands_selected = 0;
  state->mapping_selected = 0;
}

Component MakeSettingsModalOverlay(Component main, SettingsModalState* state,
                                 AppSettings* settings, SettingsApplyCallback on_apply,
                                 WorkspaceSettingsApplyCallback on_workspace_apply) {
  return Renderer(
      CatchEvent(main, [state, settings, on_apply, on_workspace_apply](Event event) {
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
              state->panel = SettingsPanel::kMain;
              return true;
            case SettingsPanel::kMain:
              close_settings_modal(state, settings, on_apply, on_workspace_apply);
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

        clamp_main_selection(state);
        clamp_include_path_selection(state);
        clamp_compile_commands_selection(state);
        clamp_mapping_selection(state);

        Element body;
        std::string title = "Configuración";
        std::string footer = "↑↓ j/k  Espacio/Enter alternar   F10/Esc cerrar y guardar";
        switch (state->panel) {
          case SettingsPanel::kMain:
            body = render_main_settings(state);
            break;
          case SettingsPanel::kIncludePaths:
            title = "Rutas include de clangd";
            footer = "";
            body = render_include_paths_panel(state);
            break;
          case SettingsPanel::kCompileCommands:
            title = "compile_commands";
            footer = "";
            body = render_compile_commands_panel(state);
            break;
          case SettingsPanel::kPathMappings:
            title = "Mapeos de rutas";
            footer = "";
            body = render_path_mappings_panel(state);
            break;
          case SettingsPanel::kPathBrowser:
            title = "Seleccionar directorio";
            footer = "";
            body = render_path_browser_panel(state);
            break;
        }

        std::string config_note;
        const std::string global_path = AppSettings::config_path();
        if (!global_path.empty()) {
          config_note = "Global: " + global_path;
        }
        if (state->has_workspace) {
          const std::string workspace_path = WorkspaceConfig::config_path(state->workspace_root);
          if (!config_note.empty()) {
            config_note += "  |  ";
          }
          config_note += "Workspace: " + workspace_path;
        }
        if (config_note.empty()) {
          config_note = "HOME no definido; la configuración global no se guardará en disco";
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
