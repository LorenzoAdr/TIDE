#include "ui/busy_strip.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

#include "i18n/tr.hpp"
#include "ui/main_layout.hpp"
#include "ui/theme.hpp"

namespace tuide {
namespace {

constexpr int kSpinnerFrameMs = 100;

const char* kBrailleFrames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
constexpr int kBrailleFrameCount = 10;

int64_t steady_now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string truncate_pad(std::string_view text, int width) {
  if (width <= 0) {
    return {};
  }
  std::string out;
  int cells = 0;
  for (std::size_t i = 0; i < text.size() && cells < width;) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    std::size_t len = 1;
    if ((c & 0x80) == 0) {
      len = 1;
    } else if ((c & 0xE0) == 0xC0) {
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
    }
    if (i + len > text.size() || cells + 1 > width) {
      break;
    }
    out.append(text.substr(i, len));
    i += len;
    ++cells;
  }
  if (cells == width && !text.empty() && out.size() < text.size()) {
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
      out.pop_back();
    }
    if (!out.empty()) {
      out.pop_back();
    }
    out.push_back('.');
  }
  while (cells < width) {
    out.push_back(' ');
    ++cells;
  }
  return out;
}

std::string format_indicator(const BusyStripState& state) {
  const char* frame = kBrailleFrames[state.spinner_frame % kBrailleFrameCount];
  if (state.kind == BusyIndicatorKind::Spinner) {
    // Solo braille (tarea no cuantizable), relleno a ancho fijo.
    std::string display;
    display.append(frame);
    for (int i = 1; i < BusyStripState::kIndicatorWidth; ++i) {
      display.push_back(' ');
    }
    return display;
  }
  if (state.kind == BusyIndicatorKind::Percent) {
    // Braille + espacio + porcentaje ("⠋  42%" / "⠋ 100%").
    const int pct = std::clamp(state.percent, 0, 100);
    std::string num = std::to_string(pct) + "%";
    constexpr int kPercentCols = 4;  // "100%"
    if (static_cast<int>(num.size()) > kPercentCols) {
      num = num.substr(num.size() - static_cast<std::size_t>(kPercentCols));
    }
    std::string display;
    display.append(frame);
    display.push_back(' ');
    display.append(static_cast<std::size_t>(kPercentCols - static_cast<int>(num.size())), ' ');
    display.append(num);
    return display;
  }
  return std::string(BusyStripState::kIndicatorWidth, ' ');
}

std::string format_strip_line(const BusyStripState& state) {
  return format_indicator(state) + " " + truncate_pad(state.label, state.label_width);
}

void paint_ansi_unlocked(BusyStripState* state) {
  if (state == nullptr) {
    return;
  }
  // reflect() rellena box durante el Render a Screen; vale tras el primer Draw.
  if (state->box.x_max < state->box.x_min || state->box.y_max < state->box.y_min) {
    return;
  }
  state->box_valid = true;
  const int row = state->box.y_min + 1;
  const int col = state->box.x_min + 1;
  const std::string line = format_strip_line(*state);
  // Mismo color que el texto de foco a la izquierda (Header), no Muted de botones.
  const std::string fg = theme::Header().Print(/*is_background_color=*/false);
  const std::string bg = theme::StatusBar().Print(/*is_background_color=*/true);
  std::ostringstream oss;
  oss << "\0337"
      << "\033[" << row << ";" << col << "H"
      << "\033[" << fg << ";" << bg << "m" << line << "\033[0m"
      << "\0338";
  std::cout << oss.str() << std::flush;
}

void ensure_spinner_thread(BusyStripState* state) {
  if (state == nullptr || state->ticker_running.load(std::memory_order_acquire)) {
    return;
  }
  bool expected = false;
  if (!state->ticker_running.compare_exchange_strong(expected, true)) {
    return;
  }
  state->ticker = std::thread([state] {
    while (state->ticker_running.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kSpinnerFrameMs));
      std::lock_guard<std::mutex> lock(state->paint_mutex);
      if (state->kind != BusyIndicatorKind::Spinner &&
          state->kind != BusyIndicatorKind::Percent) {
        continue;
      }
      state->spinner_frame = (state->spinner_frame + 1) % kBrailleFrameCount;
      state->last_spinner_ms = steady_now_ms();
      paint_ansi_unlocked(state);
    }
  });
}

