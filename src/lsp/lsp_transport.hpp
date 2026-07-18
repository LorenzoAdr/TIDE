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
  bool write_request(int id, const std::string& method, nlohmann::json params);
  bool write_response(const nlohmann::json& id, nlohmann::json result);
  bool wait_response(int id, int timeout_ms, nlohmann::json* out);
  void send_cancel(int id);
  void send_notification(const std::string& method, nlohmann::json params);

  using ResponseAcceptanceFilter = std::function<bool(int response_id)>;
  void set_response_acceptance_filter(ResponseAcceptanceFilter filter);

  using NotificationHandler =
      std::function<void(const std::string& method, const nlohmann::json& params)>;
  void set_notification_handler(NotificationHandler handler);
  void set_reader_eof_handler(std::function<void()> handler);

  bool is_running() const { return running_.load(); }

 private:
  enum class ReadFailKind { None, Eof, Malformed };

  bool write_message(const std::string& payload);
  std::optional<std::string> read_message(ReadFailKind* fail_kind = nullptr);
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

  std::mutex handler_mutex_;
  NotificationHandler notification_handler_;
  std::mutex eof_handler_mutex_;
  std::function<void()> reader_eof_handler_;
  std::mutex response_filter_mutex_;
  ResponseAcceptanceFilter response_acceptance_filter_;
};

}  // namespace tgdb