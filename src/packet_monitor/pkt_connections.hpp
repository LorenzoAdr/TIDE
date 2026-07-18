#pragma once

#include <string>
#include <vector>

namespace tuide::packet_monitor {

struct UdpConnectionEntry {
  std::string local_address;
  std::string remote_address;
  std::string state;
};

std::vector<UdpConnectionEntry> list_udp_connections(int pid);

}  // namespace tuide::packet_monitor