void stop_spinner_thread(BusyStripState* state) {
  if (state == nullptr) {
    return;
  }
  state->ticker_running.store(false, std::memory_order_release);
  if (state->ticker.joinable()) {
    state->ticker.join();
  }
}

}  // namespace

BusyStripState::BusyStripState() = default;

BusyStripState::~BusyStripState() {
  stop_spinner_thread(this);
}

int busy_strip_total_width(const BusyStripState& state) {
  return BusyStripState::kIndicatorWidth + 1 + state.label_width;
}

ftxui::Element MakeBusyStripPlaceholder(BusyStripState* state) {
  using namespace ftxui;
  const int width = state != nullptr ? busy_strip_total_width(*state)
                                     : BusyStripState::kIndicatorWidth + 1 +
                                           BusyStripState::kDefaultLabelWidth;
  std::string spaces(static_cast<std::size_t>(std::max(1, width)), ' ');
  Element el = text(spaces);
  if (state != nullptr) {
    el = std::move(el) | reflect(state->box);
  }
  return el;
}

std::string_view busy_activity_i18n_key(BusyActivity activity) {
  switch (activity) {
    case BusyActivity::Idle:
      return "busy.idle";
    case BusyActivity::Indexing:
      return "busy.indexing";
    case BusyActivity::CallHierarchy:
      return "busy.call_hierarchy";
    case BusyActivity::FindReferences:
      return "busy.find_references";
    case BusyActivity::ProjectSearch:
      return "busy.project_search";
    case BusyActivity::GitPush:
      return "busy.git_push";
    case BusyActivity::GitPull:
      return "busy.git_pull";
    case BusyActivity::OutlinePending:
      return "busy.outline_pending";
    case BusyActivity::ToolpackInstall:
      return "busy.toolpack_install";
    case BusyActivity::ExportPortable:
      return "busy.export_portable";
    case BusyActivity::AiThinking:
      return "busy.ai_thinking";
    case BusyActivity::AiMapping:
      return "busy.ai_mapping";
    case BusyActivity::AiEmbedding:
      return "busy.ai_embedding";
  }
  return "busy.idle";
}

bool busy_activity_is_ai(BusyActivity activity) {
  return activity == BusyActivity::AiThinking || activity == BusyActivity::AiMapping ||
         activity == BusyActivity::AiEmbedding;
}

void set_busy_spinner(MainLayoutState* layout, BusyActivity activity, std::string_view label) {
  if (layout == nullptr || layout->busy_strip == nullptr) {
    return;
  }
  BusyStripState& state = *layout->busy_strip;
  {
    std::lock_guard<std::mutex> lock(state.paint_mutex);
    const BusyActivity previous = state.activity;
    state.kind = BusyIndicatorKind::Spinner;
    state.activity = activity;
    state.percent = 0;
    if (!label.empty()) {
      state.label = std::string(label);
    } else if (state.label.empty() || previous != activity) {
      state.label = i18n::tr(busy_activity_i18n_key(activity));
    }
    state.spinner_frame = 0;
    state.last_spinner_ms = steady_now_ms();
    paint_ansi_unlocked(&state);
  }
  ensure_spinner_thread(&state);
}

void set_busy_percent(MainLayoutState* layout, BusyActivity activity, int percent,
                      std::string_view label) {
  if (layout == nullptr || layout->busy_strip == nullptr) {
    return;
  }
  BusyStripState& state = *layout->busy_strip;
  {
    std::lock_guard<std::mutex> lock(state.paint_mutex);
    const bool was_active = state.kind == BusyIndicatorKind::Spinner ||
                            state.kind == BusyIndicatorKind::Percent;
    const BusyActivity previous = state.activity;
    state.kind = BusyIndicatorKind::Percent;
    state.activity = activity;
    state.percent = std::clamp(percent, 0, 100);
    if (!label.empty()) {
      state.label = std::string(label);
    } else if (state.label.empty() || previous != activity) {
      state.label = i18n::tr(busy_activity_i18n_key(activity));
    }
    if (!was_active) {
      state.spinner_frame = 0;
      state.last_spinner_ms = steady_now_ms();
    }
    paint_ansi_unlocked(&state);
  }
  ensure_spinner_thread(&state);
}

