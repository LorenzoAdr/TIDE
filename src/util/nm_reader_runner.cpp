#include "util/nm_reader_runner.hpp"

#include <sstream>
#include <thread>

#include "util/thread_name.hpp"

namespace tgdb {

NmReaderRunner::NmReaderRunner() = default;

NmReaderRunner::~NmReaderRunner() {
  cancel();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void NmReaderRunner::start(const std::string& binary_path) {
  cancel_requested_ = false;
  const uint64_t gen = ++generation_;
  if (worker_.joinable()) {
    worker_.detach();
  }
  running_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ready_generation_ = 0;
  }
  worker_ = std::thread([this, gen, binary_path] {
    set_current_thread_name("nm-read");
    worker_main(gen, binary_path);
  });
}

void NmReaderRunner::cancel() {
  cancel_requested_ = true;
  generation_.fetch_add(1);
}

bool NmReaderRunner::running() const {
  return running_.load();
}

bool NmReaderRunner::poll(NmReadResult* result) {
  if (result == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (ready_generation_ == 0) {
    return false;
  }
  *result = std::move(result_);
  result_ = {};
  ready_generation_ = 0;
  return true;
}

void NmReaderRunner::worker_main(uint64_t generation, std::string binary_path) {
  NmReadResult local;
  if (!cancel_requested_.load() && generation == generation_.load()) {
    local = read_binary_symbols(binary_path);
  } else {
    local.error = "Cancelado";
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation == generation_.load()) {
      result_ = std::move(local);
      ready_generation_ = generation;
    }
  }
  running_ = false;
}

}  // namespace tgdb
