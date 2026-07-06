#pragma once

#include <string>
#include <vector>

#include "packet_monitor/pkt_connections.hpp"
#include "packet_monitor/pkt_protocol.hpp"
#include "packet_monitor/pkt_ring.hpp"

namespace tgdb::packet_monitor {

struct DisplayPacket {
  PacketRecord record;
  std::string summary;
  DecodedPacket decoded;
};

struct PacketMonitorState {
  bool enabled = false;
  bool capture_available = false;
  int inferior_pid = 0;
  bool recording = false;
  int selected_packet = 0;
  int packet_scroll = 0;
  int connection_scroll = 0;

  CaptureFilters filters;
  std::string protocol_path;
  ProtocolDefinition protocol;
  bool protocol_loaded = false;
  std::string protocol_error;

  PacketRingReader ring;
  std::vector<PacketRecord> live_packets;
  std::vector<PacketRecord> recorded_packets;
  std::vector<UdpConnectionEntry> connections;
  std::string status_message;

  static constexpr std::size_t kMaxLivePackets = 200;
};

class PacketMonitorService {
 public:
  void reset();
  void set_enabled(bool enabled) { state_.enabled = enabled; }
  void set_inferior_pid(int pid);
  void set_workspace_root(const std::string& workspace_root);
  void tick();

  void toggle_recording();
  void stop_recording_and_save();
  void reload_protocols();
  bool select_protocol_by_index(int index);
  void select_packet(int index);

  PacketMonitorState& state() { return state_; }
  const PacketMonitorState& state() const { return state_; }
  std::vector<DisplayPacket> display_packets() const;
  std::vector<std::string> protocol_labels() const;

 private:
  void ingest_packets(std::vector<PacketRecord> packets);
  void refresh_connections();
  void load_selected_protocol();

  PacketMonitorState state_;
  std::string workspace_root_;
  std::vector<std::string> protocol_files_;
};

}  // namespace tgdb::packet_monitor
