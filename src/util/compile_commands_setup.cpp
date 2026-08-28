#include "util/compile_commands_setup.hpp"

#include "util/child_process_guard.hpp"
#include "util/thread_name.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace tuide {

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

std::atomic<uint64_t> g_cmake_generation{0};
std::atomic<pid_t> g_cmake_pid{-1};
std::atomic<bool> g_cmake_stop{false};
std::mutex g_cmake_thread_mutex;
std::thread g_cmake_thread;

bool run_cmake_configure(const fs::path& source_dir, const fs::path& build_dir) {
  if (g_cmake_stop.load(std::memory_order_acquire)) {
    return false;
  }
  const pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    child_die_with_parent();
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

  g_cmake_pid.store(pid, std::memory_order_release);
  int status = 0;
  pid_t waited = 0;
  while ((waited = waitpid(pid, &status, WNOHANG)) == 0) {
    if (g_cmake_stop.load(std::memory_order_acquire)) {
      kill(pid, SIGTERM);
      waitpid(pid, &status, 0);
      g_cmake_pid.store(-1, std::memory_order_release);
      return false;
    }
    usleep(50000);
  }
  g_cmake_pid.store(-1, std::memory_order_release);
  if (waited < 0) {
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

std::string ensure_host_compile_commands_dir(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return {};
  }

  const fs::path root(workspace_root);
  std::string compile_dir = find_compile_commands_dir(workspace_root);
  if (!compile_dir.empty()) {
    expose_compile_commands_at_root(root, fs::path(compile_dir));
    return compile_dir;
  }

  return {};
}

std::string generate_host_compile_commands_dir(const std::string& workspace_root) {
  if (workspace_root.empty() || g_cmake_stop.load(std::memory_order_acquire)) {
    return {};
  }

  const fs::path root(workspace_root);
  std::error_code ec;
  if (!fs::is_regular_file(root / "CMakeLists.txt", ec)) {
    return {};
  }

  std::string existing = find_compile_commands_dir(workspace_root);
  if (!existing.empty()) {
    expose_compile_commands_at_root(root, fs::path(existing));
    return existing;
  }

  for (const auto* build_name : kBuildDirNames) {
    if (g_cmake_stop.load(std::memory_order_acquire)) {
      return {};
    }
    const fs::path build_dir = root / build_name;
    fs::create_directories(build_dir, ec);
    ec.clear();
    if (!run_cmake_configure(root, build_dir)) {
      continue;
    }
    if (!compile_commands_at(build_dir, &ec)) {
      continue;
    }
    expose_compile_commands_at_root(root, build_dir);
    return build_dir.string();
  }

  return {};
}

void request_host_cmake_compile_commands(const std::string& workspace_root,
                                         std::function<void(std::string compile_dir)> on_done) {
  if (workspace_root.empty()) {
    if (on_done) {
      on_done({});
    }
    return;
  }

  const uint64_t generation = g_cmake_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  g_cmake_stop.store(false, std::memory_order_release);
  const pid_t previous = g_cmake_pid.exchange(-1, std::memory_order_acq_rel);
  if (previous > 0) {
    kill(previous, SIGTERM);
  }

  std::lock_guard<std::mutex> lock(g_cmake_thread_mutex);
  if (g_cmake_thread.joinable()) {
    g_cmake_thread.detach();
  }
  g_cmake_thread = std::thread([workspace_root, on_done, generation] {
    set_current_thread_name("cmake-cc");
    const std::string dir = generate_host_compile_commands_dir(workspace_root);
    if (g_cmake_generation.load(std::memory_order_acquire) != generation ||
        g_cmake_stop.load(std::memory_order_acquire)) {
      return;
    }
    if (on_done) {
      on_done(dir);
    }
  });
}

void shutdown_host_cmake_compile_commands() {
  g_cmake_stop.store(true, std::memory_order_release);
  g_cmake_generation.fetch_add(1, std::memory_order_acq_rel);
  const pid_t pid = g_cmake_pid.exchange(-1, std::memory_order_acq_rel);
  if (pid > 0) {
    kill(pid, SIGTERM);
  }
  std::lock_guard<std::mutex> lock(g_cmake_thread_mutex);
  if (g_cmake_thread.joinable()) {
    g_cmake_thread.join();
  }
}

}  // namespace tuide
