#include "util/monitor_log.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

std::string g_dir;

void fail(const std::string& message, const std::string& dump) {
  std::cerr << "FAIL: " << message << "\n---\n" << dump << "\n---\n";
  std::exit(1);
}

void expect(bool condition, const char* message) {
  if (!condition) {
    fail(message, {});
  }
}

std::string read_file(const std::string& path) {
  std::ifstream input(path);
  std::string data;
  std::string line;
  while (std::getline(input, line)) {
    data += line;
    data += '\n';
  }
  return data;
}

void expect_has(const std::string& hay, const std::string& needle, const char* message) {
  if (hay.find(needle) == std::string::npos) {
    fail(message, hay);
  }
}

void expect_lacks(const std::string& hay, const std::string& needle, const char* message) {
  if (hay.find(needle) != std::string::npos) {
    fail(message, hay);
  }
}

void reset_monitor() {
  tuide::monitor_log::set_enabled(false);
  std::filesystem::remove_all(g_dir);
  std::filesystem::create_directories(g_dir);
  tuide::monitor_log::set_storage_directory(g_dir);
  tuide::monitor_log::set_enabled(true);
}

void test_user_action_survives_trim() {
  reset_monitor();
  tuide::monitor_log::note_user_action("debug_continue", "F5", "editor");
  tuide::monitor_log::test_append_line(
      "2000-01-01T00:00:00.000000+0000 [t] ui ancient noise");
  tuide::monitor_log::trim_once();

  const std::string flight = read_file(tuide::monitor_log::flight_path());
  const std::string log = read_file(tuide::monitor_log::log_path());
  expect_has(flight, "last_user: debug_continue key=F5 focus=editor", "user anchor");
  expect_has(flight, "ui: ALIVE", "alive ui");
  expect_lacks(log, "2000-01-01", "ancient noise trimmed");
  tuide::monitor_log::set_enabled(false);
}

void test_open_scope_survives_trim_and_marks_stuck() {
  reset_monitor();
  {
    tuide::monitor_log::MonitorScope scope("lsp", "send_request method=foo");
    const std::string log = read_file(tuide::monitor_log::log_path());
    const auto pos = log.rfind("start id=");
    expect(pos != std::string::npos, "start id present");
    const std::string id = log.substr(pos + std::string("start id=").size());
    const std::string id_token = id.substr(0, id.find_first_not_of("0123456789"));
    tuide::monitor_log::test_append_line(
        "2000-01-01T00:00:00.000000+0000 [t] lsp send_request method=foo start id=" + id_token);
    tuide::monitor_log::test_append_line("2000-01-01T00:00:00.000001+0000 [t] ui ancient noise");
    tuide::monitor_log::trim_once();

    const std::string kept = read_file(tuide::monitor_log::log_path());
    expect_has(kept, "2000-01-01T00:00:00.000000", "open start kept");
    expect_lacks(kept, "ancient noise", "noise trimmed while scope open");

    tuide::monitor_log::test_rewind_inflight(std::chrono::milliseconds(3000));
    tuide::monitor_log::trim_once();
    const std::string flight = read_file(tuide::monitor_log::flight_path());
    expect_has(flight, "send_request method=foo", "inflight name");
    expect_has(flight, "STUCK", "stuck marker");
  }

  tuide::monitor_log::test_append_line(
      "2000-01-01T00:00:00.000000+0000 [t] lsp send_request method=foo start id=999");
  tuide::monitor_log::trim_once();
  const std::string after = read_file(tuide::monitor_log::log_path());
  expect_lacks(after, "start id=999", "closed start trimmed");
  const std::string flight = read_file(tuide::monitor_log::flight_path());
  expect_has(flight, "(ninguno)", "inflight empty");
  tuide::monitor_log::set_enabled(false);
}

void test_freeze_keeps_log_and_resume_trims() {
  reset_monitor();
  tuide::monitor_log::note_user_action("save_file", "ctrl+s", "editor");
  tuide::monitor_log::test_append_line("2000-01-01T00:00:00.000000+0000 [t] ui ancient before freeze");
  tuide::monitor_log::test_set_heartbeat_age(std::chrono::milliseconds(2000));
  tuide::monitor_log::trim_once();

  const std::string frozen_log = read_file(tuide::monitor_log::log_path());
  const std::string frozen_flight = read_file(tuide::monitor_log::flight_path());
  expect_has(frozen_log, "ancient before freeze", "freeze skips trim");
  expect_has(frozen_log, "mon FREEZE", "freeze line");
  expect_has(frozen_flight, "ui: FROZEN", "frozen anchor");
  expect_has(frozen_flight, "last_user: save_file", "user kept while frozen");

  tuide::monitor_log::heartbeat();
  tuide::monitor_log::trim_once();
  const std::string resumed_log = read_file(tuide::monitor_log::log_path());
  const std::string resumed_flight = read_file(tuide::monitor_log::flight_path());
  expect_has(resumed_log, "mon RESUME", "resume line");
  expect_has(resumed_log, "mon FREEZE", "episode kept");
  expect_lacks(resumed_log, "ancient before freeze", "ancient trimmed after resume");
  expect_has(resumed_flight, "ui: ALIVE", "alive after resume");
  expect_has(resumed_flight, "last_user: save_file", "user kept after resume");
  tuide::monitor_log::set_enabled(false);
}

void test_dap_queue_start_and_end() {
  reset_monitor();
  tuide::monitor_log::note_dap_queued("continue", "");
  std::string flight = read_file(tuide::monitor_log::flight_path());
  expect_has(flight, "last_dap: continue", "dap kind");
  expect_has(flight, "(sin start)", "queued before start");

  tuide::monitor_log::note_dap_started("continue");
  flight = read_file(tuide::monitor_log::flight_path());
  expect_has(flight, "(sin end)", "started without end");
  expect_lacks(flight, "(sin start)", "start clears sin start");

  tuide::monitor_log::note_dap_queued("refresh_stack", "");
  flight = read_file(tuide::monitor_log::flight_path());
  expect_has(flight, "last_dap: refresh_stack", "newer queue");
  expect_has(flight, "(sin start)", "newer not started");
  expect_has(flight, "running_dap: continue", "previous still running");

  tuide::monitor_log::note_dap_finished("continue");
  tuide::monitor_log::note_dap_started("refresh_stack");
  tuide::monitor_log::note_dap_finished("refresh_stack");
  flight = read_file(tuide::monitor_log::flight_path());
  expect_has(flight, "end at=", "finished dap");
  expect_lacks(flight, "(sin end)", "end clears sin end");
  tuide::monitor_log::set_enabled(false);
}

void test_shutdown_marks_off() {
  reset_monitor();
  tuide::monitor_log::note_user_action("quit", "ctrl+q", "editor");
  tuide::monitor_log::set_enabled(false);
  const std::string flight = read_file(g_dir + "/monitor.flight");
  const std::string log = read_file(g_dir + "/monitor.log");
  expect_has(flight, "ui: OFF", "off after disable");
  expect_has(flight, "last_user: quit", "last user kept on shutdown");
  expect_has(log, "session end", "session end");
}

}  // namespace

int main() {
  g_dir = "/tmp/tuide-monitor-log-test-" + std::to_string(::getpid());
  tuide::monitor_log::set_background_trim(false);
  test_user_action_survives_trim();
  test_open_scope_survives_trim_and_marks_stuck();
  test_freeze_keeps_log_and_resume_trims();
  test_dap_queue_start_and_end();
  test_shutdown_marks_off();
  std::filesystem::remove_all(g_dir);
  std::cout << "ok\n";
  return 0;
}
