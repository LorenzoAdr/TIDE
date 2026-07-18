#include "packet_monitor/pkt_protocol.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path resolve_hello_protocol_path() {
  const fs::path relative = fs::path("examples") / "protocols" / "hello_sensor.json";
  const std::vector<fs::path> candidates = {
#ifdef TUIDE_SOURCE_DIR
      fs::path(TUIDE_SOURCE_DIR) / relative,
#endif
      fs::current_path() / relative,
      relative,
  };
  for (const auto& candidate : candidates) {
    std::error_code ec;
    if (fs::exists(candidate, ec)) {
      return candidate;
    }
  }
  return relative;
}

}  // namespace

int main() {
  const fs::path protocol_path = resolve_hello_protocol_path();
  tuide::packet_monitor::ProtocolDefinition protocol;
  std::string error;
  if (!tuide::packet_monitor::load_protocol_from_json(protocol_path.string(), &protocol, &error)) {
    std::cerr << "load failed: " << error << " (tried " << protocol_path.string() << ")\n";
    return 1;
  }

  std::vector<uint8_t> heartbeat = {0x01, 0x00, 0x34, 0x12};
  const auto decoded_hb =
      tuide::packet_monitor::decode_packet(heartbeat, protocol, "0x01");
  assert(decoded_hb.error.empty());
  assert(decoded_hb.variant_name == "Heartbeat");
  assert(decoded_hb.fields.size() == 3);

  std::vector<uint8_t> sensor = {0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0xB8, 0x41, 0x01, 0x00};
  const auto decoded_sensor =
      tuide::packet_monitor::decode_packet(sensor, protocol, "0x02");
  assert(decoded_sensor.error.empty());
  assert(decoded_sensor.variant_name == "SensorData");
  assert(decoded_sensor.fields.size() == 4);

  std::cout << "pkt_protocol_test ok (" << protocol_path.string() << ")\n";
  return 0;
}
