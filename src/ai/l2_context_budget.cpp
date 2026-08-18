#include "ai/l2_context_budget.hpp"

#include <algorithm>
#include <fstream>
#include <string>

namespace tuide {
namespace {

constexpr int kBaselineNCtx = 8192;
constexpr std::size_t kMinExplore = 6000;
constexpr std::size_t kMaxExplore = 48000;
constexpr std::size_t kMinEdit = 5000;
constexpr std::size_t kMaxEdit = 40000;
constexpr std::size_t kMinPack = 5000;
constexpr std::size_t kMaxPack = 40000;
constexpr std::size_t kMinObsPacked = 4000;
constexpr std::size_t kMaxObsPacked = 32000;
constexpr std::size_t kMinResume = 3000;
constexpr std::size_t kMaxResume = 24000;
constexpr std::size_t kMinObsTail = 1200;
constexpr std::size_t kMaxObsTail = 8000;
constexpr std::size_t kMinObsTurn = 1200;
constexpr std::size_t kMaxObsTurn = 9600;

std::size_t scale_clamp(std::size_t baseline, double scale, std::size_t lo, std::size_t hi) {
  const double v = static_cast<double>(baseline) * scale;
  std::size_t out = static_cast<std::size_t>(v + 0.5);
  if (out < lo) {
    out = lo;
  }
  if (out > hi) {
    out = hi;
  }
  return out;
}

std::optional<std::size_t> parse_kb_line(const std::string& line) {
  const auto colon = line.find(':');
  if (colon == std::string::npos) {
    return std::nullopt;
  }
  std::size_t i = colon + 1;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    ++i;
  }
  if (i >= line.size() || line[i] < '0' || line[i] > '9') {
    return std::nullopt;
  }
  std::size_t value = 0;
  while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
    value = value * 10 + static_cast<std::size_t>(line[i] - '0');
    ++i;
  }
  return value;
}

}  // namespace

L2ContextBudget default_l2_context_budget() {
  return budget_from_n_ctx(kBaselineNCtx, "local");
}

L2ContextBudget budget_from_n_ctx(int n_ctx, const std::string& backend) {
  L2ContextBudget b;
  b.backend = backend;
  if (n_ctx < 1024) {
    n_ctx = 1024;
  }
  b.n_ctx = n_ctx;
  const double scale = static_cast<double>(n_ctx) / static_cast<double>(kBaselineNCtx);
  b.prompt_explore = scale_clamp(10000, scale, kMinExplore, kMaxExplore);
  b.prompt_edit = scale_clamp(8000, scale, kMinEdit, kMaxEdit);
  b.pack_chars = scale_clamp(9000, scale, kMinPack, kMaxPack);
  b.frag_share = std::max<std::size_t>(400, b.pack_chars / 4);
  b.obs_packed = scale_clamp(8000, scale, kMinObsPacked, kMaxObsPacked);
  b.obs_per_turn = scale_clamp(2400, scale, kMinObsTurn, kMaxObsTurn);
  b.obs_tail = scale_clamp(1800, scale, kMinObsTail, kMaxObsTail);
  b.resume_chars = scale_clamp(5500, scale, kMinResume, kMaxResume);
  return b;
}

int n_ctx_cap_from_ram_kb(std::size_t mem_available_kb) {
  // Reserve ~4 GiB for model + OS; ~128 MiB KV per 1k tokens (7B Q4 rough).
  constexpr std::size_t kReserveKb = 4ull * 1024ull * 1024ull;
  constexpr int kFloor = 4096;
  constexpr int kCeil = 131072;
  if (mem_available_kb <= kReserveKb) {
    return kFloor;
  }
  const std::size_t spare = mem_available_kb - kReserveKb;
  const int extra_k = static_cast<int>(spare / (128ull * 1024ull));
  int cap = kFloor + extra_k * 1024;
  if (cap < kFloor) {
    cap = kFloor;
  }
  if (cap > kCeil) {
    cap = kCeil;
  }
  return cap;
}

std::size_t read_mem_available_kb() {
  std::ifstream in("/proc/meminfo");
  if (!in) {
    return 0;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("MemAvailable:", 0) == 0) {
      if (const auto v = parse_kb_line(line)) {
        return *v;
      }
      return 0;
    }
  }
  return 0;
}

L2ContextBudget compute_l2_context_budget(const std::string& backend,
                                          const AiLevel2Settings& settings,
                                          std::optional<std::size_t> mem_available_kb) {
  const bool remote = backend == "remote";
  int n_ctx = 0;
  if (remote) {
    n_ctx = settings.n_ctx_remote > 0 ? settings.n_ctx_remote : 0;
    if (n_ctx <= 0) {
      n_ctx = std::max(settings.n_ctx > 0 ? settings.n_ctx : kBaselineNCtx, 32768);
    }
  } else {
    n_ctx = settings.n_ctx > 0 ? settings.n_ctx : kBaselineNCtx;
    std::size_t mem_kb = 0;
    if (mem_available_kb.has_value()) {
      mem_kb = *mem_available_kb;
    } else {
      mem_kb = read_mem_available_kb();
    }
    if (mem_kb > 0) {
      n_ctx = std::min(n_ctx, n_ctx_cap_from_ram_kb(mem_kb));
    }
    if (n_ctx < 4096) {
      n_ctx = 4096;
    }
  }
  return budget_from_n_ctx(n_ctx, remote ? "remote" : "local");
}

}  // namespace tuide
