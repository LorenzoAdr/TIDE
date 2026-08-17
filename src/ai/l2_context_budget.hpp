#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "ai/ai_types.hpp"

namespace tuide {

// Prompt/pack/obs budgets derived from the active L2 backend's effective n_ctx.
// Disk may keep a richer pack; propose() always slices to these caps.
struct L2ContextBudget {
  int n_ctx = 8192;
  std::size_t prompt_explore = 10000;
  std::size_t prompt_edit = 8000;
  std::size_t pack_chars = 9000;
  std::size_t frag_share = 2250;
  std::size_t obs_packed = 8000;
  std::size_t obs_per_turn = 2400;
  std::size_t obs_tail = 1800;
  std::size_t resume_chars = 5500;
  std::string backend;  // "local" | "remote"
};

// Baseline calibrated to ai.level2.n_ctx=8192 (explore~10k / edit~8k / pack~9k).
L2ContextBudget default_l2_context_budget();

// Pure scaler: linear from n_ctx relative to 8192, then clamp.
L2ContextBudget budget_from_n_ctx(int n_ctx, const std::string& backend);

// RAM ceiling for local KV (MemAvailable heuristic). Floor 4096.
int n_ctx_cap_from_ram_kb(std::size_t mem_available_kb);

// Read MemAvailable from /proc/meminfo; 0 if unavailable.
std::size_t read_mem_available_kb();

// backend: "local" | "remote". mem_available_kb nullopt → read live (local only).
L2ContextBudget compute_l2_context_budget(const std::string& backend,
                                          const AiLevel2Settings& settings,
                                          std::optional<std::size_t> mem_available_kb = std::nullopt);

}  // namespace tuide
