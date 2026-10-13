#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace tuide::monitor_log {

void set_enabled(bool on);
bool enabled();
void heartbeat();
void event(std::string_view category, std::string_view message);

// Última acción de usuario, anotada antes de ejecutar el handler.
void note_user_action(std::string_view action, std::string_view key, std::string_view focus);

// Última orden DAP. `queued` en el push; `started`/`finished` alrededor del worker.
void note_dap_queued(std::string_view kind, std::string_view detail = {});
void note_dap_started(std::string_view kind);
void note_dap_finished(std::string_view kind);

std::string log_path();
std::string flight_path();

// Pruebas. El directorio solo se aplica con el monitor apagado.
void set_storage_directory(std::string directory);
void set_background_trim(bool on);
void trim_once();
void test_set_heartbeat_age(std::chrono::milliseconds age);
void test_append_line(std::string line);
void test_rewind_inflight(std::chrono::milliseconds age);

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
  uint64_t id_ = 0;
  bool hot_ = false;
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
