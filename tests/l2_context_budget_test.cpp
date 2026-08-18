#include <iostream>
#include <string>

#include "ai/ai_types.hpp"
#include "ai/l2_context_budget.hpp"

using tuide::AiLevel2Settings;
using tuide::budget_from_n_ctx;
using tuide::compute_l2_context_budget;
using tuide::default_l2_context_budget;
using tuide::n_ctx_cap_from_ram_kb;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

int main() {
  {
    const auto b = default_l2_context_budget();
    expect(b.n_ctx == 8192, "default n_ctx");
    expect(b.prompt_explore == 10000, "default explore");
    expect(b.prompt_edit == 8000, "default edit");
    expect(b.pack_chars == 9000, "default pack");
    expect(b.backend == "local", "default backend");
  }

  {
    const auto remote = budget_from_n_ctx(32768, "remote");
    expect(remote.n_ctx == 32768, "remote n_ctx");
    expect(remote.prompt_explore == 40000, "remote explore 4x");
    expect(remote.prompt_edit == 32000, "remote edit 4x");
    expect(remote.pack_chars == 36000, "remote pack 4x");
    expect(remote.prompt_explore <= 48000, "explore clamp max");
  }

  {
    const auto tiny = budget_from_n_ctx(4096, "local");
    expect(tiny.prompt_explore == 6000, "local floor explore");
    expect(tiny.pack_chars == 5000, "local floor pack");
  }

  {
    expect(n_ctx_cap_from_ram_kb(1024ull * 1024ull) == 4096, "low ram floor");
    // 8 GiB available → 4 GiB reserve → 4 GiB spare → ~32 extra k → 4096+32768
    const int cap8 = n_ctx_cap_from_ram_kb(8ull * 1024ull * 1024ull);
    expect(cap8 >= 32768, "8GiB allows large ctx");
  }

  {
    AiLevel2Settings s;
    s.n_ctx = 8192;
    s.n_ctx_remote = 32768;
    const auto local = compute_l2_context_budget("local", s, 64ull * 1024ull * 1024ull);
    expect(local.backend == "local", "compute local backend");
    expect(local.n_ctx == 8192, "compute local uses settings when RAM plenty");

    const auto remote = compute_l2_context_budget("remote", s, std::nullopt);
    expect(remote.backend == "remote", "compute remote backend");
    expect(remote.n_ctx == 32768, "compute remote uses n_ctx_remote");
    expect(remote.pack_chars > local.pack_chars, "remote pack > local pack");
  }

  {
    AiLevel2Settings s;
    s.n_ctx = 32768;
    s.n_ctx_remote = 0;
    // Tight RAM: reserve 4GiB, only ~4.5GiB available → floor-ish
    const auto capped = compute_l2_context_budget("local", s, 4500ull * 1024ull);
    expect(capped.n_ctx <= 8192, "RAM caps local n_ctx below settings");
    expect(capped.n_ctx >= 4096, "RAM cap still >= floor");
  }

  if (failures == 0) {
    std::cout << "l2_context_budget_test OK\n";
    return 0;
  }
  std::cerr << failures << " failure(s)\n";
  return 1;
}
