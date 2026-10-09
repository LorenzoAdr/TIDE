#include "util/docker_clangd_spec.hpp"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect_true(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_eq(const std::string& actual, const std::string& expected, const char* message) {
  if (actual != expected) {
    std::cerr << "FAIL: " << message << "\n  got:      " << actual << "\n  expected: " << expected
              << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using tuide::DockerClangdLaunch;

  expect_eq(tuide::clangd_path_mappings_argument({{"/home/user", "/hosthome"},
                                                 {"/home/user/proj", "/workspace"}}),
            "/home/user/proj=/workspace,/home/user=/hosthome",
            "longest host prefix comes first");
  expect_eq(tuide::clangd_path_mappings_argument({{"/home/a=b", "/workspace"}, {"", "/workspace"}}),
            "", "paths with '=' and empty pairs are dropped");

  const auto bundled_root =
      tuide::docker_clangd_install_root("/cache/tuide/clangd-19/bin/clangd",
                                        "/cache/tuide/clangd-19/lib/clang/19");
  expect_true(bundled_root.has_value(), "bundled layout has an install root");
  if (bundled_root.has_value()) {
    expect_eq(*bundled_root, "/cache/tuide/clangd-19", "install root is the prefix above bin");
    expect_eq(tuide::docker_clangd_remote_resource_dir(*bundled_root,
                                                      "/cache/tuide/clangd-19/lib/clang/19"),
              "/tmp/tuide-clangd/lib/clang/19", "resource dir keeps lib/clang inside the container");
  }
  expect_true(!tuide::docker_clangd_install_root("/usr/bin/clangd", "/usr/lib/clang/18").has_value(),
              "system /usr prefix is not copied as a tree");
  expect_eq(tuide::docker_clangd_remote_resource_dir("/cache/clangd", "/opt/llvm/lib/clang/18"),
            "/tmp/tuide-clangd/resource", "a resource dir outside the copied root uses /resource");

  DockerClangdLaunch launch;
  launch.docker_binary = "/usr/bin/docker";
  launch.container = "dev";
  launch.remote_binary = "/tmp/tuide-clangd/bin/clangd";
  launch.remote_resource_dir = "/tmp/tuide-clangd/lib/clang/19";
  launch.remote_compile_commands_dir = "/workspace/build";
  launch.path_mappings = "/home/proj=/workspace";
  const tuide::LanguageServerSpec spec =
      tuide::make_docker_clangd_spec("/home/proj", launch, true, false);
  expect_eq(spec.command, "/usr/bin/docker", "clangd is started via the docker binary");
  expect_eq(spec.workspace_root, "/home/proj", "LSP root stays the host workspace");
  expect_true(spec.args.size() >= 8, "docker exec argv has clangd flags");
  if (spec.args.size() >= 8) {
    expect_eq(spec.args[0], "exec", "docker subcommand");
    expect_eq(spec.args[1], "-i", "stdin stays open for LSP");
    expect_eq(spec.args[2], "dev", "container name");
    expect_eq(spec.args[3], "/tmp/tuide-clangd/bin/clangd", "injected clangd");
    expect_eq(spec.args[4], "--resource-dir=/tmp/tuide-clangd/lib/clang/19", "in-container resource dir");
    expect_eq(spec.args[5], "--compile-commands-dir=/workspace/build",
              "compile commands stay on container paths");
    expect_eq(spec.args[6], "--path-mappings=/home/proj=/workspace", "editor paths map into the container");
    expect_true(spec.args[7].rfind("--query-driver=/usr/bin/gcc", 0) == 0,
                "query-driver targets the compiler inside the container");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "docker_clangd_test ok\n";
  return 0;
}
