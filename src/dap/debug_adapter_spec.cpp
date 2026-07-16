#include "dap/debug_adapter_spec.hpp"

#include <filesystem>

#include "util/bundled_tools.hpp"

namespace fs = std::filesystem;

namespace tgdb {

DebugAdapterKind debug_adapter_kind_for_program(const std::string& program_path) {
  if (program_path.empty()) {
    return DebugAdapterKind::kGdb;
  }
  const std::string ext = fs::path(program_path).extension().string();
  if (ext == ".py" || ext == ".pyw") {
    return DebugAdapterKind::kDebugpy;
  }
  return DebugAdapterKind::kGdb;
}

std::optional<DebugAdapterSpec> make_gdb_adapter_spec() {
  const auto location = resolve_gdb();
  if (!location.has_value()) {
    return std::nullopt;
  }
  DebugAdapterSpec spec;
  spec.kind = DebugAdapterKind::kGdb;
  spec.id = kDapAdapterGdb;
  spec.command = location->binary_path;
  spec.args = {"--quiet", "--interpreter=dap"};
  return spec;
}

std::optional<DebugAdapterSpec> make_debugpy_adapter_spec() {
  const auto location = resolve_debugpy();
  if (!location.has_value()) {
    return std::nullopt;
  }
  DebugAdapterSpec spec;
  spec.kind = DebugAdapterKind::kDebugpy;
  spec.id = kDapAdapterDebugpy;
  spec.command = location->python_path;
  spec.args = {"-m", "debugpy.adapter"};
  return spec;
}

std::optional<DebugAdapterSpec> make_debug_adapter_spec(const DebugAdapterKind kind) {
  if (kind == DebugAdapterKind::kDebugpy) {
    return make_debugpy_adapter_spec();
  }
  return make_gdb_adapter_spec();
}

}  // namespace tgdb
