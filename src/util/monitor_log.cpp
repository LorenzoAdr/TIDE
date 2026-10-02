#include "util/monitor_log.hpp"

#include "util/thread_name.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace tuide::monitor_log {

namespace {

constexpr double kRetentionSeconds = 1.0;
constexpr double kSlowRetentionSeconds = 30.0;
constexpr auto kTrimInterval = std::chrono::milliseconds(200);
constexpr auto kFreezeThreshold = std::chrono::milliseconds(500);
constexpr std::int64_t kSlowThresholdUs = 16'000;  // 16 ms — un frame UI a 60 Hz

constexpr const char* kConfigDir = ".config/tuide";
constexpr const char* kLogFile = "monitor.log";

std::atomic<bool> g_enabled{false};
std::atomic<std::chrono::steady_clock::time_point::rep> g_last_heartbeat{
    std::chrono::steady_clock::now().time_since_epoch().count()};

std::mutex g_write_mutex;
std::ofstream g_log;
std::thread g_trim_thread;
std::atomic<bool> g_trim_stop{false};

std::string config_dir_path() {
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    return {};
  }
  return std::string(home) + "/" + kConfigDir;
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
                local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
                local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec,
                static_cast<int>(us));

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

std::string current_thread_name() {
#if defined(__linux__) || defined(__APPLE__)
  char name[16] = {};
  if (pthread_getname_np(pthread_self(), name, sizeof(name)) == 0 && name[0] != '\0') {
    return name;
  }
#endif
  return "thread";
}

bool parse_timestamp_prefix(const std::string& line,
                            std::chrono::system_clock::time_point* out) {
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

  const auto base = std::chrono::system_clock::from_time_t(seconds);
  *out = base + std::chrono::microseconds(micros);
  return true;
}

std::string format_duration(std::chrono::microseconds dur) {
  const auto us = dur.count();
  std::ostringstream out;
  out << "dur=";
  if (us >= 1'000) {
    out << std::fixed << std::setprecision(2)
        << (static_cast<double>(us) / 1'000.0) << "ms";
  } else {
    out << us << "us";
  }
  if (us >= kSlowThresholdUs) {
    out << " SLOW";
  }
  return out.str();
}

void trim_log_file(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return;
  }

  const auto now = std::chrono::system_clock::now();
  const auto normal_cutoff =
      now - std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>(kRetentionSeconds));
  const auto slow_cutoff =
      now - std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>(kSlowRetentionSeconds));

  std::vector<std::string> kept;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    const bool is_slow = line.find(" SLOW") != std::string::npos;
    std::chrono::system_clock::time_point ts;
    if (!parse_timestamp_prefix(line, &ts)) {
      kept.push_back(std::move(line));
      continue;
    }
    const auto cutoff = is_slow ? slow_cutoff : normal_cutoff;
    if (ts >= cutoff) {
      kept.push_back(std::move(line));
    }
  }
  input.close();

  const std::string tmp_path = path + ".tmp";
  {
    std::ofstream output(tmp_path, std::ios::trunc);
    if (!output) {
      return;
    }
    for (const auto& kept_line : kept) {
      output << kept_line << '\n';
    }
  }
  std::rename(tmp_path.c_str(), path.c_str());
}

void trim_loop() {
  set_current_thread_name("mon-trim");
  while (!g_trim_stop.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(kTrimInterval);
    if (!g_enabled.load(std::memory_order_acquire)) {
      continue;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto last_hb = std::chrono::steady_clock::time_point{
        std::chrono::steady_clock::duration{g_last_heartbeat.load(std::memory_order_acquire)}};
    if (now - last_hb > kFreezeThreshold) {
      continue;
    }

    const std::string path = log_path();
    if (path.empty()) {
      continue;
    }

    std::lock_guard<std::mutex> lock(g_write_mutex);
    g_log.flush();
    g_log.close();
    trim_log_file(path);
    g_log.open(path, std::ios::app);
  }
}

void open_log_file() {
  const std::string dir = config_dir_path();
  if (dir.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);

  const std::string path = dir + "/" + kLogFile;
  g_log.open(path, std::ios::app);
  if (!g_log) {
    return;
  }

  std::ostringstream header;
  header << "session start pid=" << getpid();
  const std::string ts = format_timestamp(std::chrono::system_clock::now());
  g_log << ts << " [mon] " << header.str() << '\n';
  g_log.flush();
}

void close_log_file() {
  if (g_log.is_open()) {
    const std::string ts = format_timestamp(std::chrono::system_clock::now());
    g_log << ts << " [mon] session end\n";
    g_log.flush();
    g_log.close();
  }
}

void stop_trim_thread() {
  g_trim_stop.store(true, std::memory_order_release);
  if (g_trim_thread.joinable()) {
    g_trim_thread.join();
  }
}

// Join before ~std::thread runs during process exit (otherwise std::terminate).
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
      std::lock_guard<std::mutex> lock(g_write_mutex);
      open_log_file();
    }
    heartbeat();
    g_enabled.store(true, std::memory_order_release);
    if (!g_trim_thread.joinable()) {
      g_trim_thread = std::thread(trim_loop);
    }
    return;
  }

  g_enabled.store(false, std::memory_order_release);
  stop_trim_thread();
  {
    std::lock_guard<std::mutex> lock(g_write_mutex);
    close_log_file();
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

  const std::string ts = format_timestamp(std::chrono::system_clock::now());
  const std::string thread = current_thread_name();

  std::lock_guard<std::mutex> lock(g_write_mutex);
  if (!g_log.is_open()) {
    return;
  }
  g_log << ts << " [" << thread << "] " << category << ' ' << message << '\n';
  g_log.flush();
}

std::string log_path() {
  const std::string dir = config_dir_path();
  if (dir.empty()) {
    return {};
  }
  return dir + "/" + kLogFile;
}

MonitorScope::MonitorScope(std::string_view category, std::string_view name)
    : category_(category), name_(name), start_(std::chrono::steady_clock::now()) {
  if (!enabled()) {
    return;
  }
  active_ = true;
  event(category_, std::string(name_) + " start");
}

MonitorScope::~MonitorScope() {
  if (!active_) {
    return;
  }
  const auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start_);
  std::ostringstream msg;
  msg << name_ << " end " << format_duration(dur);
  event(category_, msg.str());
}

}  // namespace tuide::monitor_log
