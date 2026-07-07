#include "util/system_stats.hpp"

#include "util/thread_name.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

#include "util/ui_activity_gate.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

constexpr auto kSampleInterval = std::chrono::milliseconds(500);
constexpr auto kFileDumpInterval = std::chrono::milliseconds(100);
constexpr double kMinFrameDtMs = 1.0;

std::string read_file_trimmed(const fs::path& path) {
  std::ifstream input(path);
  if (!input) {
    return "";
  }
  std::string content((std::istreambuf_iterator<char>(input)),
                      std::istreambuf_iterator<char>());
  while (!content.empty() && (content.back() == '\0' || content.back() == '\n')) {
    content.pop_back();
  }
  for (char& c : content) {
    if (c == '\0') {
      c = ' ';
    }
  }
  return content;
}

std::optional<std::size_t> parse_kb_value(const std::string& line) {
  const auto pos = line.find(':');
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  std::string value = line.substr(pos + 1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  std::size_t kb = 0;
  std::istringstream stream(value);
  stream >> kb;
  if (!stream) {
    return std::nullopt;
  }
  return kb;
}

std::optional<std::size_t> read_status_kb(const fs::path& status_path, const char* key) {
  std::ifstream input(status_path);
  if (!input) {
    return std::nullopt;
  }
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind(key, 0) == 0) {
      return parse_kb_value(line);
    }
  }
  return std::nullopt;
}

bool parse_proc_stat_ticks(const fs::path& stat_path, std::uint64_t* utime, std::uint64_t* stime) {
  if (utime == nullptr || stime == nullptr) {
    return false;
  }
  std::ifstream input(stat_path);
  if (!input) {
    return false;
  }
  std::string line;
  if (!std::getline(input, line)) {
    return false;
  }
  const auto close_paren = line.rfind(')');
  if (close_paren == std::string::npos || close_paren + 2 >= line.size()) {
    return false;
  }
  std::istringstream stream(line.substr(close_paren + 2));
  std::string state;
  if (!(stream >> state)) {
    return false;
  }
  std::array<std::uint64_t, 12> fields{};
  for (std::uint64_t& field : fields) {
    if (!(stream >> field)) {
      return false;
    }
  }
  *utime = fields[10];
  *stime = fields[11];
  return true;
}

double cpu_percent_from_delta(std::uint64_t prev_ticks, std::uint64_t curr_ticks,
                              double elapsed_sec, long clock_ticks) {
  if (clock_ticks <= 0 || elapsed_sec <= 0.0) {
    return 0.0;
  }
  const double delta = static_cast<double>(curr_ticks > prev_ticks ? curr_ticks - prev_ticks : 0);
  return 100.0 * delta / (elapsed_sec * static_cast<double>(clock_ticks));
}

int parse_core_index(const std::string& name) {
  if (name.size() <= 3 || name.rfind("cpu", 0) != 0) {
    return -1;
  }
  if (name == "cpu") {
    return -1;
  }
  try {
    return std::stoi(name.substr(3));
  } catch (...) {
    return -1;
  }
}

bool parse_ppid_from_stat(const fs::path& stat_path, int* ppid) {
  if (ppid == nullptr) {
    return false;
  }
  std::ifstream input(stat_path);
  if (!input) {
    return false;
  }
  std::string line;
  if (!std::getline(input, line)) {
    return false;
  }
  const auto close_paren = line.rfind(')');
  if (close_paren == std::string::npos || close_paren + 2 >= line.size()) {
    return false;
  }
  std::istringstream stream(line.substr(close_paren + 2));
  std::string state;
  if (!(stream >> state >> *ppid)) {
    return false;
  }
  return true;
}

