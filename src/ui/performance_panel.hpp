#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "util/system_stats.hpp"
#include "util/ui_activity_gate.hpp"
#include "ui/ui_event_trace.hpp"

namespace tgdb {

struct PerformancePanelState {
  int thread_scroll = 0;
};

ftxui::Element RenderPerformancePanel(PerformanceSampler* sampler, UiPerfMonitor* ui_perf,
                                        PerformancePanelState* state, int width, int height,
                                        const UiEventTrace* ui_event_trace = nullptr,
                                        const std::atomic<uint64_t>* ui_paint_count = nullptr,
                                        const std::atomic<uint64_t>* ui_lsp_request_count = nullptr);
ftxui::Component MakePerformancePanel(PerformanceSampler* sampler, UiPerfMonitor* ui_perf,
                                        std::shared_ptr<PerformancePanelState> state);

}  // namespace tgdb
