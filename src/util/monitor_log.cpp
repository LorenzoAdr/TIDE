#include "util/monitor_log.hpp"

#include "util/thread_name.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace tuide::monitor_log {

namespace {

constexpr double kRetentionSeconds = 1.0;
constexpr double kSlowRetentionSeconds = 30.0;
constexpr double kEpisodeRetentionSeconds = 300.0;
constexpr auto kTrimInterval = std::chrono::milliseconds(200);
constexpr auto kFreezeThreshold = std::chrono::milliseconds(500);
constexpr auto kStuckThreshold = std::chrono::milliseconds(2000);
constexpr auto kFlightRefresh = std::chrono::seconds(1);
constexpr std::int64_t kSlowThresholdUs = 16'000;
constexpr std::size_t kDetailLimit = 120;

constexpr const char* kConfigDir = ".config/tuide";
constexpr const char* kLogFile = "monitor.log";
constexpr const char* kFlightFile = "monitor.flight";

struct OpenScope {
  uint64_t id = 0;
  std::string category;
  std::string name;
  std::string thread;
  std::chrono::steady_clock::time_point start;
  bool hot = false;
};

struct UserAnchor {
  bool set = false;
  std::string action;
  std::string key;
  std::string focus;
  std::string at;
};

struct DapSlot {
  uint64_t gen = 0;
  std::string kind;
  std::string detail;
  std::string at;
};

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_background_trim{true};
std::atomic<bool> g_trim_stop{false};
std::atomic<std::chrono::steady_clock::time_point::rep> g_last_heartbeat{
    std::chrono::steady_clock::now().time_since_epoch().count()};

std::mutex g_mu;
int g_fd = -1;
bool g_rewriting = false;
bool g_pending_sync = false;
bool g_freeze_latched = false;
bool g_flight_dirty = false;
std::chrono::steady_clock::time_point g_freeze_since{};
std::chrono::steady_clock::time_point g_last_flight{};
std::string g_storage_dir;
std::vector<std::string> g_ring;
std::vector<std::string> g_pending;
std::vector<OpenScope> g_inflight;
UserAnchor g_user;
DapSlot g_queued;
DapSlot g_running;
bool g_running_open = false;
std::string g_running_end_at;
uint64_t g_next_scope_id = 0;
uint64_t g_dap_seq = 0;
std::thread g_trim_thread;

std::string config_dir_locked() {
  if (!g_storage_dir.empty()) {
    return g_storage_dir;
  }
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    return {};
  }
  return std::string(home) + "/" + kConfigDir;
}

std::string log_path_locked() {
  const std::string dir = config_dir_locked();
  if (dir.empty()) {
    return {};
  }
  return dir + "/" + kLogFile;
}

std::string flight_path_locked() {
  const std::string dir = config_dir_locked();
  if (dir.empty()) {
    return {};
  }
  return dir + "/" + kFlightFile;
}

std::string format_timestamp(std::chrono::system_clock::time_point tp) {
  const auto time = std::chrono::system_clock::to_time_t(tp);
  const auto us =
      std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count() %
      1'000'000;

  std::tm local_tm{};
  localtime_r(&time, &local_tm);

  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%06d",
                local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday, local_tm.tm_hour,
                local_tm.tm_min, local_tm.tm_sec, static_cast<int>(us));

  std::ostringstream out;
  out << buf;
  const int offset_min = local_tm.tm_gmtoff / 60;
  const int offset_hour = offset_min / 60;
  const int offset_rem = std::abs(offset_min % 60);
  out << (offset_min >= 0 ? '+' : '-');
  out << std::setw(2) << std::setfill('0') << std::abs(offset_hour);
  out << std::setw(2) << std::setfill('0') << offset_rem;
  return out.str();
}

