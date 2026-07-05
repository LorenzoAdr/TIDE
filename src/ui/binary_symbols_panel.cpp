#include "ui/binary_symbols_panel.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <thread>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "symbols/source_symbol_resolver.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/clickable.hpp"
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/text_input_style.hpp"
#include "ui/theme.hpp"
#include "util/nm_reader_runner.hpp"
#include "util/thread_name.hpp"

namespace tgdb {

using namespace ftxui;
namespace fs = std::filesystem;

namespace {

constexpr int kAddrWidth = 10;
constexpr int kSizeWidth = 6;
constexpr int kTypeWidth = 28;
constexpr int kBindingFilterCount = 7;

constexpr NmBindingFilter kBindingFilterOrder[kBindingFilterCount] = {
    NmBindingFilter::kAll,      NmBindingFilter::kUndefined, NmBindingFilter::kDefined,
    NmBindingFilter::kText,     NmBindingFilter::kData,      NmBindingFilter::kBss,
    NmBindingFilter::kWeak,
};

class SymbolFilterRunner {
 public:
  ~SymbolFilterRunner() {
    cancel();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  void start(std::shared_ptr<const std::vector<NmSymbol>> symbols, const std::string& query,
             NmBindingFilter binding_filter) {
    if (symbols == nullptr || symbols->empty()) {
      return;
    }
    cancel();
    const uint64_t gen = ++generation_;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ready_generation_ = 0;
      result_.clear();
    }
    if (worker_.joinable()) {
      worker_.detach();
    }
    running_ = true;
    worker_ = std::thread([this, gen, symbols, query, binding_filter] {
      set_current_thread_name("nm-filter");
      if (gen != generation_.load()) {
        running_ = false;
        return;
      }
      std::vector<int> indices;
      filter_nm_symbol_indices(*symbols, query, binding_filter, &indices);
      if (gen != generation_.load()) {
        running_ = false;
        return;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      if (gen == generation_.load()) {
        result_ = std::move(indices);
        ready_generation_ = gen;
      }
      running_ = false;
    });
  }

  void cancel() {
    generation_.fetch_add(1);
    running_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    ready_generation_ = 0;
  }

  bool running() const { return running_.load(); }

  bool poll(std::vector<int>* indices) {
    if (indices == nullptr) {
      return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (ready_generation_ == 0) {
      return false;
    }
    *indices = std::move(result_);
    ready_generation_ = 0;
    return true;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<int> result_;
  uint64_t ready_generation_ = 0;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> generation_{0};
};

struct BinarySymbolsPanelState {
  std::string binary_path;
  std::string filter_query;
  std::string applied_name_filter;
  bool filter_dirty = false;
  NmBindingFilter binding_filter = NmBindingFilter::kAll;
  std::shared_ptr<const std::vector<NmSymbol>> symbols;
  std::vector<int> filtered_indices;
  int selected = 0;
  int first_visible = 0;
  int last_visible_lines = 1;
  Box list_content_box;
  Box scrollbar_box;
  ScrollbarLayout scrollbar_layout;
  bool scrollbar_dragging = false;
  int scrollbar_drag_offset = 0;
  Box filter_box;
  std::array<Box, kBindingFilterCount> binding_filter_boxes{};
  std::string status;
  bool loading = false;
  bool filtering = false;
  NmReaderRunner runner;
  SymbolFilterRunner filter_runner;
  std::size_t last_shell_output_size = 0;
  bool pending_shell_scan = false;
  std::string pending_select_name;
  uint64_t last_custom_tick_processed = 0;
};

bool symbols_panel_loading(const BinarySymbolsPanelState& state,
                           const MainLayoutState* layout_state) {
  if (state.loading) {
    return true;
  }
  return binary_symbols_request_pending(layout_state);
}

std::string symbols_panel_binary_path(const BinarySymbolsPanelState& state,
                                      const MainLayoutState* layout_state) {
  if (!state.binary_path.empty()) {
    return state.binary_path;
  }
  if (layout_state != nullptr && !layout_state->binary_symbols_pending.binary_path.empty()) {
    return layout_state->binary_symbols_pending.binary_path;
  }
  return {};
}

int visible_line_count(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

int list_viewport_lines(const BinarySymbolsPanelState& state) {
  if (state.list_content_box.y_max >= state.list_content_box.y_min) {
    return visible_line_count(state.list_content_box);
  }
  return std::max(1, state.last_visible_lines);
}

std::string format_address(const NmSymbol& symbol) {
  if (!symbol.has_address) {
    return "        -";
  }
  std::ostringstream stream;
  stream << std::hex << symbol.address;
  std::string text = stream.str();
  if (text.size() > static_cast<std::size_t>(kAddrWidth)) {
    return text.substr(text.size() - kAddrWidth);
  }
  return std::string(kAddrWidth - text.size(), '0') + text;
}

std::string format_size(const NmSymbol& symbol) {
  if (!symbol.has_size || symbol.size == 0) {
    return "-";
  }
  std::ostringstream stream;
  stream << std::hex << symbol.size;
  return stream.str();
}

Color category_color(NmSymbolType category) {
  switch (category) {
    case NmSymbolType::kUndefined:
      return theme::Error();
    case NmSymbolType::kText:
      return theme::Accent();
    case NmSymbolType::kData:
      return theme::Success();
    case NmSymbolType::kBss:
      return theme::Warning();
    case NmSymbolType::kWeak:
      return theme::Muted();
    default:
      return theme::UiText();
  }
}

void schedule_async_filter(BinarySymbolsPanelState* state, MainLayoutState* layout_state) {
  if (state == nullptr) {
    return;
  }
  state->filter_dirty = state->filter_query != state->applied_name_filter;
  if (state->symbols == nullptr || state->symbols->empty()) {
    state->filtered_indices.clear();
    state->filtering = false;
    state->filter_runner.cancel();
    return;
  }
  if (state->applied_name_filter.empty() && state->binding_filter == NmBindingFilter::kAll) {
    state->filter_runner.cancel();
    state->filtered_indices.resize(state->symbols->size());
    std::iota(state->filtered_indices.begin(), state->filtered_indices.end(), 0);
    state->filtering = false;
    if (layout_state != nullptr) {
      layout_state->request_ui_tick = true;
    }
    return;
  }
  state->filtering = true;
  state->filter_runner.start(state->symbols, state->applied_name_filter, state->binding_filter);
  if (layout_state != nullptr) {
    layout_state->request_ui_tick = true;
  }
}

void apply_name_filter(BinarySymbolsPanelState* state, MainLayoutState* layout_state) {
  if (state == nullptr) {
    return;
  }
  state->applied_name_filter = state->filter_query;
  state->filter_dirty = false;
  state->selected = 0;
  state->first_visible = 0;
  schedule_async_filter(state, layout_state);
}

void apply_binding_filter(BinarySymbolsPanelState* state, MainLayoutState* layout_state,
                          NmBindingFilter filter) {
  if (state == nullptr) {
    return;
  }
  state->binding_filter = filter;
  state->selected = 0;
  state->first_visible = 0;
  schedule_async_filter(state, layout_state);
}

std::optional<NmBindingFilter> binding_filter_from_key(const Event& event) {
  if (event == Event::Character('1')) {
    return NmBindingFilter::kAll;
  }
  if (event == Event::Character('2')) {
    return NmBindingFilter::kUndefined;
  }
  if (event == Event::Character('3')) {
    return NmBindingFilter::kDefined;
  }
  if (event == Event::Character('4')) {
    return NmBindingFilter::kText;
  }
  if (event == Event::Character('5')) {
    return NmBindingFilter::kData;
  }
  if (event == Event::Character('6')) {
    return NmBindingFilter::kBss;
  }
  if (event == Event::Character('7')) {
    return NmBindingFilter::kWeak;
  }
  return std::nullopt;
}

bool handle_binding_filter_click(BinarySymbolsPanelState* state, MainLayoutState* layout_state,
                                 int x, int y) {
  if (state == nullptr) {
    return false;
  }
  for (int i = 0; i < kBindingFilterCount; ++i) {
    const Box& box = state->binding_filter_boxes[i];
    if (box.Contain(x, y)) {
      apply_binding_filter(state, layout_state, kBindingFilterOrder[i]);
      return true;
    }
  }
  return false;
}

void clamp_scroll_viewport(BinarySymbolsPanelState* state, int visible_lines) {
  const int total = static_cast<int>(state->filtered_indices.size());
  const int max_first = std::max(0, total - visible_lines);
  state->first_visible = std::max(0, std::min(state->first_visible, max_first));
}

void clamp_scroll_to_selection(BinarySymbolsPanelState* state, int visible_lines) {
  clamp_scroll_viewport(state, visible_lines);
  if (state->selected < state->first_visible) {
    state->first_visible = state->selected;
  } else if (state->selected >= state->first_visible + visible_lines) {
    state->first_visible = state->selected - visible_lines + 1;
  }
  clamp_scroll_viewport(state, visible_lines);
}

void start_analysis(BinarySymbolsPanelState* state, MainLayoutState* layout_state,
                    const std::string& binary_path) {
  if (state == nullptr || binary_path.empty()) {
    return;
  }
  state->runner.cancel();
  state->binary_path = binary_path;
  state->loading = true;
  state->status = "Analizando " + fs::path(binary_path).filename().string() + "...";
  state->symbols.reset();
  state->filtered_indices.clear();
  state->filter_query.clear();
  state->applied_name_filter.clear();
  state->filter_dirty = false;
  state->filtering = false;
  state->filter_runner.cancel();
  state->selected = 0;
  state->first_visible = 0;
  state->runner.start(binary_path);
  if (layout_state != nullptr) {
    layout_state->request_ui_tick = true;
  }
}

int find_filtered_index_by_name(const BinarySymbolsPanelState& state, const std::string& name) {
  if (state.symbols == nullptr) {
    return -1;
  }
  for (int i = 0; i < static_cast<int>(state.filtered_indices.size()); ++i) {
    const int symbol_index = state.filtered_indices[static_cast<std::size_t>(i)];
    if (symbol_index >= 0 && symbol_index < static_cast<int>(state.symbols->size()) &&
        (*state.symbols)[static_cast<std::size_t>(symbol_index)].name == name) {
      return i;
    }
  }
  return -1;
}

void navigate_to_symbol(WorkspaceModel* workspace, MainLayoutState* layout_state,
                        const NmSymbol& symbol, SymbolWorkspaceIndexer* symbol_indexer,
                        const std::shared_ptr<ISymbolProvider>& symbols,
                        WorkspaceIndexer* file_indexer) {
  if (workspace == nullptr) {
    return;
  }
  const std::string workspace_root = workspace->root;
  const auto loc = resolve_nm_symbol_in_workspace(symbol, workspace_root, symbol_indexer, symbols,
                                                  file_indexer);
  if (!loc.has_value() || !loc->valid) {
    workspace->status_message = "Sin ubicación en código para: " + symbol.name;
    return;
  }
  workspace->record_cursor_jump();
  workspace->open_file_at(loc->path, loc->line, loc->character);
  workspace->status_message = "→ " + fs::path(loc->path).filename().string() + ":" +
                              std::to_string(loc->line + 1) + ":" +
                              std::to_string(loc->character + 1);
  if (layout_state != nullptr) {
    schedule_editor_navigation(layout_state, *loc);
  }
}

void apply_pending_request(BinarySymbolsPanelState* state, MainLayoutState* layout_state,
                           DebugModel* model) {
  if (state == nullptr || layout_state == nullptr) {
    return;
  }
  auto& pending = layout_state->binary_symbols_pending;

  std::string target_path = pending.binary_path;
  if (target_path.empty() && model != nullptr && !model->program.empty()) {
    target_path = model->program;
  }

  const bool has_new_binary = !target_path.empty() &&
                              (pending.refresh || target_path != state->binary_path);
  if (has_new_binary || pending.open_tab) {
    layout_state->console_visible = true;
    layout_state->console_tabs.selected_tab = ConsolePanelTabs::kBinarySymbols;
    layout_state->focus_sync_needed = true;
  }

  bool started_analysis = false;
  if (has_new_binary) {
    if (pending.start_after_paint == 0) {
      pending.start_after_paint = layout_state->ui_paint_count + 1;
      pending.open_tab = false;
      return;
    }
    if (layout_state->ui_paint_count < pending.start_after_paint) {
      pending.open_tab = false;
      return;
    }
    pending.start_after_paint = 0;
    start_analysis(state, layout_state, target_path);
    started_analysis = true;
    pending.refresh = false;
  }

  bool schedule_filter = started_analysis;
  if (!pending.name_filter.empty()) {
    state->filter_query = pending.name_filter;
    state->applied_name_filter = pending.name_filter;
    state->filter_dirty = false;
    pending.name_filter.clear();
    schedule_filter = true;
  }
  if (pending.binding_filter != NmBindingFilter::kAll) {
    state->binding_filter = pending.binding_filter;
    pending.binding_filter = NmBindingFilter::kAll;
    schedule_filter = true;
  }
  if (schedule_filter) {
    schedule_async_filter(state, layout_state);
  }
  if (!pending.select_symbol_name.empty()) {
    state->pending_select_name = pending.select_symbol_name;
    pending.select_symbol_name.clear();
  }

  pending.open_tab = false;
  if (started_analysis) {
    pending.binary_path.clear();
  }
}

bool poll_runner(BinarySymbolsPanelState* state, MainLayoutState* layout_state) {
  if (state == nullptr || !state->loading) {
    return false;
  }
  NmReadResult result;
  if (!state->runner.poll(&result)) {
    if (state->runner.running()) {
      return false;
    }
    if (state->loading) {
      state->loading = false;
      state->status = "Análisis cancelado";
      if (layout_state != nullptr) {
        layout_state->request_ui_tick = true;
      }
      return true;
    }
    return false;
  }
  state->loading = false;
  state->symbols = std::make_shared<std::vector<NmSymbol>>(std::move(result.symbols));
  if (!result.error.empty() && state->symbols->empty()) {
    state->status = result.error;
  } else {
    state->status = std::to_string(state->symbols->size()) + " símbolos";
    if (!result.error.empty()) {
      state->status += " (" + result.error + ")";
    }
  }
  schedule_async_filter(state, layout_state);
  if (layout_state != nullptr) {
    layout_state->request_ui_tick = true;
  }
  return true;
}

NmBindingFilter cycle_binding_filter(NmBindingFilter current) {
  switch (current) {
    case NmBindingFilter::kAll:
      return NmBindingFilter::kUndefined;
    case NmBindingFilter::kUndefined:
      return NmBindingFilter::kDefined;
    case NmBindingFilter::kDefined:
      return NmBindingFilter::kText;
    case NmBindingFilter::kText:
      return NmBindingFilter::kData;
    case NmBindingFilter::kData:
      return NmBindingFilter::kBss;
    case NmBindingFilter::kBss:
      return NmBindingFilter::kWeak;
    case NmBindingFilter::kWeak:
    default:
      return NmBindingFilter::kAll;
  }
}

Element make_symbol_row(const NmSymbol& symbol, bool selected) {
  Element row = hbox({
      text(format_address(symbol)) | size(WIDTH, EQUAL, kAddrWidth),
      text(" ") | size(WIDTH, EQUAL, 1),
      text(format_size(symbol)) | size(WIDTH, EQUAL, kSizeWidth),
      text(" ") | size(WIDTH, EQUAL, 1),
      text(symbol.translated_type) | size(WIDTH, EQUAL, kTypeWidth) |
          color(category_color(symbol.category)),
      text(" ") | size(WIDTH, EQUAL, 1),
      text(symbol.name) | flex,
  });
  if (selected) {
    row = row | inverted | bold;
  } else {
    row = row | color(category_color(symbol.category));
  }
  return row | size(HEIGHT, EQUAL, 1);
}

void activate_filter_input(MainLayoutState* layout_state, FocusManagerState* focus,
                           Component filter_input) {
  if (layout_state == nullptr) {
    return;
  }
  layout_state->text_input_focus = TextInputFocus::BinarySymbolsFilter;
  if (focus != nullptr) {
    focus->region = FocusRegion::Terminal;
  }
  filter_input->TakeFocus();
  cursor_blink::show();
}

Element render_name_filter_field(const BinarySymbolsPanelState& state, bool filter_active,
                                 const InputOption& filter_input_option) {
  if (filter_active) {
    Element field =
        hbox({RenderBlinkInputLine(state.filter_query, filter_input_option.cursor_position(), true),
              filler()}) |
        flex | bgcolor(theme::TabIdle()) | size(HEIGHT, EQUAL, 1);
    return clear_under(std::move(field));
  }
  std::string preview =
      state.filter_query.empty() ? "Filtrar por nombre..." : state.filter_query;
  if (state.filter_dirty) {
    preview += " *";
  }
  Element field = ModalInputLine(preview) | flex;
  if (state.filter_query.empty()) {
    field = field | dim;
  }
  return field;
}

bool handle_binary_symbols_scrollbar_mouse(BinarySymbolsPanelState* state,
                                           MainLayoutState* layout_state, const Mouse& m,
                                           int total, int visible) {
  if (state == nullptr || !state->scrollbar_layout.scrollable) {
    return false;
  }
  const int max_first = std::max(0, total - visible);
  const bool in_bar = state->scrollbar_box.Contain(m.x, m.y);

  if (m.motion == Mouse::Moved) {
    if (layout_state != nullptr) {
      const std::string_view before = layout_state->clickable.hovered_id();
      if (in_bar || state->scrollbar_dragging) {
        layout_state->clickable.set_hover(press_id::kEditorScrollbar);
      } else {
        layout_state->clickable.clear_hover_if(
            [](std::string_view id) { return id == press_id::kEditorScrollbar; });
      }
      if (layout_state->clickable.hovered_id() != before) {
        layout_state->request_ui_tick = true;
      }
    }
    if (state->scrollbar_dragging) {
      const int local_y = m.y - state->scrollbar_box.y_min;
      const int thumb_top = local_y - state->scrollbar_drag_offset;
      state->first_visible =
          std::max(0, std::min(scroll_for_thumb_top(state->scrollbar_layout, thumb_top), max_first));
      return true;
    }
    return in_bar;
  }

  if (state->scrollbar_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      state->scrollbar_dragging = false;
      return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Moved) {
      const int local_y = m.y - state->scrollbar_box.y_min;
      const int thumb_top = local_y - state->scrollbar_drag_offset;
      state->first_visible =
          std::max(0, std::min(scroll_for_thumb_top(state->scrollbar_layout, thumb_top), max_first));
      return true;
    }
  }

  if (!in_bar) {
    return false;
  }

  if (m.button == Mouse::WheelUp) {
    state->first_visible = std::max(0, state->first_visible - 3);
    return true;
  }
  if (m.button == Mouse::WheelDown) {
    state->first_visible = std::min(state->first_visible + 3, max_first);
    return true;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
    trigger_press(layout_state, press_id::kEditorScrollbar);
    const int local_y = m.y - state->scrollbar_box.y_min;
    if (scrollbar_thumb_hit(state->scrollbar_layout, state->scrollbar_box, m.x, m.y)) {
      state->scrollbar_dragging = true;
      state->scrollbar_drag_offset = local_y - state->scrollbar_layout.thumb_y;
    } else {
      const int thumb_top = local_y - state->scrollbar_layout.thumb_height / 2;
      state->first_visible = std::max(
          0, std::min(scroll_for_thumb_top(state->scrollbar_layout, thumb_top), max_first));
      state->scrollbar_dragging = true;
      state->scrollbar_drag_offset = state->scrollbar_layout.thumb_height / 2;
    }
    return true;
  }

  return false;
}

bool scroll_list_by_wheel(BinarySymbolsPanelState* state, int delta, int visible) {
  if (state == nullptr || delta == 0) {
    return false;
  }
  const int total = static_cast<int>(state->filtered_indices.size());
  const int max_first = std::max(0, total - visible);
  if (delta < 0) {
    state->first_visible = std::max(0, state->first_visible + delta);
  } else {
    state->first_visible = std::min(state->first_visible + delta, max_first);
  }
  clamp_scroll_viewport(state, visible);
  return true;
}

bool forward_filter_input(Event event, Component filter_input) {
  if (event.is_character() || event == Event::Backspace || event == Event::Delete ||
      event == Event::ArrowLeft || event == Event::ArrowRight || event == Event::Home ||
      event == Event::End) {
    return filter_input->OnEvent(event);
  }
  return false;
}

bool poll_filter_runner(BinarySymbolsPanelState* state, MainLayoutState* layout_state) {
  if (state == nullptr || !state->filtering) {
    return false;
  }
  std::vector<int> indices;
  if (!state->filter_runner.poll(&indices)) {
    if (state->filter_runner.running()) {
      return false;
    }
    if (state->filtering) {
      state->filtering = false;
      if (layout_state != nullptr) {
        layout_state->request_ui_tick = true;
      }
      return true;
    }
    return false;
  }
  state->filtering = false;
  state->filtered_indices = std::move(indices);
  if (state->selected >= static_cast<int>(state->filtered_indices.size())) {
    state->selected = std::max(0, static_cast<int>(state->filtered_indices.size()) - 1);
  }
  if (!state->pending_select_name.empty()) {
    const int idx = find_filtered_index_by_name(*state, state->pending_select_name);
    if (idx >= 0) {
      state->selected = idx;
    }
    state->pending_select_name.clear();
  }
  if (layout_state != nullptr) {
    layout_state->request_ui_tick = true;
  }
  return true;
}

void tick_binary_symbols_panel(BinarySymbolsPanelState* state, MainLayoutState* layout_state,
                               DebugModel* model, ShellSession* shell) {
  if (state == nullptr || layout_state == nullptr) {
    return;
  }
  if (state->last_custom_tick_processed == layout_state->ui_custom_tick) {
    return;
  }
  state->last_custom_tick_processed = layout_state->ui_custom_tick;
  apply_pending_request(state, layout_state, model);
  poll_runner(state, layout_state);
  poll_filter_runner(state, layout_state);

  if (shell != nullptr && state->pending_shell_scan) {
    const std::string screen = shell->screen_text();
    if (screen.size() != state->last_shell_output_size) {
      state->last_shell_output_size = screen.size();
      scan_shell_output_for_linker_errors(screen, layout_state, model);
    }
    state->pending_shell_scan = false;
  }

  if (state->runner.running() && layout_state != nullptr) {
    layout_state->request_ui_tick = true;
  }
  if (state->filter_runner.running() && layout_state != nullptr) {
    layout_state->request_ui_tick = true;
  }
}

}  // namespace

void request_binary_symbols_panel(MainLayoutState* layout_state, const std::string& binary_path,
                                  const std::string& name_filter, NmBindingFilter binding_filter,
                                  bool open_tab) {
  if (layout_state == nullptr) {
    return;
  }
  auto& pending = layout_state->binary_symbols_pending;
  if (open_tab || !binary_path.empty()) {
    pending.open_tab = true;
    layout_state->console_visible = true;
    layout_state->console_tabs.selected_tab = ConsolePanelTabs::kBinarySymbols;
    layout_state->focus_sync_needed = true;
  }
  if (!binary_path.empty()) {
    pending.binary_path = binary_path;
    pending.start_after_paint = 0;
  }
  if (!name_filter.empty()) {
    pending.name_filter = name_filter;
    pending.select_symbol_name = name_filter;
  }
  if (binding_filter != NmBindingFilter::kAll) {
    pending.binding_filter = binding_filter;
  }
  layout_state->request_ui_tick = true;
}

void refresh_binary_symbols_if_matches(MainLayoutState* layout_state,
                                       const std::string& binary_path) {
  if (layout_state == nullptr || binary_path.empty()) {
    return;
  }
  layout_state->binary_symbols_pending.refresh = true;
  layout_state->binary_symbols_pending.binary_path = binary_path;
  layout_state->binary_symbols_pending.start_after_paint = 0;
  layout_state->binary_symbols_pending.open_tab = false;
  layout_state->request_ui_tick = true;
}

void scan_shell_output_for_linker_errors(const std::string& output, MainLayoutState* layout_state,
                                         DebugModel* model) {
  if (layout_state == nullptr || output.empty()) {
    return;
  }
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    const auto symbol = parse_linker_undefined_reference(line);
    if (!symbol.has_value()) {
      continue;
    }
    std::string binary_path;
    if (model != nullptr && !model->program.empty()) {
      binary_path = model->program;
    }
    request_binary_symbols_panel(layout_state, binary_path, *symbol, NmBindingFilter::kUndefined,
                                 true);
    break;
  }
}

Component MakeBinarySymbolsPanel(WorkspaceModel* workspace, DebugModel* model,
                                 FocusManagerState* focus, MainLayoutState* layout_state,
                                 const std::shared_ptr<ISymbolProvider>& symbols,
                                 SymbolWorkspaceIndexer* symbol_indexer,
                                 WorkspaceIndexer* file_indexer, ShellSession* shell) {
  auto state = std::make_shared<BinarySymbolsPanelState>();

  auto filter_input_option = std::make_shared<InputOption>(
      MakeBlinkInputOption(&state->filter_query, "Filtrar por nombre..."));
  auto filter_input = Input(*filter_input_option);
  Components children = {filter_input};
  auto input_layers = Container::Vertical(children);

  auto handler = [workspace, model, focus, state, layout_state, symbols, symbol_indexer,
                  file_indexer, shell, filter_input](Event event) -> bool {
    if (layout_state == nullptr) {
      return false;
    }
    const bool pending_work = layout_state->binary_symbols_pending.open_tab ||
                              !layout_state->binary_symbols_pending.binary_path.empty() ||
                              layout_state->binary_symbols_pending.refresh;
    const bool tab_active =
        layout_state->console_tabs.selected_tab == ConsolePanelTabs::kBinarySymbols;
    if (!pending_work && !tab_active) {
      return false;
    }

    if (event == Event::Custom) {
      tick_binary_symbols_panel(state.get(), layout_state, model, shell);
      return true;
    }

    if (!tab_active) {
      return false;
    }

    const bool filter_active =
        layout_state->text_input_focus == TextInputFocus::BinarySymbolsFilter;

    if (event.is_mouse()) {
      const auto& m = event.mouse();
      const int visible = list_viewport_lines(*state);
      const int total = static_cast<int>(state->filtered_indices.size());
      if (handle_binary_symbols_scrollbar_mouse(state.get(), layout_state, m, total, visible)) {
        layout_state->request_ui_tick = true;
        return true;
      }
      if (m.button == Mouse::Left && m.motion == Mouse::Pressed &&
          handle_binding_filter_click(state.get(), layout_state, m.x, m.y)) {
        if (focus != nullptr) {
          focus->region = FocusRegion::Terminal;
        }
        layout_state->text_input_focus = TextInputFocus::None;
        layout_state->request_ui_tick = true;
        return true;
      }
      if (state->filter_box.Contain(m.x, m.y) && m.button == Mouse::Left &&
          m.motion == Mouse::Pressed) {
        activate_filter_input(layout_state, focus, filter_input);
        layout_state->request_ui_tick = true;
        return true;
      }
      if ((state->list_content_box.Contain(m.x, m.y) || state->scrollbar_box.Contain(m.x, m.y)) &&
          (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown)) {
        const int delta = m.button == Mouse::WheelUp ? -3 : 3;
        if (scroll_list_by_wheel(state.get(), delta, visible)) {
          layout_state->request_ui_tick = true;
          return true;
        }
      }
      if (m.button == Mouse::Left && m.motion == Mouse::Pressed &&
          state->list_content_box.Contain(m.x, m.y)) {
        if (focus != nullptr) {
          focus->region = FocusRegion::Terminal;
        }
        layout_state->text_input_focus = TextInputFocus::None;
        const int visual_row = m.y - state->list_content_box.y_min;
        const int row = state->first_visible + visual_row;
        if (row < 0 || row >= static_cast<int>(state->filtered_indices.size()) ||
            state->symbols == nullptr) {
          return true;
        }
        state->selected = row;
        clamp_scroll_to_selection(state.get(), visible);
        const int symbol_index = state->filtered_indices[static_cast<std::size_t>(row)];
        navigate_to_symbol(workspace, layout_state,
                         (*state->symbols)[static_cast<std::size_t>(symbol_index)], symbol_indexer,
                         symbols, file_indexer);
        if (focus != nullptr && !m.control) {
          focus->region = FocusRegion::Editor;
        }
        layout_state->request_ui_tick = true;
        return true;
      }
      if (event.mouse().motion == Mouse::Moved && state->list_content_box.Contain(m.x, m.y)) {
        if (focus != nullptr) {
          focus->region = FocusRegion::Terminal;
        }
        if (filter_active) {
          layout_state->text_input_focus = TextInputFocus::None;
        }
      }
    }

    if (const auto binding = binding_filter_from_key(event)) {
      apply_binding_filter(state.get(), layout_state, *binding);
      if (filter_active) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      return true;
    }

    if (filter_active) {
      if (event == Event::Escape) {
        layout_state->text_input_focus = TextInputFocus::None;
        return true;
      }
      if (event == Event::Tab) {
        layout_state->text_input_focus = TextInputFocus::None;
        apply_binding_filter(state.get(), layout_state,
                             cycle_binding_filter(state->binding_filter));
        return true;
      }
      if (event == Event::Return) {
        apply_name_filter(state.get(), layout_state);
        layout_state->text_input_focus = TextInputFocus::None;
        return true;
      }
      if (forward_filter_input(event, filter_input)) {
        state->filter_dirty = state->filter_query != state->applied_name_filter;
        cursor_blink::show();
        layout_state->request_ui_tick = true;
        return true;
      }
      return false;
    }

    if (is_editor_chrome_input_focus(layout_state->text_input_focus)) {
      return false;
    }

    if (event == Event::Character('r')) {
      if (!state->binary_path.empty()) {
        start_analysis(state.get(), layout_state, state->binary_path);
      }
      return true;
    }

    if (event == Event::Escape && state->loading) {
      state->runner.cancel();
      state->loading = false;
      state->status = "Análisis cancelado";
      layout_state->request_ui_tick = true;
      return true;
    }

    if (event == Event::Escape && binary_symbols_request_pending(layout_state)) {
      layout_state->binary_symbols_pending = {};
      state->status = "Análisis cancelado";
      layout_state->request_ui_tick = true;
      return true;
    }

    if (event == Event::Tab) {
      apply_binding_filter(state.get(), layout_state, cycle_binding_filter(state->binding_filter));
      return true;
    }

    if (event == Event::Character('/')) {
      activate_filter_input(layout_state, focus, filter_input);
      return true;
    }

    const int visible = list_viewport_lines(*state);
    if (state->filtered_indices.empty()) {
      return false;
    }

    if (event == Event::ArrowDown || event == Event::Character('j')) {
      state->selected =
          std::min(state->selected + 1, static_cast<int>(state->filtered_indices.size()) - 1);
      clamp_scroll_to_selection(state.get(), visible);
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      state->selected = std::max(0, state->selected - 1);
      clamp_scroll_to_selection(state.get(), visible);
      return true;
    }
    if (event == Event::Return) {
      if (state->symbols == nullptr) {
        return true;
      }
      const int symbol_index = state->filtered_indices[static_cast<std::size_t>(state->selected)];
      navigate_to_symbol(workspace, layout_state,
                         (*state->symbols)[static_cast<std::size_t>(symbol_index)], symbol_indexer,
                         symbols, file_indexer);
      if (focus != nullptr) {
        focus->region = FocusRegion::Editor;
      }
      return true;
    }
    if (event == Event::PageDown) {
      state->selected = std::min(state->selected + visible,
                                 static_cast<int>(state->filtered_indices.size()) - 1);
      clamp_scroll_to_selection(state.get(), visible);
      return true;
    }
    if (event == Event::PageUp) {
      state->selected = std::max(0, state->selected - visible);
      clamp_scroll_to_selection(state.get(), visible);
      return true;
    }
    return false;
  };

  if (layout_state != nullptr) {
    layout_state->binary_symbols_key_handler = handler;
  }

  return WrapFocusable(CatchEvent(
      Renderer(input_layers, [state, layout_state, filter_input_option, focus] {
        const int visible = list_viewport_lines(*state);
        state->last_visible_lines = visible;
        clamp_scroll_viewport(state.get(), visible);

        const bool filter_active =
            layout_state != nullptr &&
            layout_state->text_input_focus == TextInputFocus::BinarySymbolsFilter;

        Element filter_field =
            render_name_filter_field(*state, filter_active, *filter_input_option);

        const std::string header_path = symbols_panel_binary_path(*state, layout_state);
        const std::string header_display =
            header_path.empty() ? "(sin binario)" : fs::path(header_path).filename().string();
        const bool show_loading = symbols_panel_loading(*state, layout_state);
        std::string loading_suffix;
        if (show_loading) {
          loading_suffix = "  [cargando… Esc: cancelar]";
        } else if (state->filtering) {
          loading_suffix = "  [filtrando…]";
        }
        std::string status_text = state->status;
        if (binary_symbols_request_pending(layout_state) && !header_display.empty() &&
            header_display != "(sin binario)") {
          status_text = "Analizando " + header_display + "...";
        }
        const std::string header = "Binario: " + header_display + "  |  " + status_text +
                                   loading_suffix;

        Elements binding_labels;
        binding_labels.push_back(text("Estado: ") | color(theme::Muted()));
        for (int i = 0; i < kBindingFilterCount; ++i) {
          const NmBindingFilter filter = kBindingFilterOrder[i];
          Element label = text(nm_binding_filter_label(filter));
          if (filter == state->binding_filter) {
            label = label | bold | color(theme::Accent()) | inverted;
          } else {
            label = label | color(theme::Muted());
          }
          binding_labels.push_back(label | reflect(state->binding_filter_boxes[static_cast<std::size_t>(i)]));
          if (i + 1 < kBindingFilterCount) {
            binding_labels.push_back(text(" "));
          }
        }
        binding_labels.push_back(
            text("  Enter: aplicar nombre  clic/Tab/1-7: estado  /: nombre") | color(theme::Muted()));

        Elements rows;
        if (show_loading && (state->symbols == nullptr || state->symbols->empty())) {
          rows.push_back(text(" Ejecutando nm en segundo plano... ") | color(theme::Muted()));
        } else if (state->filtering && state->filtered_indices.empty()) {
          rows.push_back(text(" Filtrando símbolos... ") | color(theme::Muted()));
        } else if (state->filtered_indices.empty()) {
          rows.push_back(text(" (sin símbolos) ") | color(theme::Muted()));
        } else {
          const int end = std::min(static_cast<int>(state->filtered_indices.size()),
                                   state->first_visible + visible);
          for (int i = state->first_visible; i < end; ++i) {
            const int symbol_index = state->filtered_indices[static_cast<std::size_t>(i)];
            rows.push_back(make_symbol_row((*state->symbols)[static_cast<std::size_t>(symbol_index)],
                                           i == state->selected));
          }
        }

        const int total = static_cast<int>(state->filtered_indices.size());
        state->scrollbar_layout =
            compute_scrollbar_layout(total, state->first_visible, visible, visible);
        const bool scrollbar_hovered =
            layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kEditorScrollbar);
        const bool scrollbar_active =
            state->scrollbar_dragging ||
            (layout_state != nullptr &&
             layout_state->clickable.is_pressed(press_id::kEditorScrollbar));
        Element scrollbar =
            vertical_scrollbar(total, state->first_visible, visible, visible, scrollbar_hovered,
                               scrollbar_active) |
            reflect(state->scrollbar_box);
        Element list_body = vbox(std::move(rows)) | flex;
        Element list_panel =
            hbox({std::move(list_body) | flex, std::move(scrollbar)}) | flex |
            reflect(state->list_content_box);

        return vbox({
                   text(header) | color(theme::Muted()) | size(HEIGHT, EQUAL, 1),
                   hbox({text("Nombre: ") | color(theme::Muted()), filter_field | flex}) |
                       size(HEIGHT, EQUAL, 1) | reflect(state->filter_box),
                   hbox(std::move(binding_labels)) | size(HEIGHT, EQUAL, 1),
                   separator(),
                   std::move(list_panel),
               }) |
               flex | bgcolor(theme::PanelBg());
      }),
      handler));
}

}  // namespace tgdb
