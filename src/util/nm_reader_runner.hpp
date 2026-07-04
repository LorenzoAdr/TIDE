#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "util/nm_reader.hpp"

namespace tgdb {

class NmReaderRunner {
 public:
  NmReaderRunner();
  ~NmReaderRunner();

  NmReaderRunner(const NmReaderRunner&) = delete;
  NmReaderRunner& operator=(const NmReaderRunner&) = delete;

  void start(const std::string& binary_path);
  void cancel();
  bool running() const;
  bool poll(NmReadResult* result);

 private:
  void worker_main(uint64_t generation, std::string binary_path);

  mutable std::mutex mutex_;
  NmReadResult result_;
  bool finished_ = false;
  bool cancelled_ = false;
  uint64_t ready_generation_ = 0;

  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> generation_{0};
  std::atomic<bool> cancel_requested_{false};
};

}  // namespace tgdb
