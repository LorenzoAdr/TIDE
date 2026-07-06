#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pkt_shm.h"

namespace tgdb::packet_monitor {

struct PacketRecord {
  uint64_t timestamp_ns = 0;
  uint8_t direction = TGDB_PKT_IN;
  uint16_t src_port = 0;
  uint16_t dst_port = 0;
  uint32_t src_ipv4 = 0;
  uint32_t dst_ipv4 = 0;
  std::vector<uint8_t> payload;
};

struct CaptureFilters {
  std::string src_ip;
  std::string dst_ip;
  std::size_t min_size = 0;
  std::size_t max_size = TGDB_PKT_MAX_PAYLOAD;
  std::string variant_key;  // empty = any
};

class PacketRingReader {
 public:
  bool open_for_pid(int pid);
  void close();
  bool is_open() const { return header_ != nullptr; }
  int pid() const { return pid_; }

  std::vector<PacketRecord> poll_new_packets();

 private:
  int pid_ = 0;
  std::string path_;
  void* mapped_ = nullptr;
  std::size_t mapped_size_ = 0;
  TgdbPktHeader* header_ = nullptr;
  TgdbPktEntry* entries_ = nullptr;
  uint32_t read_idx_ = 0;
};

std::string format_ipv4(uint32_t addr);
bool parse_ipv4(const std::string& text, uint32_t* out);
std::string shm_path_for_pid(int pid);
std::string runtime_dir_path();

bool packet_matches_filters(const PacketRecord& packet, const CaptureFilters& filters,
                            const class ProtocolDefinition* protocol);

}  // namespace tgdb::packet_monitor
