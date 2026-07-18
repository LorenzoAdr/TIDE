#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/pixel.hpp"

namespace tuide {

class UiPerfMonitor;

// One rasterized, single-height row of terminal cells. Produced once from an
// ftxui::Element (via PixelRowFromElement) and then reused across frames by
// EditorGridNode without re-walking/re-decorating the Element tree, which is
// what FTXUI would otherwise do on *every* Draw() call even for a cached,
// unchanged Element -- ScreenInteractive::Draw() always re-runs Render() over
// the whole document, there's no memoization of Node::Render results across
// frames inside FTXUI itself.
struct EditorPixelRow {
  std::vector<ftxui::Pixel> cells;
};

// Renders `element` once into an off-screen scratch Screen of the given
// width (height 1) and captures the resulting Pixels. This reuses FTXUI's
// own layout/decorator/highlighting logic as-is (so colors, tab expansion,
// unicode grapheme handling, etc. all stay byte-for-byte identical to
// building the Element the old way) -- it's just captured once instead of
// being replayed by FTXUI on every subsequent frame.
std::shared_ptr<EditorPixelRow> PixelRowFromElement(const ftxui::Element& element, int width);

// Builds an ftxui::Element backed by a leaf Node that paints pre-rasterized
// rows directly into the Screen's pixel matrix (via Screen::PixelAt) during
// its own Render(Screen&), instead of holding a subtree of Element/Node
// objects (Text/hbox/Decorator...) that FTXUI has to walk and re-apply
// decorators to on every single Draw() call.
//
// `ui_perf`/`perf_phase` are optional (pass nullptr/"" to disable) and let
// the caller measure exactly how much this direct-write pass costs, via the
// same UiPerfMonitor phase log used elsewhere in the editor panel.
// `width` is the rasterized row width (cells captured per line). `layout_min_x` is the
// horizontal space the hbox must reserve: fixed for the gutter (line numbers), 0 for the
// flex-growing code column so the scrollbar is not clipped by an outer frame.
ftxui::Element MakeEditorPixelGrid(std::vector<std::shared_ptr<EditorPixelRow>> rows, int width,
                                   int layout_min_x, UiPerfMonitor* ui_perf,
                                   std::string perf_phase);

}  // namespace tuide
