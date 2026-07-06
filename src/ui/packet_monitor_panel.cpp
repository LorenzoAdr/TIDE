#include "ui/packet_monitor_panel.hpp"

#include <algorithm>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "i18n/tr.hpp"
#include "ui/clickable.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

std::string variant_options(const packet_monitor::PacketMonitorState& state) {
  if (!state.protocol_loaded) {
    return "*";
  }
  if (!state.protocol.has_discriminator) {
    return "*";
  }
  std::string result = "*";
  for (const auto& [key, variant] : state.protocol.variants) {
    result += "," + key + "=" + variant.name;
  }
  return result;
}

bool handle_packet_monitor_hover(PacketMonitorPanelState* panel_state,
                                 MainLayoutState* layout_state, const Mouse& mouse) {
  if (panel_state == nullptr || layout_state == nullptr || mouse.motion != Mouse::Moved) {
    return false;
  }
  return update_panel_hover(
      layout_state, mouse.x, mouse.y,
      {{press_id::kPacketMonitorRecord, &panel_state->record_box},
       {press_id::kPacketMonitorSave, &panel_state->save_box}},
      press_id::is_packet_monitor_hover);
}

}  // namespace

bool handle_packet_monitor_keys(const Event& event, packet_monitor::PacketMonitorService* service,
                                PacketMonitorPanelState* state, MainLayoutState* layout_state) {
  if (service == nullptr || state == nullptr || layout_state == nullptr) {
    return false;
  }
  auto& monitor = service->state();
  auto& pkt_state = monitor;
  if (event == Event::Character('r')) {
    service->toggle_recording();
    layout_state->request_ui_tick = true;
    return true;
  }
  if (event == Event::Character('s')) {
    service->stop_recording_and_save();
    layout_state->request_ui_tick = true;
    return true;
  }
  if (event == Event::Character('p')) {
    const auto labels = service->protocol_labels();
    if (!labels.empty()) {
      state->protocol_selected =
          (state->protocol_selected + 1) % static_cast<int>(labels.size());
      service->select_protocol_by_index(state->protocol_selected);
      layout_state->request_ui_tick = true;
    }
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    service->select_packet(pkt_state.selected_packet - 1);
    pkt_state.packet_scroll = std::max(0, pkt_state.selected_packet - 2);
    layout_state->request_ui_tick = true;
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    service->select_packet(pkt_state.selected_packet + 1);
    pkt_state.packet_scroll = std::max(0, pkt_state.selected_packet - 2);
    layout_state->request_ui_tick = true;
    return true;
  }
  if (event == Event::Character('v')) {
    const auto labels = service->protocol_labels();
    if (!labels.empty() && monitor.protocol_loaded && monitor.protocol.has_discriminator) {
      static const std::vector<std::string> keys = {"", "0x01", "0x02", "0x03"};
      static int variant_idx = 0;
      variant_idx = (variant_idx + 1) % static_cast<int>(keys.size());
      pkt_state.filters.variant_key = keys[static_cast<std::size_t>(variant_idx)];
      layout_state->request_ui_tick = true;
    }
    return true;
  }
  return false;
}

bool handle_packet_monitor_mouse(const ftxui::Event& event,
                                 packet_monitor::PacketMonitorService* service,
                                 PacketMonitorPanelState* panel_state,
                                 MainLayoutState* layout_state, FocusManagerState* focus) {
  if (service == nullptr || panel_state == nullptr || layout_state == nullptr || !event.is_mouse()) {
    return false;
  }

  Event mutable_event = event;
  const Mouse& mouse = mutable_event.mouse();
  if (mouse.motion == Mouse::Moved) {
    return handle_packet_monitor_hover(panel_state, layout_state, mouse);
  }

  if (mouse.motion != Mouse::Pressed || mouse.button != Mouse::Left) {
    return false;
  }

  if (focus != nullptr) {
    focus->region = FocusRegion::Terminal;
  }
  if (layout_state->text_input_focus == TextInputFocus::Console) {
    layout_state->text_input_focus = TextInputFocus::None;
  }

  if (panel_state->record_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kPacketMonitorRecord);
    service->toggle_recording();
    layout_state->request_ui_tick = true;
    return true;
  }
  if (panel_state->save_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kPacketMonitorSave);
    service->stop_recording_and_save();
    layout_state->request_ui_tick = true;
    return true;
  }

  return false;
}

