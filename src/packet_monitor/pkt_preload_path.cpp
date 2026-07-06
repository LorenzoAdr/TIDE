#include "packet_monitor/pkt_preload_path.hpp"

#include <climits>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

namespace tgdb::packet_monitor {

namespace fs = std::filesystem;

std::string resolve_preload_library_path() {
  if (const char* override_path = std::getenv("TGDB_PKT_PRELOAD_PATH")) {
    if (override_path[0] != '\0') {
      return override_path;
    }
  }

  std::error_code ec;
  char buffer[PATH_MAX] = {};
  const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length > 0) {
    buffer[length] = '\0';
    const fs::path exe = fs::path(buffer);
    const fs::path candidate = exe.parent_path() / "lib" / "libtgdb_pkt.so";
    if (fs::exists(candidate, ec)) {
      return candidate.string();
    }
    const fs::path sibling = exe.parent_path() / "libtgdb_pkt.so";
    if (fs::exists(sibling, ec)) {
      return sibling.string();
    }
  }

  const fs::path build_candidate = fs::current_path(ec) / "build" / "lib" / "libtgdb_pkt.so";
  if (fs::exists(build_candidate, ec)) {
    return build_candidate.string();
  }
  return {};
}

}  // namespace tgdb::packet_monitor
