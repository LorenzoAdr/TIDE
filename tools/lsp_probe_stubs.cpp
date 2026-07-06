#include "util/compile_commands_remap.hpp"

namespace tgdb {

CompileCommandsSetupResult ensure_compile_commands_for_clangd(
    const std::string& workspace_root, const WorkspaceConfig& /*config*/) {
  return CompileCommandsSetupResult{.compile_dir = workspace_root + "/.tgdb"};
}

}  // namespace tgdb
