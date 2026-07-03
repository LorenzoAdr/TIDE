#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace tgdb::monitor_log {

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

}  // namespace tgdb::monitor_log

#define TGDB_MON(cat, msg)                                                     \
  do {                                                                         \
    if (tgdb::monitor_log::enabled()) {                                        \
      tgdb::monitor_log::event((cat), (msg));                                  \
    }                                                                          \
  } while (false)

#define TGDB_MON_SCOPE(cat, name)                                              \
  tgdb::monitor_log::MonitorScope TGDB_MON_SCOPE_CONCAT(_tgdb_mon_, __LINE__)( \
      (cat), (name))

#define TGDB_MON_SCOPE_CONCAT(a, b) TGDB_MON_SCOPE_CONCAT_IMPL(a, b)
#define TGDB_MON_SCOPE_CONCAT_IMPL(a, b) a##b
