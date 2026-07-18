#include "packet_monitor/pkt_monitor_service.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "packet_monitor/pkt_store.hpp"

namespace tuide::packet_monitor {

namespace fs = std::filesystem;

namespace {

std::string format_timestamp(uint64_t ns) {
  const auto ms = static_cast<std::uint64_t>(ns / 1000000ull);
  const auto sec = ms / 1000ull;
  const auto frac = ms % 1000ull;
  std::ostringstream stream;
  stream << sec << '.' << std::setw(3) << std::setfill('0') << frac;
  return stream.str();
}

std::string format_hex_preview(const std::vector<uint8_t>& payload, std::size_t max_bytes) {
  std::ostringstream stream;
  const std::size_t count = std::min(payload.size(), max_bytes);
  for (std::size_t i = 0; i < count; ++i) {
    if (i > 0) {
      stream << ' ';
    }
    stream << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
           << static_cast<int>(payload[i]);
  }
  if (payload.size() > max_bytes) {
    stream << " ...";
  }
  return stream.str();
}

}  // namespace

void PacketMonitorService::reset() {
  state_.ring.close();
  state_.inferior_pid = 0;
  state_.capture_available = false;
  state_.recording = false;
  state_.live_packets.clear();
  state_.recorded_packets.clear();
  state_.connections.clear();
  state_.selected_packet = 0;
  state_.packet_scroll = 0;
  state_.status_message.clear();
}

void PacketMonitorService::set_inferior_pid(int pid) {
  if (state_.inferior_pid == pid) {
    return;
  }
  state_.inferior_pid = pid;
  state_.ring.close();
  state_.capture_available = false;
  if (pid > 0 && state_.enabled) {
    state_.capture_available = state_.ring.open_for_pid(pid);
    if (!state_.capture_available) {
      state_.status_message = "waiting for packet ring";
    }
  }
  refresh_connections();
}

void PacketMonitorService::set_workspace_root(const std::string& workspace_root) {
  workspace_root_ = workspace_root;
  reload_protocols();
}

void PacketMonitorService::reload_protocols() {
  protocol_files_ = list_protocol_files(workspace_root_);
  if (!state_.protocol_path.empty()) {
    load_selected_protocol();
    return;
  }
  if (!protocol_files_.empty()) {
    state_.protocol_path = protocol_files_.front();
    load_selected_protocol();
  }
}

bool PacketMonitorService::select_protocol_by_index(int index) {
  if (index < 0 || index >= static_cast<int>(protocol_files_.size())) {
    return false;
  }
  state_.protocol_path = protocol_files_[static_cast<std::size_t>(index)];
  load_selected_protocol();
  return true;
}

void PacketMonitorService::load_selected_protocol() {
  state_.protocol_loaded = false;
  state_.protocol_error.clear();
  if (state_.protocol_path.empty()) {
    return;
  }
  std::string error;
  if (!load_protocol_from_json(state_.protocol_path, &state_.protocol, &error)) {
    state_.protocol_error = error;
    return;
  }
  state_.protocol_loaded = true;
}

void PacketMonitorService::tick() {
  refresh_connections();
  if (!state_.enabled || state_.inferior_pid <= 0) {
    return;
  }
  if (!state_.ring.is_open()) {
    state_.capture_available = state_.ring.open_for_pid(state_.inferior_pid);
  }
  if (!state_.ring.is_open()) {
    return;
  }
  ingest_packets(state_.ring.poll_new_packets());
}

void PacketMonitorService::ingest_packets(std::vector<PacketRecord> packets) {
  const ProtocolDefinition* protocol =
      state_.protocol_loaded ? &state_.protocol : nullptr;
  for (auto& packet : packets) {
    if (!packet_matches_filters(packet, state_.filters, protocol)) {
      continue;
    }
    state_.live_packets.push_back(packet);
    if (state_.live_packets.size() > PacketMonitorState::kMaxLivePackets) {
      state_.live_packets.erase(state_.live_packets.begin());
      if (state_.selected_packet > 0) {
        --state_.selected_packet;
      }
      if (state_.packet_scroll > 0) {
        --state_.packet_scroll;
      }
    }
    if (state_.recording) {
      state_.recorded_packets.push_back(packet);
    }
  }
}

void PacketMonitorService::refresh_connections() {
  if (state_.inferior_pid <= 0) {
    state_.connections.clear();
    return;
  }
  state_.connections = list_udp_connections(state_.inferior_pid);
}

void PacketMonitorService::toggle_recording() {
  state_.recording = !state_.recording;
  if (state_.recording) {
    state_.recorded_packets.clear();
    state_.selected_packet = 0;
    state_.packet_scroll = 0;
    state_.status_message = "recording";
  } else {
    state_.status_message = "record stopped";
  }
}

void PacketMonitorService::stop_recording_and_save() {
  if (state_.recording) {
    state_.recording = false;
  }
  if (state_.recorded_packets.empty()) {
    state_.status_message = "no packets to save";
    return;
  }
  std::error_code ec;
  const std::string dir = default_capture_dir();
  fs::create_directories(dir, ec);
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
  const std::string path = dir + "/capture-" + std::to_string(stamp) + ".tgcap";
  CaptureFileHeader header;
  header.protocol_path = state_.protocol_path;
  header.filters = state_.filters;
  std::string error;
  if (!save_capture_file(path, header, state_.recorded_packets, &error)) {
    state_.status_message = error;
    return;
  }
  state_.status_message = "saved " + path;
}

void PacketMonitorService::select_packet(int index) {
  state_.selected_packet = std::max(0, index);
}

std::vector<std::string> PacketMonitorService::protocol_labels() const {
  std::vector<std::string> labels;
  labels.reserve(protocol_files_.size());
  for (const auto& path : protocol_files_) {
    labels.push_back(fs::path(path).filename().string());
  }
  return labels;
}

std::vector<DisplayPacket> PacketMonitorService::display_packets() const {
  const std::vector<PacketRecord>& source =
      state_.recording || !state_.recorded_packets.empty() ? state_.recorded_packets
                                                           : state_.live_packets;
  std::vector<DisplayPacket> rows;
  rows.reserve(source.size());
  for (const auto& record : source) {
    DisplayPacket row;
    row.record = record;
    std::ostringstream summary;
    summary << (record.direction == TUIDE_PKT_OUT ? "OUT" : "IN") << ' '
            << format_timestamp(record.timestamp_ns) << ' ' << record.payload.size() << "B "
            << format_ipv4(record.src_ipv4) << ':' << record.src_port << " -> "
            << format_ipv4(record.dst_ipv4) << ':' << record.dst_port << ' '
            << format_hex_preview(record.payload, 8);
    row.summary = summary.str();
    if (state_.protocol_loaded) {
      row.decoded = decode_packet(record.payload, state_.protocol, "");
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace tuide::packet_monitor