std::string friendly_process_name(int pid, const std::string& comm,
                                  const std::string& cmdline) {
  const std::string haystack = comm + " " + cmdline;
  if (comm == "clangd" || comm.rfind("clangd", 0) == 0 ||
      haystack.find("clangd") != std::string::npos) {
    return "clangd";
  }
  if (haystack.find("gdb") != std::string::npos) {
    return "gdb";
  }
  if (haystack.find("bash") != std::string::npos || haystack.find("/sh") != std::string::npos) {
    return "shell";
  }
  if (!comm.empty()) {
    return comm;
  }
  return "pid:" + std::to_string(pid);
}

}  // namespace

#ifdef __linux__

struct CpuTimes {
  std::uint64_t user = 0;
  std::uint64_t nice = 0;
  std::uint64_t system = 0;
  std::uint64_t idle = 0;
  std::uint64_t iowait = 0;
  std::uint64_t irq = 0;
  std::uint64_t softirq = 0;
  std::uint64_t steal = 0;
  std::uint64_t guest = 0;
  std::uint64_t guest_nice = 0;

  std::uint64_t total() const {
    return user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;
  }

  std::uint64_t busy() const { return total() - idle - iowait; }
};

struct ProcessCpuState {
  std::uint64_t utime = 0;
  std::uint64_t stime = 0;
};

struct ThreadCpuState {
  int tid = 0;
  std::string comm;
  std::uint64_t utime = 0;
  std::uint64_t stime = 0;
};

struct ChildProcessCpuState {
  int pid = 0;
  std::string comm;
  std::string display_name;
  std::uint64_t utime = 0;
  std::uint64_t stime = 0;
  std::size_t rss_kb = 0;
  int thread_count = 0;
};

struct SystemCpuState {
  std::unordered_map<std::string, CpuTimes> cores;
};

#endif  // __linux__

struct SamplerStatsState {
#ifdef __linux__
  ProcessCpuState process;
  std::vector<ThreadCpuState> threads;
  std::vector<ChildProcessCpuState> children;
  SystemCpuState system;
#endif
};

namespace {

#ifdef __linux__

bool parse_cpu_line(const std::string& line, CpuTimes* out) {
  if (out == nullptr) {
    return false;
  }
  std::istringstream stream(line);
  std::string label;
  stream >> label;
  if (label.rfind("cpu", 0) != 0) {
    return false;
  }
  stream >> out->user >> out->nice >> out->system >> out->idle >> out->iowait >> out->irq >>
      out->softirq >> out->steal >> out->guest >> out->guest_nice;
  return static_cast<bool>(stream);
}

bool read_process_status(ProcessSample* process) {
  if (process == nullptr) {
    return false;
  }
  const fs::path status_path{"/proc/self/status"};
  if (const auto rss = read_status_kb(status_path, "VmRSS")) {
    process->rss_kb = *rss;
  }
  if (const auto vms = read_status_kb(status_path, "VmSize")) {
    process->vms_kb = *vms;
  }
  if (const auto threads = read_status_kb(status_path, "Threads")) {
    process->thread_count = static_cast<int>(*threads);
  }
  return true;
}

bool read_system_meminfo(SystemSample* system) {
  if (system == nullptr) {
    return false;
  }
  std::ifstream input("/proc/meminfo");
  if (!input) {
    return false;
  }
  std::size_t mem_total = 0;
  std::size_t mem_available = 0;
  std::size_t swap_total = 0;
  std::size_t swap_free = 0;
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind("MemTotal:", 0) == 0) {
      if (const auto value = parse_kb_value(line)) {
        mem_total = *value;
      }
    } else if (line.rfind("MemAvailable:", 0) == 0) {
      if (const auto value = parse_kb_value(line)) {
        mem_available = *value;
      }
    } else if (line.rfind("SwapTotal:", 0) == 0) {
      if (const auto value = parse_kb_value(line)) {
        swap_total = *value;
      }
    } else if (line.rfind("SwapFree:", 0) == 0) {
      if (const auto value = parse_kb_value(line)) {
        swap_free = *value;
      }
    }
  }
  system->mem_total_kb = mem_total;
  system->mem_used_kb = mem_total > mem_available ? mem_total - mem_available : 0;
  system->swap_total_kb = swap_total;
  system->swap_used_kb = swap_total > swap_free ? swap_total - swap_free : 0;
  return true;
}

