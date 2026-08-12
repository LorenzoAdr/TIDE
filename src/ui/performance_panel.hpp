#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "util/system_stats.hpp"
#include "util/ui_activity_gate.hpp"
#include "ui/ui_event_trace.hpp"

namespace tuide {

struct MainLayoutState;

struct PerformancePanelState {
  int thread_scroll = 0;
  ftxui::Box panel_box;
  ftxui::Box refresh_box;
};

ftxui::Element RenderPerformancePanel(PerformanceSampler* sampler, UiPerfMonitor* ui_perf,
                                        PerformancePanelState* state, int width, int height,
                                        MainLayoutState* layout_state = nullptr,
                                        const UiEventTrace* ui_event_trace = nullptr,
                                        const std::atomic<uint64_t>* ui_paint_count = nullptr,
                                        const std::atomic<uint64_t>* ui_lsp_request_count = nullptr);
ftxui::Component MakePerformancePanel(PerformanceSampler* sampler, UiPerfMonitor* ui_perf,
                                        std::shared_ptr<PerformancePanelState> state,
                                        MainLayoutState* layout_state = nullptr);

}  // namespace tuide