void clear_busy(MainLayoutState* layout) {
  if (layout == nullptr || layout->busy_strip == nullptr) {
    return;
  }
  BusyStripState& state = *layout->busy_strip;
  {
    std::lock_guard<std::mutex> lock(state.paint_mutex);
    // Coding-symbol embedding owns the strip until it finishes; generic clear_busy
    // from Indexing/outline/etc. must not wipe the % between embed chunks.
    if (state.activity == BusyActivity::AiEmbedding) {
      return;
    }
    state.kind = BusyIndicatorKind::None;
    state.activity = BusyActivity::Idle;
    state.percent = 0;
    state.label.clear();
    state.spinner_frame = 0;
    paint_ansi_unlocked(&state);
  }
  // Dejar el hilo vivo pero idle (kind==None); evita join en hot path.
}

void clear_busy_if(MainLayoutState* layout, BusyActivity activity) {
  if (layout == nullptr || layout->busy_strip == nullptr) {
    return;
  }
  BusyActivity current = BusyActivity::Idle;
  {
    std::lock_guard<std::mutex> lock(layout->busy_strip->paint_mutex);
    current = layout->busy_strip->activity;
  }
  if (current == activity) {
    clear_busy(layout);
  }
}

void refresh_ai_mapping_busy(MainLayoutState* layout, bool scanning, std::size_t done,
                             std::size_t total) {
  if (layout == nullptr || layout->busy_strip == nullptr) {
    return;
  }
  BusyActivity current = BusyActivity::Idle;
  {
    std::lock_guard<std::mutex> lock(layout->busy_strip->paint_mutex);
    current = layout->busy_strip->activity;
  }
  // Pensando has priority; Mapping may replace Indexing (more specific progress).
  if (current == BusyActivity::AiThinking) {
    return;
  }
  if (current != BusyActivity::Idle && current != BusyActivity::AiMapping &&
      current != BusyActivity::Indexing) {
    return;
  }
  if (!scanning) {
    clear_busy_if(layout, BusyActivity::AiMapping);
    return;
  }
  // Waiting for the workspace file list (total still unknown) → spinner "Mapping".
  if (total == 0) {
    set_busy_spinner(layout, BusyActivity::AiMapping);
    return;
  }
  int pct = static_cast<int>((done * 100) / total);
  if (pct > 100) {
    pct = 100;
  }
  set_busy_percent(layout, BusyActivity::AiMapping, pct);
}

void refresh_ai_embedding_busy(MainLayoutState* layout, bool active, std::size_t done,
                               std::size_t total) {
  if (layout == nullptr || layout->busy_strip == nullptr) {
    return;
  }
  BusyActivity current = BusyActivity::Idle;
  {
    std::lock_guard<std::mutex> lock(layout->busy_strip->paint_mutex);
    current = layout->busy_strip->activity;
  }
  // Pensando has priority; embedding may replace Mapping / Indexing / itself.
  if (current == BusyActivity::AiThinking) {
    return;
  }
  if (!active) {
    clear_busy_if(layout, BusyActivity::AiEmbedding);
    return;
  }
  // Always take the strip while embedding is active (Indexing/outline clear_busy
  // would otherwise leave it blank until the next embed chunk).
  if (current != BusyActivity::Idle && current != BusyActivity::AiEmbedding &&
      current != BusyActivity::AiMapping && current != BusyActivity::Indexing &&
      current != BusyActivity::OutlinePending) {
    return;
  }
  if (total == 0) {
    set_busy_spinner(layout, BusyActivity::AiEmbedding);
    return;
  }
  int pct = static_cast<int>((done * 100) / total);
  if (pct > 100) {
    pct = 100;
  }
  set_busy_percent(layout, BusyActivity::AiEmbedding, pct);
}

void busy_strip_tick(BusyStripState* state, int64_t now_ms) {
  (void)now_ms;
  if (state == nullptr) {
    return;
  }
  // Spinner avanzado por hilo dedicado; tick no-op público por compat.
}

void busy_strip_paint_ansi(BusyStripState* state) {
  if (state == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(state->paint_mutex);
  paint_ansi_unlocked(state);
}

void busy_strip_reassert_after_draw(BusyStripState* state) {
  if (state == nullptr) {
    return;
  }
  state->reassert_posted.store(false, std::memory_order_release);
  std::lock_guard<std::mutex> lock(state->paint_mutex);
  paint_ansi_unlocked(state);
}

}  // namespace tuide