bool read_system_cpu(SystemCpuState* state) {
  if (state == nullptr) {
    return false;
  }
  std::ifstream input("/proc/stat");
  if (!input) {
    return false;
  }
  state->cores.clear();
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind("cpu", 0) != 0) {
      break;
    }
    CpuTimes times;
    if (!parse_cpu_line(line, &times)) {
      continue;
    }
    std::istringstream stream(line);
    std::string name;
    stream >> name;
    state->cores.emplace(name, times);
  }
  return !state->cores.empty();
}

bool read_process_cpu(ProcessCpuState* state) {
  if (state == nullptr) {
    return false;
  }
  return parse_proc_stat_ticks("/proc/self/stat", &state->utime, &state->stime);
}

std::string read_thread_comm(const fs::path& task_dir) {
  std::string comm = read_file_trimmed(task_dir / "comm");
  if (comm != "tgdb") {
    return comm;
  }
  std::ifstream input(task_dir / "status");
  if (!input) {
    return comm;
  }
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind("Name:", 0) != 0) {
      continue;
    }
    std::string name = line.substr(5);
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) {
      name.erase(name.begin());
    }
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
      name.pop_back();
    }
    if (!name.empty() && name != "tgdb") {
      return name;
    }
    break;
  }
  return comm;
}

bool read_thread_cpus(std::vector<ThreadCpuState>* threads) {
  if (threads == nullptr) {
    return false;
  }
  threads->clear();
  const fs::path task_dir{"/proc/self/task"};
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(task_dir, ec)) {
    if (ec) {
      return false;
    }
    if (!entry.is_directory(ec)) {
      continue;
    }
    const std::string tid_str = entry.path().filename().string();
    int tid = 0;
    try {
      tid = std::stoi(tid_str);
    } catch (...) {
      continue;
    }
    ThreadCpuState thread;
    thread.tid = tid;
    thread.comm = read_thread_comm(entry.path());
    if (!parse_proc_stat_ticks(entry.path() / "stat", &thread.utime, &thread.stime)) {
      continue;
    }
    threads->push_back(std::move(thread));
  }
  return !threads->empty();
}

bool read_child_process_stats(int pid, ChildProcessCpuState* out) {
  if (out == nullptr || pid <= 0) {
    return false;
  }
  const fs::path proc_dir = fs::path("/proc") / std::to_string(pid);
  out->pid = pid;
  out->comm = read_file_trimmed(proc_dir / "comm");
  const std::string cmdline = read_file_trimmed(proc_dir / "cmdline");
  out->display_name = friendly_process_name(pid, out->comm, cmdline);
  if (const auto rss = read_status_kb(proc_dir / "status", "VmRSS")) {
    out->rss_kb = *rss;
  }
  if (const auto threads = read_status_kb(proc_dir / "status", "Threads")) {
    out->thread_count = static_cast<int>(*threads);
  }
  return parse_proc_stat_ticks(proc_dir / "stat", &out->utime, &out->stime);
}

