#include "ui/ui_event_trace.hpp"

#include <algorithm>

namespace tuide {

const char* ui_event_kind_label(UiEventKind kind) {
  switch (kind) {
    case UiEventKind::UserInput:
      return "input";
    case UiEventKind::InputCorrelated:
      return "correlated";
    case UiEventKind::TerminalOutput:
      return "terminal";
    case UiEventKind::DebugCritical:
      return "debug";
  }
  return "unknown";
}

void UiEventTrace::record(const UiEvent& event, bool coalesced, int64_t ts_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  UiEventTraceEntry entry;
  entry.seq = next_seq_++;
  entry.kind = event.kind;
  entry.tag = event.tag;
  entry.correlation_id = event.correlation_id;
  entry.src_file = event.src_file != nullptr ? event.src_file : "";
  entry.src_line = event.src_line;
  entry.ts_ms = ts_ms;
  entry.coalesced = coalesced;
  ring_[ring_head_] = std::move(entry);
  ring_head_ = (ring_head_ + 1) % kCapacity;
  if (ring_size_ < kCapacity) {
    ++ring_size_;
  }
  ++total_emitted_;
  kind_counts_[static_cast<std::size_t>(event.kind)]++;
}

std::vector<UiEventTraceEntry> UiEventTrace::recent(std::size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::size_t count = std::min(limit, ring_size_);
  std::vector<UiEventTraceEntry> out;
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t index = (ring_head_ + kCapacity - 1 - i) % kCapacity;
    out.push_back(ring_[index]);
  }
  return out;
}

std::array<uint64_t, 4> UiEventTrace::counts_by_kind() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return kind_counts_;
}

}  // namespace tuide
