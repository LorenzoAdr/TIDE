#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace tuide {

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
  // Block until every message enqueued so far has been written to the pipe
  // (or timeout / stop). Used before wait_response so large didChange payloads
  // cannot burn the RPC timeout while still sitting in the outbound queue.
  bool flush_writes(int timeout_ms);
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

  struct OutboundMessage {
    std::string payload;
    uint64_t seq = 0;
  };

  // Enqueue JSON payload for the writer thread. Returns the assigned sequence
  // number, or 0 on failure. Callers no longer block on ::write.
  uint64_t enqueue_message(std::string payload);
  // Same, but ahead of notifications already queued. Used for server→client
  // replies so a large didOpen cannot stall clangd waiting on that reply.
  uint64_t enqueue_message_front(std::string payload);
  bool write_all(const char* data, std::size_t len);
  bool write_message(const std::string& payload);
  bool write_bytes(const std::string& payload);
  std::optional<std::string> read_message(ReadFailKind* fail_kind = nullptr);
  void reader_loop();
  void writer_loop();

  int stdin_fd_ = -1;
  int stdout_fd_ = -1;
  std::thread reader_;
  std::thread writer_;
  std::atomic<bool> running_{false};

  std::mutex io_mutex_;
  std::mutex write_queue_mutex_;
  std::condition_variable write_cv_;
  std::condition_variable write_progress_cv_;
  std::deque<OutboundMessage> outbound_;
  uint64_t next_write_seq_ = 1;
  uint64_t last_written_seq_ = 0;

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

}  // namespace tuide