std::string format_age(std::chrono::milliseconds age) {
  if (age.count() < 0) {
    age = std::chrono::milliseconds(0);
  }
  if (age.count() >= 1000) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << (static_cast<double>(age.count()) / 1000.0)
        << 's';
    return out.str();
  }
  return std::to_string(age.count()) + "ms";
}

std::string current_thread_name() {
#if defined(__linux__) || defined(__APPLE__)
  char name[16] = {};
  if (pthread_getname_np(pthread_self(), name, sizeof(name)) == 0 && name[0] != '\0') {
    return name;
  }
#endif
  return "thread";
}

std::string sanitize_field(std::string_view value) {
  std::string out;
  out.reserve(std::min(value.size(), kDetailLimit));
  for (const char c : value) {
    if (out.size() >= kDetailLimit) {
      break;
    }
    if (c == '\n' || c == '\r' || c == '\t') {
      out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool parse_timestamp_prefix(const std::string& line, std::chrono::system_clock::time_point* out) {
  if (line.size() < 26 || line[4] != '-' || line[7] != '-' || line[10] != 'T') {
    return false;
  }

  std::tm local_tm{};
  int micros = 0;
  const int matched = std::sscanf(line.c_str(), "%d-%d-%dT%d:%d:%d.%d", &local_tm.tm_year,
                                  &local_tm.tm_mon, &local_tm.tm_mday, &local_tm.tm_hour,
                                  &local_tm.tm_min, &local_tm.tm_sec, &micros);
  if (matched < 6) {
    return false;
  }
  local_tm.tm_year -= 1900;
  local_tm.tm_mon -= 1;
  local_tm.tm_isdst = -1;

  const std::time_t seconds = mktime(&local_tm);
  if (seconds == static_cast<std::time_t>(-1)) {
    return false;
  }

  *out = std::chrono::system_clock::from_time_t(seconds) + std::chrono::microseconds(micros);
  return true;
}

std::optional<uint64_t> parse_start_id(const std::string& line) {
  const auto pos = line.rfind(" start id=");
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  const char* begin = line.c_str() + pos + std::strlen(" start id=");
  char* end = nullptr;
  const unsigned long long value = std::strtoull(begin, &end, 10);
  if (end == begin) {
    return std::nullopt;
  }
  return static_cast<uint64_t>(value);
}

std::string format_duration(std::chrono::microseconds dur) {
  const auto us = dur.count();
  std::ostringstream out;
  out << "dur=";
  if (us >= 1'000) {
    out << std::fixed << std::setprecision(2) << (static_cast<double>(us) / 1'000.0) << "ms";
  } else {
    out << us << "us";
  }
  if (us >= kSlowThresholdUs) {
    out << " SLOW";
  }
  return out.str();
}

bool is_hot_scope(std::string_view category, std::string_view name) {
  if (category == "ui" && name.size() >= 5 && name.substr(0, 5) == "tick.") {
    return true;
  }
  return name == "tick_callback";
}

std::chrono::milliseconds heartbeat_age_locked() {
  const auto last = std::chrono::steady_clock::time_point{
      std::chrono::steady_clock::duration{g_last_heartbeat.load(std::memory_order_acquire)}};
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                               last);
}

bool heartbeat_frozen_locked() {
  return heartbeat_age_locked() > kFreezeThreshold;
}

bool write_all(int fd, const char* data, std::size_t size) {
  while (size > 0) {
    const ssize_t wrote = ::write(fd, data, size);
    if (wrote < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    data += wrote;
    size -= static_cast<std::size_t>(wrote);
  }
  return true;
}

void close_fd_locked() {
  if (g_fd >= 0) {
    ::close(g_fd);
    g_fd = -1;
  }
}

void write_raw_locked(const std::string& line, bool sync) {
  g_ring.push_back(line);
  if (g_fd >= 0) {
    const std::string out = line + '\n';
    if (write_all(g_fd, out.data(), out.size()) && sync) {
      ::fsync(g_fd);
    }
    return;
  }
  if (g_rewriting) {
    g_pending.push_back(line);
    if (sync) {
      g_pending_sync = true;
    }
  }
}

std::string format_line(std::string_view category, std::string_view message) {
  std::ostringstream out;
  out << format_timestamp(std::chrono::system_clock::now()) << " [" << current_thread_name()
      << "] " << category << ' ' << message;
  return out.str();
}

void write_event_locked(std::string_view category, std::string_view message, bool sync) {
  write_raw_locked(format_line(category, message), sync);
}

std::string render_flight_locked() {
  std::ostringstream out;
  out << "updated: " << format_timestamp(std::chrono::system_clock::now()) << '\n';
  if (!g_enabled.load(std::memory_order_acquire)) {
    out << "ui: OFF\n";
  } else {
    const auto age = heartbeat_age_locked();
    if (age > kFreezeThreshold) {
      out << "ui: FROZEN heartbeat_age=" << format_age(age) << '\n';
    } else {
      out << "ui: ALIVE heartbeat_age=" << format_age(age) << '\n';
    }
  }

  if (!g_user.set) {
    out << "last_user: (ninguna)\n";
  } else {
    out << "last_user: " << g_user.action << " key=" << g_user.key << " focus=" << g_user.focus
        << " at=" << g_user.at << '\n';
  }

  if (g_queued.gen == 0 && !g_running_open) {
    out << "last_dap: (ninguna)\n";
  } else if (g_queued.gen != 0) {
    out << "last_dap: " << g_queued.kind;
    if (!g_queued.detail.empty()) {
      out << " detail=" << g_queued.detail;
    }
    out << " queued at=" << g_queued.at;
    if (g_running.gen == g_queued.gen && g_running.gen != 0) {
      out << " started at=" << g_running.at;
      if (g_running_open) {
        out << " (sin end)";
      } else {
        out << " end at=" << g_running_end_at;
      }
    } else {
      out << " (sin start)";
    }
    out << '\n';
    if (g_running_open && g_running.gen != g_queued.gen) {
      out << "running_dap: " << g_running.kind;
      if (!g_running.detail.empty()) {
        out << " detail=" << g_running.detail;
      }
      out << " started at=" << g_running.at << " (sin end)\n";
    }
  } else if (g_running_open) {
    out << "running_dap: " << g_running.kind << " started at=" << g_running.at << " (sin end)\n";
  }

  out << "inflight:\n";
  if (g_inflight.empty()) {
    out << "  (ninguno)\n";
  } else {
    const auto now = std::chrono::steady_clock::now();
    for (const auto& scope : g_inflight) {
      const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - scope.start);
      out << "  " << scope.category << ' ' << scope.name << " thread=" << scope.thread
          << " age=" << format_age(age);
      if (age >= kStuckThreshold) {
        out << " STUCK";
      }
      out << '\n';
    }
  }
  return out.str();
}

void publish_flight_locked(bool sync) {
  const std::string path = flight_path_locked();
  if (path.empty()) {
    return;
  }
  const std::string body = render_flight_locked();
  const std::string tmp = path + ".tmp";
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    return;
  }
  const bool wrote = write_all(fd, body.data(), body.size());
  if (wrote && sync) {
    ::fsync(fd);
  }
  ::close(fd);
  if (!wrote) {
    ::unlink(tmp.c_str());
    return;
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    ::unlink(tmp.c_str());
  }
  g_flight_dirty = false;
  g_last_flight = std::chrono::steady_clock::now();
}

void maybe_publish_locked() {
  const auto now = std::chrono::steady_clock::now();
  if (!g_flight_dirty && now - g_last_flight < kFlightRefresh) {
    return;
  }
  publish_flight_locked(false);
}

std::unordered_set<uint64_t> open_ids_locked() {
  std::unordered_set<uint64_t> ids;
  ids.reserve(g_inflight.size());
  for (const auto& scope : g_inflight) {
    ids.insert(scope.id);
  }
  return ids;
}

std::vector<std::string> select_kept(const std::vector<std::string>& lines,
                                     const std::unordered_set<uint64_t>& open_ids) {
  const auto now = std::chrono::system_clock::now();
  const auto normal_cutoff =
      now - std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>(kRetentionSeconds));
  const auto slow_cutoff =
      now - std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>(kSlowRetentionSeconds));
  const auto episode_cutoff =
      now - std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>(kEpisodeRetentionSeconds));

  std::vector<std::string> kept;
  kept.reserve(lines.size());
  for (const auto& line : lines) {
    if (line.empty()) {
      continue;
    }
    if (const auto id = parse_start_id(line)) {
      if (open_ids.count(*id) > 0) {
        kept.push_back(line);
        continue;
      }
    }
    std::chrono::system_clock::time_point ts;
    if (!parse_timestamp_prefix(line, &ts)) {
      kept.push_back(line);
      continue;
    }
    if (line.find(" mon FREEZE") != std::string::npos ||
        line.find(" mon RESUME") != std::string::npos) {
      if (ts >= episode_cutoff) {
        kept.push_back(line);
      }
      continue;
    }
    const bool slow = line.find(" SLOW") != std::string::npos;
    const auto cutoff = slow ? slow_cutoff : normal_cutoff;
    if (ts >= cutoff) {
      kept.push_back(line);
    }
  }
  return kept;
}

