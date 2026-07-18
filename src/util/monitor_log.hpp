#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace tuide::monitor_log {

void set_enabled(bool on);
bool enabled();
void heartbeat();
void event(std::string_view category, std::string_view message);
std::string log_path();

class MonitorScope {
 public:
  MonitorScope(std::string_view category, std::string_view name);
  ~MonitorScope();

  MonitorScope(const MonitorScope&) = delete;
  MonitorScope& operator=(const MonitorScope&) = delete;

 private:
  std::string category_;
  std::string name_;
  std::chrono::steady_clock::time_point start_;
  bool active_ = false;
};

}  // namespace tuide::monitor_log

#define TUIDE_MON(cat, msg)                                                     \
  do {                                                                         \
    if (tuide::monitor_log::enabled()) {                                        \
      tuide::monitor_log::event((cat), (msg));                                  \
    }                                                                          \
  } while (false)

#define TUIDE_MON_SCOPE(cat, name)                                              \
  tuide::monitor_log::MonitorScope TUIDE_MON_SCOPE_CONCAT(_tuide_mon_, __LINE__)( \
      (cat), (name))

#define TUIDE_MON_SCOPE_CONCAT(a, b) TUIDE_MON_SCOPE_CONCAT_IMPL(a, b)
#define TUIDE_MON_SCOPE_CONCAT_IMPL(a, b) a##b
