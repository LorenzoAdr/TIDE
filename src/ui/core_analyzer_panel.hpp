#pragma once

#include <functional>
#include <memory>

#include "app/debug_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"

namespace tuide {

using CommandCallback = std::function<void(const struct UiCommand&)>;

ftxui::Component MakeCoreAnalyzerPanel(DebugModel* model, CommandCallback on_command,
                                       MainLayoutState* layout_state,
                                       FocusManagerState* focus);

void submit_core_analyzer_command(const std::string& line, DebugModel* model,
                                  CommandCallback on_command);

void submit_core_analyzer_class_search(const std::string& type_query, DebugModel* model,
                                       CommandCallback on_command);

void apply_core_analyzer_search_result(DebugModel* model, const std::string& output,
                                       const std::string& type_query);

void add_watch_for_core_instance(const CoreAnalyzerInstance& instance,
                                 const std::string& type_query, DebugModel* model,
                                 CommandCallback on_command);

}  // namespace tuide