bool rewrite_log_file(const std::string& path, const std::vector<std::string>& lines) {
  const std::string tmp = path + ".tmp";
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    return false;
  }
  bool ok = true;
  for (const auto& line : lines) {
    const std::string out = line + '\n';
    if (!write_all(fd, out.data(), out.size())) {
      ok = false;
      break;
    }
  }
  ::close(fd);
  if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
    ::unlink(tmp.c_str());
    return false;
  }
  return true;
}

void reopen_log_locked() {
  const std::string path = log_path_locked();
  if (path.empty()) {
    return;
  }
  g_fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (g_fd < 0) {
    return;
  }
  for (const auto& line : g_pending) {
    const std::string out = line + '\n';
    write_all(g_fd, out.data(), out.size());
  }
  if (g_pending_sync) {
    ::fsync(g_fd);
  }
  g_pending.clear();
  g_pending_sync = false;
}

void open_log_locked() {
  const std::string dir = config_dir_locked();
  if (dir.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::string path = log_path_locked();
  g_fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (g_fd < 0) {
    return;
  }
  std::ostringstream header;
  header << "session start pid=" << ::getpid() << " flight=" << flight_path_locked();
  write_event_locked("mon", header.str(), false);
}

void update_freeze_locked() {
  const bool frozen = heartbeat_frozen_locked();
  if (frozen && !g_freeze_latched) {
    g_freeze_latched = true;
    g_freeze_since = std::chrono::steady_clock::now();
    write_event_locked("mon", "FREEZE heartbeat_age=" + format_age(heartbeat_age_locked()), true);
    publish_flight_locked(true);
    return;
  }
  if (!frozen && g_freeze_latched) {
    g_freeze_latched = false;
    const auto after = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_freeze_since);
    write_event_locked("mon", "RESUME after=" + format_age(after), false);
    publish_flight_locked(false);
    return;
  }
  maybe_publish_locked();
}

