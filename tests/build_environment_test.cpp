#include "build/build_environment.hpp"
#include "build/build_environment_selector.hpp"
#include "build/make_qp_parser.hpp"

#include <cassert>
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
    std::cerr << "FAIL: " << message << " (got '" << actual << "', expected '" << expected
              << "')\n";
    ++failures;
  }
}

}  // namespace

int main() {
  const std::string make_qp = R"(
CFLAGS := -O2 -Iinclude -DNDEBUG
CXXFLAGS := -std=c++17 -Iinclude
BUILD_DIR := build
debug:
release:
all:
)";

  const tgdb::MakeQpInfo info = tgdb::parse_make_qp_output(make_qp);
  expect_true(info.variables.at("CFLAGS").find("-Iinclude") != std::string::npos,
              "CFLAGS parsed");
  expect_true(info.variables.at("BUILD_DIR") == "build", "BUILD_DIR parsed");
  expect_true(!info.compile_flags.empty(), "compile flags extracted");

  tgdb::BuildEnvironment env_a;
  env_a.working_dir = "/tmp/project";
  env_a.make_command = "make";
  env_a.id = tgdb::build_environment_id(env_a);

  tgdb::BuildEnvironment env_b = env_a;
  env_b.make_command = "make debug";
  env_b.id = tgdb::build_environment_id(env_b);
  expect_true(env_a.id != env_b.id, "environment ids differ by make command");

  std::vector<tgdb::BuildEnvironment> candidates = {env_a, env_b};
  tgdb::EnvironmentSelectionHints hints;
  hints.active_file_path = "/tmp/project/build/main.o";

  const auto selection = tgdb::select_active_environment(candidates, "auto", hints, env_a.id);
  expect_eq(selection.environment.id, env_a.id, "hysteresis keeps previous environment");

  const auto forced = tgdb::select_active_environment(candidates, env_b.id, hints, env_a.id);
  expect_eq(forced.environment.id, env_b.id, "forced environment selected");

  if (failures == 0) {
    std::cout << "build_environment_test: OK\n";
    return 0;
  }
  return 1;
}
