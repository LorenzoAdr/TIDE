#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tuide {

enum class DebugAdapterKind { kGdb, kDebugpy, kBashdb };

struct DebugAdapterSpec {
  DebugAdapterKind kind = DebugAdapterKind::kGdb;
  std::string id;  // DAP adapterID: "gdb", "debugpy", "bashdb"
  std::string command;
  std::vector<std::string> args;
};

inline constexpr const char* kDapAdapterGdb = "gdb";
inline constexpr const char* kDapAdapterDebugpy = "debugpy";
inline constexpr const char* kDapAdapterBashdb = "bashdb";

DebugAdapterKind debug_adapter_kind_for_program(const std::string& program_path);

std::optional<DebugAdapterSpec> make_gdb_adapter_spec();
std::optional<DebugAdapterSpec> make_debugpy_adapter_spec();
std::optional<DebugAdapterSpec> make_bashdb_adapter_spec();
std::optional<DebugAdapterSpec> make_debug_adapter_spec(DebugAdapterKind kind);

}  // namespace tuide
