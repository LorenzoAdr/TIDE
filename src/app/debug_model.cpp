#include "app/debug_model.hpp"

#include <algorithm>
#include <filesystem>

namespace tgdb {

namespace {

std::string normalize_breakpoint_file(const std::string& file) {
  if (file.empty()) {
    return file;
  }
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(file, ec);
  return ec ? file : canonical.string();
}

}  // namespace

void DebugModel::append_console(const std::string& line) {
  console_output.push_back(line);
  constexpr std::size_t kMaxLines = 5000;
  if (console_output.size() > kMaxLines) {
    console_output.erase(console_output.begin(),
                         console_output.begin() +
                             static_cast<std::ptrdiff_t>(console_output.size() -
                                                         kMaxLines));
  }
}

namespace {

std::string format_stop_reason(const std::string& reason) {
  if (reason == "attach") {
    return "adjunto";
  }
  if (reason == "pause") {
    return "pausa";
  }
  if (reason == "breakpoint") {
    return "breakpoint";
  }
  if (reason == "step") {
    return "paso";
  }
  return reason;
}

}  // namespace

void DebugModel::set_stopped(int thread_id, const std::string& reason) {
  state = DebugState::kStopped;
  active_thread_id = thread_id;
  stop_reason = reason;
  if (reason.empty()) {
    status_message = "Detenido";
  } else {
    status_message = "Detenido: " + format_stop_reason(reason);
  }
}

void DebugModel::set_running() {
  state = DebugState::kRunning;
  status_message = "Ejecutando";
  stop_reason.clear();
}

void DebugModel::set_terminated() {
  state = DebugState::kTerminated;
  status_message = "Sesión finalizada";
}

void DebugModel::toggle_breakpoint(const std::string& file, int line) {
  const std::string key = normalize_breakpoint_file(file);

  auto& lines = breakpoints_by_file[key];
  if (lines.count(line) > 0) {
    lines.erase(line);
    if (lines.empty()) {
      breakpoints_by_file.erase(key);
    }
  } else {
    lines.insert(line);
  }
}

bool DebugModel::has_breakpoint(const std::string& file, int line) const {
  const auto it = breakpoints_by_file.find(normalize_breakpoint_file(file));
  if (it == breakpoints_by_file.end()) {
    return false;
  }
  return it->second.count(line) > 0;
}

bool DebugModel::is_breakpoint_enabled(const std::string& file, int line) const {
  if (!has_breakpoint(file, line)) {
    return false;
  }
  const auto key = normalize_breakpoint_file(file);
  const auto it = disabled_breakpoints.find(key);
  if (it == disabled_breakpoints.end()) {
    return true;
  }
  return it->second.count(line) == 0;
}

void DebugModel::set_breakpoint_enabled(const std::string& file, int line,
                                        bool enabled) {
  if (!has_breakpoint(file, line)) {
    return;
  }
  const std::string key = normalize_breakpoint_file(file);
  if (enabled) {
    auto it = disabled_breakpoints.find(key);
    if (it != disabled_breakpoints.end()) {
      it->second.erase(line);
      if (it->second.empty()) {
        disabled_breakpoints.erase(it);
      }
    }
  } else {
    disabled_breakpoints[normalize_breakpoint_file(file)].insert(line);
  }
}

void DebugModel::remove_breakpoint(const std::string& file, int line) {
  const std::string key = normalize_breakpoint_file(file);
  auto it = breakpoints_by_file.find(key);
  if (it == breakpoints_by_file.end()) {
    return;
  }
  it->second.erase(line);
  if (it->second.empty()) {
    breakpoints_by_file.erase(it);
  }
  auto disabled = disabled_breakpoints.find(key);
  if (disabled != disabled_breakpoints.end()) {
    disabled->second.erase(line);
    if (disabled->second.empty()) {
      disabled_breakpoints.erase(disabled);
    }
  }
}

std::vector<int> DebugModel::enabled_breakpoint_lines(
    const std::string& file) const {
  std::vector<int> lines;
  const std::string key = normalize_breakpoint_file(file);
  const auto it = breakpoints_by_file.find(key);
  if (it == breakpoints_by_file.end()) {
    return lines;
  }
  for (int line : it->second) {
    if (is_breakpoint_enabled(key, line)) {
      lines.push_back(line);
    }
  }
  return lines;
}

void DebugModel::add_watch(const std::string& expression) {
  for (const auto& watch : watches) {
    if (watch.expression == expression) {
      return;
    }
  }
  watches.push_back({expression, "..."});
}

void DebugModel::remove_watch(int index) {
  if (index < 0 || index >= static_cast<int>(watches.size())) {
    return;
  }
  watches.erase(watches.begin() + index);
}

void DebugModel::remove_watch(const std::string& expression) {
  watches.erase(
      std::remove_if(watches.begin(), watches.end(),
                     [&](const WatchEntry& w) { return w.expression == expression; }),
      watches.end());
}

}  // namespace tgdb
