#pragma once

#include <string>
#include <vector>

#include "packet_monitor/pkt_ring.hpp"

namespace tgdb::packet_monitor {

struct CaptureFileHeader {
  std::string protocol_path;
  CaptureFilters filters;
};

bool save_capture_file(const std::string& path, const CaptureFileHeader& header,
                       const std::vector<PacketRecord>& packets, std::string* error = nullptr);

bool load_capture_file(const std::string& path, CaptureFileHeader* header,
                       std::vector<PacketRecord>* packets, std::string* error = nullptr);

std::string default_capture_dir();

}  // namespace tgdb::packet_monitor
