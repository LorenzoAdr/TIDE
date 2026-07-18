#include "packet_monitor/pkt_ring.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "packet_monitor/pkt_protocol.hpp"

namespace tuide::packet_monitor {

namespace {

std::string trim(std::string value) {
  while (!value.empty() && value.front() == ' ') {
    value.erase(value.begin());
  }
  while (!value.empty() && value.back() == ' ') {
    value.pop_back();
  }
  return value;
}

}  // namespace

std::string runtime_dir_path() {
  const char* dir = std::getenv("XDG_RUNTIME_DIR");
  if (dir != nullptr && dir[0] != '\0') {
    return dir;
  }
  return "/tmp";
}

std::string shm_path_for_pid(int pid) {
  return runtime_dir_path() + "/tuide-pkt-" + std::to_string(pid) + ".mmap";
}

std::string format_ipv4(uint32_t addr) {
  if (addr == 0) {
    return "*";
  }
  struct in_addr in {};
  in.s_addr = addr;
  char buffer[INET_ADDRSTRLEN] = {};
  if (inet_ntop(AF_INET, &in, buffer, sizeof(buffer)) == nullptr) {
    return "?";
  }
  return buffer;
}

bool parse_ipv4(const std::string& text, uint32_t* out) {
  if (out == nullptr) {
    return false;
  }
  const std::string value = trim(text);
  if (value.empty() || value == "*") {
    *out = 0;
    return true;
  }
  struct in_addr addr {};
  if (inet_pton(AF_INET, value.c_str(), &addr) != 1) {
    return false;
  }
  *out = addr.s_addr;
  return true;
}

bool PacketRingReader::open_for_pid(int pid) {
  close();
  if (pid <= 0) {
    return false;
  }
  path_ = shm_path_for_pid(pid);
  const int fd = ::open(path_.c_str(), O_RDONLY);
  if (fd < 0) {
    return false;
  }

  struct stat st {};
  if (fstat(fd, &st) != 0 || st.st_size <= 0) {
    ::close(fd);
    return false;
  }

  mapped_size_ = static_cast<std::size_t>(st.st_size);
  mapped_ = mmap(nullptr, mapped_size_, PROT_READ, MAP_SHARED, fd, 0);
  ::close(fd);
  if (mapped_ == MAP_FAILED) {
    mapped_ = nullptr;
    mapped_size_ = 0;
    return false;
  }

  header_ = static_cast<TuidePktHeader*>(mapped_);
  if (header_->magic != TUIDE_PKT_MAGIC || header_->capacity == 0) {
    close();
    return false;
  }
  entries_ = tuide_pkt_entries(header_);
  read_idx_ = header_->write_idx;
  pid_ = pid;
  return true;
}

void PacketRingReader::close() {
  if (mapped_ != nullptr && mapped_ != MAP_FAILED) {
    munmap(mapped_, mapped_size_);
  }
  mapped_ = nullptr;
  mapped_size_ = 0;
  header_ = nullptr;
  entries_ = nullptr;
  read_idx_ = 0;
  pid_ = 0;
  path_.clear();
}

std::vector<PacketRecord> PacketRingReader::poll_new_packets() {
  std::vector<PacketRecord> packets;
  if (header_ == nullptr || entries_ == nullptr) {
    return packets;
  }

  const uint32_t write_idx = header_->write_idx;
  while (read_idx_ < write_idx) {
    const uint32_t slot = tuide_pkt_slot_index(read_idx_, header_->capacity);
    const TuidePktEntry& entry = entries_[slot];
    PacketRecord record;
    record.timestamp_ns = entry.timestamp_ns;
    record.direction = entry.direction;
    record.src_port = entry.src_port;
    record.dst_port = entry.dst_port;
    record.src_ipv4 = entry.src_ipv4;
    record.dst_ipv4 = entry.dst_ipv4;
    if (entry.payload_len > 0) {
      record.payload.assign(entry.payload, entry.payload + entry.payload_len);
    }
    packets.push_back(std::move(record));
    ++read_idx_;
  }
  return packets;
}

bool packet_matches_filters(const PacketRecord& packet, const CaptureFilters& filters,
                            const ProtocolDefinition* protocol) {
  uint32_t src_filter = 0;
  uint32_t dst_filter = 0;
  if (!filters.src_ip.empty() && filters.src_ip != "*") {
    if (!parse_ipv4(filters.src_ip, &src_filter)) {
      return false;
    }
    if (packet.src_ipv4 != src_filter) {
      return false;
    }
  }
  if (!filters.dst_ip.empty() && filters.dst_ip != "*") {
    if (!parse_ipv4(filters.dst_ip, &dst_filter)) {
      return false;
    }
    if (packet.dst_ipv4 != dst_filter) {
      return false;
    }
  }

  const std::size_t size = packet.payload.size();
  if (size < filters.min_size || size > filters.max_size) {
    return false;
  }

  if (!filters.variant_key.empty() && protocol != nullptr && protocol->has_discriminator) {
    std::string key;
    if (!read_discriminator(packet.payload, protocol->discriminator, &key)) {
      return false;
    }
    if (key != filters.variant_key) {
      return false;
    }
  }
  return true;
}

}  // namespace tuide::packet_monitor