void clear_anchors_locked() {
  g_inflight.clear();
  g_user = {};
  g_queued = {};
  g_running = {};
  g_running_open = false;
  g_running_end_at.clear();
  g_freeze_latched = false;
  g_flight_dirty = false;
  g_ring.clear();
  g_pending.clear();
  g_pending_sync = false;
}

uint64_t register_scope(std::string category, std::string name, bool* hot) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_enabled.load(std::memory_order_acquire)) {
    return 0;
  }
  OpenScope scope;
  scope.id = ++g_next_scope_id;
  scope.hot = is_hot_scope(category, name);
  scope.category = std::move(category);
  scope.name = std::move(name);
  scope.thread = current_thread_name();
  scope.start = std::chrono::steady_clock::now();
  if (hot != nullptr) {
    *hot = scope.hot;
  }
  g_flight_dirty = true;
  g_inflight.push_back(std::move(scope));
  return g_inflight.back().id;
}

bool unregister_scope(uint64_t id) {
  std::lock_guard<std::mutex> lock(g_mu);
  bool hot = false;
  for (auto it = g_inflight.begin(); it != g_inflight.end(); ++it) {
    if (it->id == id) {
      hot = it->hot;
      g_inflight.erase(it);
      g_flight_dirty = true;
      break;
    }
  }
  return hot;
}

