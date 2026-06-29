#include "util/compile_commands_setup.hpp"

#include <array>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace tgdb {

namespace {

constexpr std::array<const char*, 4> kBuildDirNames = {
    "build",
    "cmake-build-debug",
    "cmake-build-release",
    "out/build",
};

bool compile_commands_at(const fs::path& dir, std::error_code* ec) {
  return fs::is_regular_file(dir / "compile_commands.json", *ec);
}

bool run_cmake_configure(const fs::path& source_dir, const fs::path& build_dir) {
  const pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    const int devnull = ::open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      ::close(devnull);
    }

    const std::string source = source_dir.string();
    const std::string build = build_dir.string();
    std::array<char*, 7> argv = {
        const_cast<char*>("cmake"),
        const_cast<char*>("-S"),
        const_cast<char*>(source.c_str()),
        const_cast<char*>("-B"),
        const_cast<char*>(build.c_str()),
        const_cast<char*>("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"),
        nullptr,
    };
    execvp("cmake", argv.data());
    _exit(127);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void expose_compile_commands_at_root(const fs::path& workspace_root,
                                     const fs::path& compile_commands_dir) {
  std::error_code ec;
  const fs::path root_db = workspace_root / "compile_commands.json";
  if (fs::exists(root_db, ec)) {
    return;
  }

  const fs::path source_db = compile_commands_dir / "compile_commands.json";
  if (!fs::is_regular_file(source_db, ec)) {
    return;
  }
  if (compile_commands_dir == workspace_root) {
    return;
  }

  fs::path link_target = fs::relative(source_db, workspace_root, ec);
  if (ec) {
    link_target = source_db;
  }
  fs::create_symlink(link_target, root_db, ec);
}

}  // namespace

std::string find_compile_commands_dir(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return {};
  }

  const fs::path root(workspace_root);
  std::error_code ec;

  const fs::path direct_candidates[] = {
      root / "compile_commands.json",
      root / "build" / "compile_commands.json",
      root / "cmake-build-debug" / "compile_commands.json",
      root / "cmake-build-release" / "compile_commands.json",
      root / "out" / "build" / "compile_commands.json",
      root / ".build" / "compile_commands.json",
  };
  for (const auto& candidate : direct_candidates) {
    if (fs::is_regular_file(candidate, ec)) {
      return candidate.parent_path().string();
    }
  }

  for (const auto* build_name : kBuildDirNames) {
    const fs::path build_dir = root / build_name;
    if (compile_commands_at(build_dir, &ec)) {
      return build_dir.string();
    }
  }

  return {};
}

std::string ensure_compile_commands_for_clangd(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return {};
  }

  const fs::path root(workspace_root);
  std::string compile_dir = find_compile_commands_dir(workspace_root);
  if (!compile_dir.empty()) {
    expose_compile_commands_at_root(root, fs::path(compile_dir));
    return compile_dir;
  }

  std::error_code ec;
  if (!fs::is_regular_file(root / "CMakeLists.txt", ec)) {
    return {};
  }

  for (const auto* build_name : kBuildDirNames) {
    const fs::path build_dir = root / build_name;
    fs::create_directories(build_dir, ec);
    ec.clear();
    if (!run_cmake_configure(root, build_dir)) {
      continue;
    }
    if (!compile_commands_at(build_dir, &ec)) {
      continue;
    }
    compile_dir = build_dir.string();
    expose_compile_commands_at_root(root, build_dir);
    return compile_dir;
  }

  return {};
}

}  // namespace tgdb