bool read_child_process_cpus(std::vector<ChildProcessCpuState>* children) {
  if (children == nullptr) {
    return false;
  }
  children->clear();
  std::unordered_set<int> seen;
  const fs::path task_root{"/proc/self/task"};
  std::error_code ec;
  for (const auto& task : fs::directory_iterator(task_root, ec)) {
    if (ec || !task.is_directory(ec)) {
      continue;
    }
    std::ifstream input(task.path() / "children");
    if (!input) {
      continue;
    }
    int pid = 0;
    while (input >> pid) {
      if (pid <= 0 || seen.count(pid) > 0) {
        continue;
      }
      seen.insert(pid);
      ChildProcessCpuState child;
      if (read_child_process_stats(pid, &child)) {
        children->push_back(std::move(child));
      }
    }
  }
  if (!children->empty()) {
    return true;
  }

  // Fallback: direct children by ppid (more expensive; used when children files are empty).
  const int self_pid = getpid();
  const fs::path proc_root{"/proc"};
  for (const auto& entry : fs::directory_iterator(proc_root, ec)) {
    if (ec || !entry.is_directory(ec)) {
      continue;
    }
    const std::string pid_str = entry.path().filename().string();
    if (pid_str.empty() || !std::isdigit(static_cast<unsigned char>(pid_str[0]))) {
      continue;
    }
    int pid = 0;
    try {
      pid = std::stoi(pid_str);
    } catch (...) {
      continue;
    }
    if (pid <= 0 || seen.count(pid) > 0) {
      continue;
    }
    int ppid = 0;
    if (!parse_ppid_from_stat(entry.path() / "stat", &ppid) || ppid != self_pid) {
      continue;
    }
    seen.insert(pid);
    ChildProcessCpuState child;
    if (!read_child_process_stats(pid, &child)) {
      continue;
    }
    children->push_back(std::move(child));
  }
  return true;
}

bool comm_is_clangd(const std::string& comm) {
  return comm == "clangd" || comm.rfind("clangd", 0) == 0;
}

int worker_display_rank(const ThreadSample& sample) {
  if (sample.is_child_process) {
    if (comm_is_clangd(sample.comm)) {
      return 0;
    }
    if (sample.comm == "gdb") {
      return 1;
    }
    if (sample.comm == "shell") {
      return 2;
    }
    return 3;
  }
  static constexpr const char* kNamedThreads[] = {
      "ui-main",   "ui-poller", "perf-sampler", "lsp-async", "lsp-read", "lsp-start",
      "idx-work",  "shell-read", "shell-boot",   "idx-syms",  "dap-wrk",
  };
  for (int i = 0; i < static_cast<int>(std::size(kNamedThreads)); ++i) {
    if (sample.comm == kNamedThreads[i]) {
      return 10 + i;
    }
  }
  if (sample.comm == "tgdb") {
    return 100;
  }
  return 50;
}

void sort_worker_samples(std::vector<ThreadSample>* workers) {
  if (workers == nullptr) {
    return;
  }
  std::sort(workers->begin(), workers->end(), [](const ThreadSample& a, const ThreadSample& b) {
    const int rank_a = worker_display_rank(a);
    const int rank_b = worker_display_rank(b);
    if (rank_a != rank_b) {
      return rank_a < rank_b;
    }
    if (a.cpu_percent != b.cpu_percent) {
      return a.cpu_percent > b.cpu_percent;
    }
    return a.tid < b.tid;
  });
}

void append_child_sample(const ChildProcessCpuState& child, SamplerStatsState* prev,
                         double elapsed_sec, long clock_ticks,
                         std::vector<ThreadSample>* out) {
  if (out == nullptr) {
    return;
  }
  bool found_prev = false;
  std::uint64_t prev_ticks = 0;
  if (prev != nullptr) {
    for (const ChildProcessCpuState& prev_child : prev->children) {
      if (prev_child.pid == child.pid) {
        prev_ticks = prev_child.utime + prev_child.stime;
        found_prev = true;
        break;
      }
    }
  }
  ThreadSample sample;
  sample.tid = child.pid;
  sample.comm = child.display_name;
  sample.is_child_process = true;
  sample.rss_kb = child.rss_kb;
  sample.child_thread_count = child.thread_count;
  sample.cpu_percent = found_prev
                           ? cpu_percent_from_delta(prev_ticks, child.utime + child.stime,
                                                    elapsed_sec, clock_ticks)
                           : 0.0;
  out->push_back(std::move(sample));
}

