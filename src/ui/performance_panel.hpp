#pragma once

#include <memory>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "util/system_stats.hpp"
#include "util/ui_activity_gate.hpp"
#include "util/ui_perf_monitor.hpp"

namespace tgdb {

struct PerformancePanelState {
  int thread_scroll = 0;
};

ftxui::Element RenderPerformancePanel(PerformanceSampler* sampler, UiPerfMonitor* ui_perf,
                                        PerformancePanelState* state, int width, int height);
ftxui::Component MakePerformancePanel(PerformanceSampler* sampler, UiPerfMonitor* ui_perf,
                                        std::shared_ptr<PerformancePanelState> state);

}  // namespace tgdb
