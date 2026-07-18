#include "packet_monitor/pkt_connections.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace tuide::packet_monitor {

namespace {

std::string trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  return value;
}

std::string format_hex_ip_port(const std::string& hex_ip, const std::string& hex_port) {
  if (hex_ip.size() != 8 || hex_port.size() != 4) {
    return "?";
  }
  try {
    const unsigned long ip = std::stoul(hex_ip, nullptr, 16);
    const unsigned long port = std::stoul(hex_port, nullptr, 16);
    const unsigned char b0 = static_cast<unsigned char>((ip >> 0) & 0xFF);
    const unsigned char b1 = static_cast<unsigned char>((ip >> 8) & 0xFF);
    const unsigned char b2 = static_cast<unsigned char>((ip >> 16) & 0xFF);
    const unsigned char b3 = static_cast<unsigned char>((ip >> 24) & 0xFF);
    std::ostringstream stream;
    stream << static_cast<int>(b0) << '.' << static_cast<int>(b1) << '.' << static_cast<int>(b2)
           << '.' << static_cast<int>(b3) << ':' << port;
    return stream.str();
  } catch (...) {
    return "?";
  }
}

}  // namespace

std::vector<UdpConnectionEntry> list_udp_connections(int pid) {
  std::vector<UdpConnectionEntry> entries;
  if (pid <= 0) {
    return entries;
  }

  std::ifstream input("/proc/" + std::to_string(pid) + "/net/udp");
  if (!input) {
    return entries;
  }

  std::string line;
  std::getline(input, line);
  while (std::getline(input, line)) {
    std::istringstream stream(line);
    std::string local;
    std::string remote;
    std::string state;
    stream >> local >> remote >> state;
    if (local.empty() || remote.empty()) {
      continue;
    }
    const auto local_colon = local.find(':');
    const auto remote_colon = remote.find(':');
    if (local_colon == std::string::npos || remote_colon == std::string::npos) {
      continue;
    }
    UdpConnectionEntry entry;
    entry.local_address =
        format_hex_ip_port(local.substr(0, local_colon), local.substr(local_colon + 1));
    entry.remote_address =
        format_hex_ip_port(remote.substr(0, remote_colon), remote.substr(remote_colon + 1));
    entry.state = trim(state);
    entries.push_back(std::move(entry));
  }
  return entries;
}

}  // namespace tuide::packet_monitor
