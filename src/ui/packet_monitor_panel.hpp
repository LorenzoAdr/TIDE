#pragma once

#include <memory>

#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/screen/box.hpp"
#include "packet_monitor/pkt_monitor_service.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"

namespace tgdb {

struct PacketMonitorPanelState {
  int protocol_selected = 0;
  ftxui::Box record_box;
  ftxui::Box save_box;
};

bool handle_packet_monitor_keys(const ftxui::Event& event,
                                packet_monitor::PacketMonitorService* service,
                                PacketMonitorPanelState* state, MainLayoutState* layout_state);

bool handle_packet_monitor_mouse(const ftxui::Event& event,
                                 packet_monitor::PacketMonitorService* service,
                                 PacketMonitorPanelState* state, MainLayoutState* layout_state,
                                 FocusManagerState* focus);

ftxui::Component MakePacketMonitorPanel(packet_monitor::PacketMonitorService* service,
                                         std::shared_ptr<PacketMonitorPanelState> state,
                                         MainLayoutState* layout_state);

}  // namespace tgdb
