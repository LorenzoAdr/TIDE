#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "ui/ui_event_types.hpp"

namespace tuide {

struct UiEventTraceEntry {
  uint64_t seq = 0;
  UiEventKind kind = UiEventKind::UserInput;
  std::string tag;
  uint64_t correlation_id = 0;
  std::string src_file;
  int src_line = 0;
  int64_t ts_ms = 0;
  bool coalesced = false;
};

class UiEventTrace {
 public:
  static constexpr std::size_t kCapacity = 256;

  void record(const UiEvent& event, bool coalesced, int64_t ts_ms);
  std::vector<UiEventTraceEntry> recent(std::size_t limit = 16) const;
  std::array<uint64_t, 4> counts_by_kind() const;
  uint64_t total_emitted() const { return total_emitted_; }

 private:
  mutable std::mutex mutex_;
  std::array<UiEventTraceEntry, kCapacity> ring_{};
  std::size_t ring_head_ = 0;
  std::size_t ring_size_ = 0;
  uint64_t next_seq_ = 1;
  uint64_t total_emitted_ = 0;
  std::array<uint64_t, 4> kind_counts_{};
};

}  // namespace tuide
