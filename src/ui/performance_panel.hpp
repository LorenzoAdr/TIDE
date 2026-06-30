#pragma once

#include <memory>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "util/system_stats.hpp"

namespace tgdb {

struct PerformancePanelState {
  int thread_scroll = 0;
};

ftxui::Element RenderPerformancePanel(PerformanceSampler* sampler, PerformancePanelState* state,
                                        int width, int height);
ftxui::Component MakePerformancePanel(PerformanceSampler* sampler,
                                        std::shared_ptr<PerformancePanelState> state);

}  // namespace tgdb