void append_thread_sample(const ThreadCpuState& thread, SamplerStatsState* prev,
                          double elapsed_sec, long clock_ticks, std::vector<ThreadSample>* out) {
  if (out == nullptr) {
    return;
  }
  bool found_prev = false;
  std::uint64_t prev_ticks = 0;
  if (prev != nullptr) {
    for (const ThreadCpuState& prev_thread : prev->threads) {
      if (prev_thread.tid == thread.tid) {
        prev_ticks = prev_thread.utime + prev_thread.stime;
        found_prev = true;
        break;
      }
    }
  }
  ThreadSample sample;
  sample.tid = thread.tid;
  sample.comm = thread.comm;
  sample.is_child_process = false;
  sample.cpu_percent = found_prev
                           ? cpu_percent_from_delta(prev_ticks, thread.utime + thread.stime,
                                                    elapsed_sec, clock_ticks)
                           : 0.0;
  out->push_back(std::move(sample));
}

void populate_worker_samples(ProcessSample* process, SamplerStatsState* prev,
                             SamplerStatsState* curr, double elapsed_sec, long clock_ticks) {
  if (process == nullptr || curr == nullptr) {
    return;
  }
  process->threads.clear();
  for (const ChildProcessCpuState& child : curr->children) {
    append_child_sample(child, prev, elapsed_sec, clock_ticks, &process->threads);
  }
  for (const ThreadCpuState& thread : curr->threads) {
    append_thread_sample(thread, prev, elapsed_sec, clock_ticks, &process->threads);
  }
  sort_worker_samples(&process->threads);
  if (process->thread_count <= 0) {
    process->thread_count = static_cast<int>(curr->threads.size());
  }
}

void update_stats(PerformanceSnapshot* snapshot, SamplerStatsState* prev, SamplerStatsState* curr,
                  double elapsed_sec, long clock_ticks) {
  if (snapshot == nullptr || prev == nullptr || curr == nullptr) {
    return;
  }

  read_process_status(&snapshot->process);
  read_system_meminfo(&snapshot->system);

  const std::uint64_t prev_proc = prev->process.utime + prev->process.stime;
  const std::uint64_t curr_proc = curr->process.utime + curr->process.stime;
  snapshot->process.cpu_percent =
      cpu_percent_from_delta(prev_proc, curr_proc, elapsed_sec, clock_ticks);

  populate_worker_samples(&snapshot->process, prev, curr, elapsed_sec, clock_ticks);

  snapshot->system.cores.clear();
  double total_prev_busy = 0.0;
  double total_curr_busy = 0.0;
  double total_prev_all = 0.0;
  double total_curr_all = 0.0;
  for (const auto& [name, times] : curr->system.cores) {
    const auto prev_it = prev->system.cores.find(name);
    if (prev_it == prev->system.cores.end()) {
      continue;
    }
    const CpuTimes& prev_times = prev_it->second;
    const double prev_total = static_cast<double>(prev_times.total());
    const double curr_total = static_cast<double>(times.total());
    const double prev_idle = static_cast<double>(prev_times.idle + prev_times.iowait);
    const double curr_idle = static_cast<double>(times.idle + times.iowait);
    const double delta_total = std::max(0.0, curr_total - prev_total);
    const double delta_idle = std::max(0.0, curr_idle - prev_idle);
    double usage = 0.0;
    if (delta_total > 0.0) {
      usage = 100.0 * (1.0 - delta_idle / delta_total);
    }
    if (name != "cpu") {
      CpuCoreSample core_sample;
      core_sample.name = name;
      core_sample.usage_percent = usage;
      snapshot->system.cores.push_back(std::move(core_sample));
    }
    total_prev_busy += static_cast<double>(prev_times.busy());
    total_curr_busy += static_cast<double>(times.busy());
    total_prev_all += prev_total;
    total_curr_all += curr_total;
  }
  std::sort(snapshot->system.cores.begin(), snapshot->system.cores.end(),
            [](const CpuCoreSample& a, const CpuCoreSample& b) {
              return parse_core_index(a.name) < parse_core_index(b.name);
            });
  snapshot->system.core_count = static_cast<int>(snapshot->system.cores.size());
  if (total_curr_all > total_prev_all) {
    const double delta_busy = total_curr_busy - total_prev_busy;
    const double delta_all = total_curr_all - total_prev_all;
    snapshot->system.cpu_total_percent = 100.0 * delta_busy / delta_all;
  } else {
    snapshot->system.cpu_total_percent = 0.0;
  }
  snapshot->system.available = true;
  snapshot->stats_available = true;
}