void publish_if_enabled(bool sync) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  publish_flight_locked(sync);
}

void trim_loop() {
  set_current_thread_name("mon-trim");
  while (!g_trim_stop.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(kTrimInterval);
    if (g_trim_stop.load(std::memory_order_acquire)) {
      break;
    }
    if (!g_enabled.load(std::memory_order_acquire)) {
      continue;
    }
    trim_once();
  }
}

void stop_trim_thread() {
  g_trim_stop.store(true, std::memory_order_release);
  if (g_trim_thread.joinable()) {
    g_trim_thread.join();
  }
}

struct TrimThreadAtExit {
  ~TrimThreadAtExit() { stop_trim_thread(); }
};
TrimThreadAtExit g_trim_thread_guard;

}  // namespace

void set_enabled(bool on) {
  if (on == g_enabled.load(std::memory_order_acquire)) {
    return;
  }

  if (on) {
    g_trim_stop.store(false, std::memory_order_release);
    {
      std::lock_guard<std::mutex> lock(g_mu);
      clear_anchors_locked();
      open_log_locked();
      g_last_heartbeat.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                             std::memory_order_release);
      g_enabled.store(true, std::memory_order_release);
      publish_flight_locked(false);
    }
    if (g_background_trim.load(std::memory_order_acquire) && !g_trim_thread.joinable()) {
      g_trim_thread = std::thread(trim_loop);
    }
    return;
  }

  g_enabled.store(false, std::memory_order_release);
  stop_trim_thread();
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_fd >= 0) {
      write_event_locked("mon", "session end", false);
    }
    g_inflight.clear();
    publish_flight_locked(false);
    g_rewriting = false;
    close_fd_locked();
  }
}

bool enabled() {
  return g_enabled.load(std::memory_order_acquire);
}

void heartbeat() {
  g_last_heartbeat.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                         std::memory_order_release);
}

void event(std::string_view category, std::string_view message) {
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  const std::string line = format_line(category, message);
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  write_raw_locked(line, false);
}

void note_user_action(std::string_view action, std::string_view key, std::string_view focus) {
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  const std::string clean_action = sanitize_field(action);
  const std::string clean_key = sanitize_field(key);
  const std::string clean_focus = sanitize_field(focus);
  std::ostringstream message;
  message << "action=" << clean_action << " key=" << clean_key << " focus=" << clean_focus;

  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  g_user.set = true;
  g_user.action = clean_action;
  g_user.key = clean_key;
  g_user.focus = clean_focus;
  g_user.at = format_timestamp(std::chrono::system_clock::now());
  publish_flight_locked(true);
  write_event_locked("ui", message.str(), true);
}

void note_dap_queued(std::string_view kind, std::string_view detail) {
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  const std::string clean_kind = sanitize_field(kind);
  const std::string clean_detail = sanitize_field(detail);
  std::ostringstream message;
  message << "queue kind=" << clean_kind;
  if (!clean_detail.empty()) {
    message << " detail=" << clean_detail;
  }

  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  g_queued.gen = ++g_dap_seq;
  g_queued.kind = clean_kind;
  g_queued.detail = clean_detail;
  g_queued.at = format_timestamp(std::chrono::system_clock::now());
  publish_flight_locked(true);
  write_event_locked("dap", message.str(), true);
}

