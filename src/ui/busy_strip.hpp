#pragma once

// Busy strip: franja fija en la status bar, fuera del dirty FTXUI.
// Indicador (Braille; + % si es cuantizable) + label por ANSI; 0 UI_WAKE por tick.

#include <atomic>
#include <cstddef>
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
  AiThinking,
  AiMapping,
  AiEmbedding,
  AiDownloading,
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
  // Irreversible: shutdown overlay is on screen; ANSI paints must stop.
  std::atomic<bool> halted{false};
  std::thread ticker;
};

int busy_strip_total_width(const BusyStripState& state);
ftxui::Element MakeBusyStripPlaceholder(BusyStripState* state);

void set_busy_spinner(MainLayoutState* layout, BusyActivity activity, std::string_view label = {});
void set_busy_percent(MainLayoutState* layout, BusyActivity activity, int percent,
                      std::string_view label = {});
void clear_busy(MainLayoutState* layout);
// Shutdown: wipe the strip (even Embedding/Downloading), stop ANSI paints. Irreversible.
void halt_busy_strip(MainLayoutState* layout);
// Only clears when the strip is currently showing `activity` (avoids wiping another job).
void clear_busy_if(MainLayoutState* layout, BusyActivity activity);
bool busy_activity_is_ai(BusyActivity activity);
// Updates Mapping % while the symbol/repo-map index scans. Does not override Pensando
// or unrelated busy activities (Indexing, git, …).
void refresh_ai_mapping_busy(MainLayoutState* layout, bool scanning, std::size_t done,
                             std::size_t total);
// Coding-symbol corpus embeddings (after Mapping). Percent when total>0.
void refresh_ai_embedding_busy(MainLayoutState* layout, bool active, std::size_t done,
                               std::size_t total);

void busy_strip_tick(BusyStripState* state, int64_t now_ms);
void busy_strip_paint_ansi(BusyStripState* state);
void busy_strip_reassert_after_draw(BusyStripState* state);

std::string_view busy_activity_i18n_key(BusyActivity activity);

}  // namespace tuide