Component MakePacketMonitorPanel(packet_monitor::PacketMonitorService* service,
                                 std::shared_ptr<PacketMonitorPanelState> state,
                                 MainLayoutState* layout_state) {
  return Renderer([service, state, layout_state] {
    if (service == nullptr) {
      return text(i18n::tr("packet_monitor.unavailable"));
    }
    auto& monitor = service->state();
    Elements left;
    Elements right;

    left.push_back(text(i18n::tr("packet_monitor.section.connections")) | bold);
    if (monitor.inferior_pid <= 0) {
      left.push_back(text(i18n::tr("packet_monitor.no_pid")) | color(theme::Muted()));
    } else if (monitor.connections.empty()) {
      left.push_back(text(i18n::tr("packet_monitor.no_connections")) | color(theme::Muted()));
    } else {
      const int start = std::clamp(monitor.connection_scroll, 0,
                                   std::max(0, static_cast<int>(monitor.connections.size()) - 1));
      const int end = std::min(static_cast<int>(monitor.connections.size()), start + 6);
      for (int i = start; i < end; ++i) {
        const auto& conn = monitor.connections[static_cast<std::size_t>(i)];
        left.push_back(text(conn.local_address + " <-> " + conn.remote_address + " [" +
                          conn.state + "]"));
      }
    }

    left.push_back(separator());
    left.push_back(text(i18n::tr("packet_monitor.section.filters")) | bold);
    left.push_back(text(i18n::tr_fmt("packet_monitor.filter.src",
                                     {monitor.filters.src_ip.empty() ? "*"
                                                                     : monitor.filters.src_ip})));
    left.push_back(text(i18n::tr_fmt("packet_monitor.filter.dst",
                                     {monitor.filters.dst_ip.empty() ? "*"
                                                                     : monitor.filters.dst_ip})));
    left.push_back(text(i18n::tr_fmt("packet_monitor.filter.size",
                                     {std::to_string(monitor.filters.min_size),
                                      std::to_string(monitor.filters.max_size)})));
    left.push_back(text(i18n::tr_fmt("packet_monitor.filter.variant",
                                     {monitor.filters.variant_key.empty()
                                          ? "*"
                                          : monitor.filters.variant_key})));

    const auto labels = service->protocol_labels();
    std::string protocol_label = i18n::tr("packet_monitor.no_protocol");
    if (!monitor.protocol_path.empty()) {
      protocol_label = monitor.protocol_path.substr(monitor.protocol_path.find_last_of('/') + 1);
    }
    left.push_back(text(i18n::tr_fmt("packet_monitor.protocol", {protocol_label})));

    left.push_back(separator());
    const bool record_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kPacketMonitorRecord);
    const bool record_pressed =
        layout_state != nullptr &&
        layout_state->clickable.is_pressed(press_id::kPacketMonitorRecord);
    const bool save_hovered =
        layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kPacketMonitorSave);
    const bool save_pressed =
        layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kPacketMonitorSave);

    const std::string record_label =
        monitor.recording ? i18n::tr("packet_monitor.btn.recording")
                        : i18n::tr("packet_monitor.btn.record");
    Element record_btn = MakeToolbarButton(
        text(record_label), record_hovered, record_pressed, false,
        state != nullptr ? &state->record_box : nullptr);
    Element save_btn = MakeToolbarButton(text(i18n::tr("packet_monitor.btn.save")), save_hovered,
                                         save_pressed, false,
                                         state != nullptr ? &state->save_box : nullptr);

    const std::size_t packet_count =
        monitor.recording ? monitor.recorded_packets.size() : monitor.live_packets.size();
    left.push_back(hbox({
        record_btn,
        text(" "),
        save_btn,
        text("  "),
        text(std::to_string(packet_count) + " pkts") | color(theme::Muted()),
    }));
    if (!monitor.status_message.empty()) {
      left.push_back(text(monitor.status_message) | color(theme::Muted()));
    }

    if (!labels.empty()) {
      left.push_back(separator());
      left.push_back(text(i18n::tr_fmt("packet_monitor.protocols_available",
                                       {std::to_string(labels.size())})) |
                     color(theme::Muted()));
      left.push_back(text(variant_options(monitor)) | color(theme::Muted()));
    }

    const auto rows = service->display_packets();

    right.push_back(text(i18n::tr("packet_monitor.section.packets")) | bold);
    Elements packet_lines;
    if (rows.empty()) {
      packet_lines.push_back(text(i18n::tr("packet_monitor.no_packets")) | color(theme::Muted()));
    } else {
      const int selected = std::clamp(monitor.selected_packet, 0, static_cast<int>(rows.size()) - 1);
      const int start = std::clamp(monitor.packet_scroll,  0, std::max(0, static_cast<int>(rows.size()) - 1));
      const int end = std::min(static_cast<int>(rows.size()), start + 12);
      for (int i = start; i < end; ++i) {
        const bool active = i == selected;
        auto line = text((active ? "> " : "  ") + rows[static_cast<std::size_t>(i)].summary);
        if (active) {
          line |= color(theme::Accent()) | bold;
        }
        packet_lines.push_back(line);
      }
    }
    Element packet_list = vbox(std::move(packet_lines)) | flex | vscroll_indicator | frame;

    right.push_back(packet_list);
    right.push_back(separator());
    right.push_back(text(i18n::tr("packet_monitor.section.inspect")) | bold);

    if (rows.empty()) {
      right.push_back(text(i18n::tr("packet_monitor.no_packet_selected")) | color(theme::Muted()));
    } else {
      const int selected = std::clamp(monitor.selected_packet, 0, static_cast<int>(rows.size()) - 1);
      const auto& decoded = rows[static_cast<std::size_t>(selected)].decoded;
      if (!monitor.protocol_loaded) {
        right.push_back(text(i18n::tr("packet_monitor.select_protocol")) | color(theme::Muted()));
      } else if (!decoded.error.empty()) {
        right.push_back(text(decoded.error) | color(theme::Error()));
      } else {
        if (!decoded.variant_name.empty()) {
          right.push_back(text(i18n::tr_fmt("packet_monitor.variant", {decoded.variant_name})));
        }
        if (decoded.fields.empty()) {
          right.push_back(text(i18n::tr("packet_monitor.no_decoded_fields")) | color(theme::Muted()));
        } else {
          for (const auto& field : decoded.fields) {
            right.push_back(text("  " + field.name + " = " + field.value));
          }
        }
      }
    }

    Element left_col = vbox(std::move(left)) | flex | vscroll_indicator;
    Element right_col = vbox(std::move(right)) | flex;
    return hbox({
               left_col,
               separator() | color(theme::AccentDim()),
               right_col,
           }) |
           flex;
  });
}

}  // namespace tgdb