bool sample_linux_stats(PerformanceSnapshot* snapshot, SamplerStatsState* prev,
                        SamplerStatsState* curr, double elapsed_sec) {
  const long clock_ticks = sysconf(_SC_CLK_TCK);
  if (clock_ticks <= 0) {
    return false;
  }
  read_process_cpu(&curr->process);
  const bool has_threads = read_thread_cpus(&curr->threads);
  read_child_process_cpus(&curr->children);
  const bool has_system = read_system_cpu(&curr->system);
  if (!has_threads) {
    return false;
  }
  update_stats(snapshot, prev, curr, elapsed_sec, clock_ticks);
  if (!has_system) {
    snapshot->system.available = false;
  }
  *prev = *curr;
  return true;
}

#endif  // __linux__

}  // namespace

void FpsCounter::on_frame() {
  const auto now = std::chrono::steady_clock::now();
  if (has_sample_) {
    const double dt_ms = std::chrono::duration<double, std::milli>(now - last_frame_).count();
    if (dt_ms >= kMinFrameDtMs) {
      const double instant_fps = 1000.0 / dt_ms;
      fps_ = fps_ <= 0.0 ? instant_fps : fps_ * 0.8 + instant_fps * 0.2;
    }
  } else {
    has_sample_ = true;
  }
  last_frame_ = now;
}

PerformanceSampler::PerformanceSampler() {
  stats_state_ = std::make_unique<SamplerStatsState>();
  sampler_running_.store(true, std::memory_order_release);
  sampler_thread_ = std::thread([this] { sampler_loop(); });
}

PerformanceSampler::~PerformanceSampler() {
  sampler_running_.store(false, std::memory_order_release);
  if (sampler_thread_.joinable()) {
    sampler_thread_.join();
  }
}

PerformanceSnapshot PerformanceSampler::snapshot() const {
  std::lock_guard<std::mutex> lock(snapshot_mutex_);
  return snapshot_;
}

void PerformanceSampler::set_worker_sampling_enabled(bool enabled) {
  sampling_enabled_.store(enabled, std::memory_order_release);
}

void PerformanceSampler::set_file_dump_enabled(bool enabled) {
  file_dump_enabled_.store(enabled, std::memory_order_release);
}

bool PerformanceSampler::file_dump_enabled() const {
  return file_dump_enabled_.load(std::memory_order_acquire);
}

void PerformanceSampler::set_dump_hooks(const UiActivityGate* activity_gate,
                                        const std::atomic<uint64_t>* ui_paint_count,
                                        const std::atomic<uint64_t>* ui_custom_tick) {
  activity_gate_ = activity_gate;
  ui_paint_count_ = ui_paint_count;
  ui_custom_tick_ = ui_custom_tick;
}

std::string PerformanceSampler::dump_file_path() const {
  if (!file_dump_enabled_.load(std::memory_order_acquire)) {
    return {};
  }
  return dump_path_;
}

void PerformanceSampler::open_dump_file() {
#ifdef __linux__
  dump_path_ = "/tmp/tgdb-perf-" + std::to_string(getpid()) + ".log";
#else
  dump_path_ = "/tmp/tgdb-perf.log";
#endif
  std::ofstream out(dump_path_, std::ios::trunc);
  if (!out) {
    dump_path_.clear();
    return;
  }
  out << "# tgdb perf dump interval_ms=100 columns:\n";
  out << "# ts_ms phase proc_cpu% proc_rss_kb paints ticks threads(tid:name:cpu% ...)\n";
}

