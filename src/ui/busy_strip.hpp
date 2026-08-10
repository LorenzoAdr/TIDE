#pragma once

// Busy strip: franja fija en la status bar, fuera del dirty FTXUI.
// Indicador (Braille; + % si es cuantizable) + label por ANSI; 0 UI_WAKE por tick.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "ftxui/dom/elements.hpp"

namespace tuide {

struct MainLayoutState;

enum class BusyIndicatorKind : std::uint8_t { None, Spinner, Percent };

enum class BusyActivity : std::uint8_t {
  Idle = 0,
  Indexing,
  CallHierarchy,
  FindReferences,
  ProjectSearch,
  GitPush,
  GitPull,
  OutlinePending,
  ToolpackInstall,
  ExportPortable,
};

struct BusyStripState {
  // Spinner alone, or braille + space + "100%" when percent is shown.
  static constexpr int kIndicatorWidth = 6;
  static constexpr int kDefaultLabelWidth = 28;

  BusyStripState();
  ~BusyStripState();
  BusyStripState(const BusyStripState&) = delete;
  BusyStripState& operator=(const BusyStripState&) = delete;

  BusyIndicatorKind kind = BusyIndicatorKind::None;
  BusyActivity activity = BusyActivity::Idle;
  int percent = 0;
  std::string label;
  int label_width = kDefaultLabelWidth;
  ftxui::Box box;
  bool box_valid = false;
  int spinner_frame = 0;
  int64_t last_spinner_ms = 0;
  std::mutex paint_mutex;
  std::atomic<bool> ticker_running{false};
  std::atomic<bool> reassert_posted{false};
  std::thread ticker;
};

int busy_strip_total_width(const BusyStripState& state);
ftxui::Element MakeBusyStripPlaceholder(BusyStripState* state);

void set_busy_spinner(MainLayoutState* layout, BusyActivity activity, std::string_view label = {});
void set_busy_percent(MainLayoutState* layout, BusyActivity activity, int percent,
                      std::string_view label = {});
void clear_busy(MainLayoutState* layout);

void busy_strip_tick(BusyStripState* state, int64_t now_ms);
void busy_strip_paint_ansi(BusyStripState* state);
void busy_strip_reassert_after_draw(BusyStripState* state);

std::string_view busy_activity_i18n_key(BusyActivity activity);

}  // namespace tuide