void note_dap_started(std::string_view kind) {
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  const std::string clean_kind = sanitize_field(kind);
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  g_running.kind = clean_kind;
  g_running.at = format_timestamp(std::chrono::system_clock::now());
  g_running_open = true;
  g_running_end_at.clear();
  if (g_queued.gen != 0 && g_queued.kind == clean_kind) {
    g_running.gen = g_queued.gen;
    g_running.detail = g_queued.detail;
  } else {
    g_running.gen = ++g_dap_seq;
    g_running.detail.clear();
  }
  publish_flight_locked(false);
}

void note_dap_finished(std::string_view kind) {
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  const std::string clean_kind = sanitize_field(kind);
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  if (!g_running_open || g_running.kind != clean_kind) {
    return;
  }
  g_running_open = false;
  g_running_end_at = format_timestamp(std::chrono::system_clock::now());
  publish_flight_locked(false);
}

std::string log_path() {
  std::lock_guard<std::mutex> lock(g_mu);
  return log_path_locked();
}

std::string flight_path() {
  std::lock_guard<std::mutex> lock(g_mu);
  return flight_path_locked();
}

void set_storage_directory(std::string directory) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  g_storage_dir = std::move(directory);
}

void set_background_trim(bool on) {
  g_background_trim.store(on, std::memory_order_release);
}

void trim_once() {
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }

  std::vector<std::string> snapshot;
  std::string path;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_enabled.load(std::memory_order_acquire) || g_rewriting) {
      return;
    }
    update_freeze_locked();
    if (g_freeze_latched) {
      return;
    }
    snapshot = select_kept(g_ring, open_ids_locked());
    g_ring = snapshot;
    path = log_path_locked();
    if (path.empty() || g_fd < 0) {
      return;
    }
    g_rewriting = true;
    close_fd_locked();
  }

  const bool wrote = rewrite_log_file(path, snapshot);

  std::lock_guard<std::mutex> lock(g_mu);
  g_rewriting = false;
  if (!wrote) {
    reopen_log_locked();
    return;
  }
  reopen_log_locked();
}

void test_set_heartbeat_age(std::chrono::milliseconds age) {
  const auto tp = std::chrono::steady_clock::now() - age;
  g_last_heartbeat.store(tp.time_since_epoch().count(), std::memory_order_release);
}

void test_append_line(std::string line) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_enabled.load(std::memory_order_acquire)) {
    return;
  }
  write_raw_locked(std::move(line), false);
}

void test_rewind_inflight(std::chrono::milliseconds age) {
  std::lock_guard<std::mutex> lock(g_mu);
  const auto start = std::chrono::steady_clock::now() - age;
  for (auto& scope : g_inflight) {
    scope.start = start;
  }
  g_flight_dirty = true;
}

MonitorScope::MonitorScope(std::string_view category, std::string_view name)
    : category_(category), name_(name), start_(std::chrono::steady_clock::now()) {
  if (!enabled()) {
    return;
  }
  id_ = register_scope(category_, name_, &hot_);
  if (id_ == 0) {
    return;
  }
  active_ = true;
  event(category_, name_ + " start id=" + std::to_string(id_));
  if (!hot_) {
    publish_if_enabled(false);
  }
}

MonitorScope::~MonitorScope() {
  if (!active_) {
    return;
  }
  const bool hot = unregister_scope(id_);
  if (!enabled()) {
    return;
  }
  const auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start_);
  std::ostringstream msg;
  msg << name_ << " end id=" << id_ << ' ' << format_duration(dur);
  event(category_, msg.str());
  if (!hot) {
    publish_if_enabled(false);
  }
}

}  // namespace tuide::monitor_log