void PerformanceSampler::append_dump_line(const PerformanceSnapshot& snap, double elapsed_sec) {
  if (dump_path_.empty()) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  const auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now.time_since_epoch())
                         .count();
  UiActivityPhase phase = UiActivityPhase::kGraceWindow;
  if (activity_gate_ != nullptr) {
    phase = activity_gate_->phase();
  }
  const uint64_t paints =
      ui_paint_count_ != nullptr ? ui_paint_count_->load(std::memory_order_relaxed) : 0;
  const uint64_t ticks =
      ui_custom_tick_ != nullptr ? ui_custom_tick_->load(std::memory_order_relaxed) : 0;

  std::ostringstream line;
  line << ts_ms << '\t' << ui_activity_phase_label(phase) << '\t'
       << std::fixed << std::setprecision(1) << snap.process.cpu_percent << '\t'
       << snap.process.rss_kb << '\t' << paints << '\t' << ticks << '\t';
  bool first = true;
  for (const ThreadSample& thread : snap.process.threads) {
    if (!first) {
      line << ' ';
    }
    first = false;
    line << thread.tid << ':' << thread.comm << ':' << std::fixed << std::setprecision(1)
         << thread.cpu_percent;
  }
  line << '\n';

  std::ofstream out(dump_path_, std::ios::app);
  if (out) {
    out << line.str();
  }
}

void PerformanceSampler::on_frame(bool /*sample_workers*/) {
  fps_counter_.on_frame();
  std::lock_guard<std::mutex> lock(snapshot_mutex_);
  snapshot_.fps = fps_counter_.fps();
}

void PerformanceSampler::sampler_loop() {
  set_current_thread_name("perf-sampler");

  auto last_sample = std::chrono::steady_clock::time_point{};
  auto last_ui_publish = std::chrono::steady_clock::time_point{};

  while (sampler_running_.load(std::memory_order_acquire)) {
    const auto now = std::chrono::steady_clock::now();
    const bool dump_enabled = file_dump_enabled_.load(std::memory_order_acquire);
    if (dump_enabled && dump_path_.empty()) {
      open_dump_file();
      last_sample = {};
    } else if (!dump_enabled && !dump_path_.empty()) {
      dump_path_.clear();
      last_sample = {};
    }

    if (!dump_enabled) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    if (last_sample.time_since_epoch().count() == 0) {
      last_sample = now;
      {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        read_process_cpu(&stats_state_->process);
        read_thread_cpus(&stats_state_->threads);
        read_child_process_cpus(&stats_state_->children);
        read_system_cpu(&stats_state_->system);
        read_process_status(&snapshot_.process);
        read_system_meminfo(&snapshot_.system);
        populate_worker_samples(&snapshot_.process, nullptr, stats_state_.get(), 0.0, 1);
        snapshot_.stats_available = true;
        snapshot_.system.available = true;
      }
      std::this_thread::sleep_for(kFileDumpInterval);
      continue;
    }

    const auto since_sample = now - last_sample;
    if (since_sample >= kFileDumpInterval) {
      const double elapsed_sec = std::chrono::duration<double>(since_sample).count();
      PerformanceSnapshot sample;
      SamplerStatsState curr{};
      {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        sample_linux_stats(&sample, stats_state_.get(), &curr, elapsed_sec);
        if (sampling_enabled_.load(std::memory_order_acquire)) {
          const auto since_ui = now - last_ui_publish;
          if (last_ui_publish.time_since_epoch().count() == 0 || since_ui >= kSampleInterval) {
            snapshot_ = sample;
            last_ui_publish = now;
          }
        }
      }
      if (!dump_path_.empty()) {
        append_dump_line(sample, elapsed_sec);
      }
      last_sample = now;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

}  // namespace tgdb
