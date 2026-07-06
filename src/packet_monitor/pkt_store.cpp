#include "packet_monitor/pkt_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace tgdb::packet_monitor {

namespace fs = std::filesystem;

namespace {

nlohmann::json filters_to_json(const CaptureFilters& filters) {
  return nlohmann::json{{"src_ip", filters.src_ip},
                        {"dst_ip", filters.dst_ip},
                        {"min_size", filters.min_size},
                        {"max_size", filters.max_size},
                        {"variant_key", filters.variant_key}};
}

CaptureFilters filters_from_json(const nlohmann::json& node) {
  CaptureFilters filters;
  if (node.contains("src_ip") && node["src_ip"].is_string()) {
    filters.src_ip = node["src_ip"].get<std::string>();
  }
  if (node.contains("dst_ip") && node["dst_ip"].is_string()) {
    filters.dst_ip = node["dst_ip"].get<std::string>();
  }
  if (node.contains("min_size") && node["min_size"].is_number_unsigned()) {
    filters.min_size = node["min_size"].get<std::size_t>();
  }
  if (node.contains("max_size") && node["max_size"].is_number_unsigned()) {
    filters.max_size = node["max_size"].get<std::size_t>();
  }
  if (node.contains("variant_key") && node["variant_key"].is_string()) {
    filters.variant_key = node["variant_key"].get<std::string>();
  }
  return filters;
}

nlohmann::json packet_to_json(const PacketRecord& packet) {
  nlohmann::json node;
  node["timestamp_ns"] = packet.timestamp_ns;
  node["direction"] = packet.direction;
  node["src_port"] = packet.src_port;
  node["dst_port"] = packet.dst_port;
  node["src_ipv4"] = packet.src_ipv4;
  node["dst_ipv4"] = packet.dst_ipv4;
  node["payload"] = packet.payload;
  return node;
}

PacketRecord packet_from_json(const nlohmann::json& node) {
  PacketRecord packet;
  if (node.contains("timestamp_ns") && node["timestamp_ns"].is_number_unsigned()) {
    packet.timestamp_ns = node["timestamp_ns"].get<uint64_t>();
  }
  if (node.contains("direction") && node["direction"].is_number_unsigned()) {
    packet.direction = static_cast<uint8_t>(node["direction"].get<unsigned>());
  }
  if (node.contains("src_port") && node["src_port"].is_number_unsigned()) {
    packet.src_port = static_cast<uint16_t>(node["src_port"].get<unsigned>());
  }
  if (node.contains("dst_port") && node["dst_port"].is_number_unsigned()) {
    packet.dst_port = static_cast<uint16_t>(node["dst_port"].get<unsigned>());
  }
  if (node.contains("src_ipv4") && node["src_ipv4"].is_number_unsigned()) {
    packet.src_ipv4 = node["src_ipv4"].get<uint32_t>();
  }
  if (node.contains("dst_ipv4") && node["dst_ipv4"].is_number_unsigned()) {
    packet.dst_ipv4 = node["dst_ipv4"].get<uint32_t>();
  }
  if (node.contains("payload") && node["payload"].is_array()) {
    for (const auto& byte : node["payload"]) {
      if (byte.is_number_unsigned()) {
        packet.payload.push_back(static_cast<uint8_t>(byte.get<unsigned>()));
      }
    }
  }
  return packet;
}

}  // namespace

std::string default_capture_dir() {
  const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
  if (xdg_cache != nullptr && xdg_cache[0] != '\0') {
    return std::string(xdg_cache) + "/tgdb/captures";
  }
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    return "/tmp/tgdb/captures";
  }
  return std::string(home) + "/.cache/tgdb/captures";
}

bool save_capture_file(const std::string& path, const CaptureFileHeader& header,
                       const std::vector<PacketRecord>& packets, std::string* error) {
  nlohmann::json doc;
  doc["magic"] = "TGDB_PKT_CAP";
  doc["version"] = 1;
  doc["protocol_path"] = header.protocol_path;
  doc["filters"] = filters_to_json(header.filters);
  doc["packets"] = nlohmann::json::array();
  for (const auto& packet : packets) {
    doc["packets"].push_back(packet_to_json(packet));
  }

  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  std::ofstream output(path);
  if (!output) {
    if (error != nullptr) {
      *error = "cannot write capture file";
    }
    return false;
  }
  output << doc.dump(2);
  return true;
}

bool load_capture_file(const std::string& path, CaptureFileHeader* header,
                       std::vector<PacketRecord>* packets, std::string* error) {
  if (header == nullptr || packets == nullptr) {
    return false;
  }
  std::ifstream input(path);
  if (!input) {
    if (error != nullptr) {
      *error = "cannot open capture file";
    }
    return false;
  }
  nlohmann::json doc;
  try {
    input >> doc;
  } catch (const std::exception& ex) {
    if (error != nullptr) {
      *error = ex.what();
    }
    return false;
  }
  if (!doc.contains("packets") || !doc["packets"].is_array()) {
    if (error != nullptr) {
      *error = "invalid capture file";
    }
    return false;
  }
  header->protocol_path.clear();
  if (doc.contains("protocol_path") && doc["protocol_path"].is_string()) {
    header->protocol_path = doc["protocol_path"].get<std::string>();
  }
  if (doc.contains("filters") && doc["filters"].is_object()) {
    header->filters = filters_from_json(doc["filters"]);
  }
  packets->clear();
  for (const auto& node : doc["packets"]) {
    packets->push_back(packet_from_json(node));
  }
  return true;
}

}  // namespace tgdb::packet_monitor
