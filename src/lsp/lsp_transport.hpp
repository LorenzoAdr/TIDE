#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace tgdb {

class LspTransport {
 public:
  LspTransport() = default;
  ~LspTransport();

  bool start(int stdin_write_fd, int stdout_read_fd);
  void stop();

  bool send_request(int id, const std::string& method, nlohmann::json params,
                    int timeout_ms, nlohmann::json* out);
  void send_notification(const std::string& method, nlohmann::json params);

 private:
  bool write_message(const std::string& payload);
  std::optional<std::string> read_message();
  void reader_loop();

  int stdin_fd_ = -1;
  int stdout_fd_ = -1;
  std::thread reader_;
  std::atomic<bool> running_{false};

  std::mutex io_mutex_;
  std::mutex pending_mutex_;
  std::condition_variable pending_cv_;
  std::unordered_map<int, nlohmann::json> pending_responses_;
  int next_id_ = 1;
};

}  // namespace tgdb
