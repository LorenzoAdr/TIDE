#include "ui/editor_grid_node.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "util/ui_perf_monitor.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

class EditorGridNode : public Node {
 public:
  EditorGridNode(std::vector<std::shared_ptr<EditorPixelRow>> rows, int width,
                UiPerfMonitor* ui_perf, std::string perf_phase)
      : rows_(std::move(rows)),
        width_(width),
        ui_perf_(ui_perf),
        perf_phase_(std::move(perf_phase)) {}

  void ComputeRequirement() override {
    requirement_ = Requirement{};
    requirement_.min_x = std::max(0, width_);
    requirement_.min_y = static_cast<int>(rows_.size());
  }

  void Render(Screen& screen) override {
    // Timed even when ui_perf_ is null (UiSyncPhaseScope no-ops in that case): this is
    // the direct replacement for the per-frame Element-tree walk-and-decorate cost that
    // used to be invisible, buried inside FTXUI's own (private) ScreenInteractive::Draw().
    UiSyncPhaseScope scope(ui_perf_, perf_phase_);
    const int row_count = static_cast<int>(rows_.size());
    for (int row = 0; row < row_count; ++row) {
      const int y = box_.y_min + row;
      if (y > box_.y_max) {
        break;
      }
      const EditorPixelRow* pixel_row = rows_[static_cast<std::size_t>(row)].get();
      if (pixel_row == nullptr) {
        continue;
      }
      const int cell_count = static_cast<int>(pixel_row->cells.size());
      for (int x = 0; x < cell_count; ++x) {
        const int screen_x = box_.x_min + x;
        if (screen_x > box_.x_max) {
          break;
        }
        screen.PixelAt(screen_x, y) = pixel_row->cells[static_cast<std::size_t>(x)];
      }
    }
  }

 private:
  std::vector<std::shared_ptr<EditorPixelRow>> rows_;
  int width_;
  UiPerfMonitor* ui_perf_;
  std::string perf_phase_;
};

}  // namespace

std::shared_ptr<EditorPixelRow> PixelRowFromElement(const Element& element, int width) {
  auto row = std::make_shared<EditorPixelRow>();
  if (width <= 0 || element == nullptr) {
    return row;
  }
  // A throwaway 1-row Screen used purely to capture the Pixels FTXUI's own layout and
  // decorator logic would have produced -- same rendering behavior, captured once instead
  // of replayed every frame. Cost is proportional to `width`, paid only on a cache miss.
  Screen scratch(width, 1);
  Render(scratch, element);
  row->cells.reserve(static_cast<std::size_t>(width));
  for (int x = 0; x < width; ++x) {
    row->cells.push_back(scratch.PixelAt(x, 0));
  }
  return row;
}

Element MakeEditorPixelGrid(std::vector<std::shared_ptr<EditorPixelRow>> rows, int width,
                            UiPerfMonitor* ui_perf, std::string perf_phase) {
  return std::make_shared<EditorGridNode>(std::move(rows), width, ui_perf, std::move(perf_phase));
}

}  // namespace tgdb
