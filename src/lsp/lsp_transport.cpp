#include "lsp/lsp_transport.hpp"

#include <chrono>
#include <cstring>
#include <unistd.h>

namespace tgdb {

LspTransport::~LspTransport() {
  stop();
}

bool LspTransport::start(int stdin_write_fd, int stdout_read_fd) {
  stop();
  if (stdin_write_fd < 0 || stdout_read_fd < 0) {
    return false;
  }
  stdin_fd_ = stdin_write_fd;
  stdout_fd_ = stdout_read_fd;
  running_ = true;
  reader_ = std::thread([this] { reader_loop(); });
  return true;
}

void LspTransport::stop() {
  running_ = false;
  if (reader_.joinable()) {
    reader_.join();
  }
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_responses_.clear();
  }
  pending_cv_.notify_all();
  stdin_fd_ = -1;
  stdout_fd_ = -1;
}

bool LspTransport::write_message(const std::string& payload) {
  if (stdin_fd_ < 0) {
    return false;
  }
  const std::string header =
      "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n";
  std::lock_guard<std::mutex> lock(io_mutex_);
  if (::write(stdin_fd_, header.data(), header.size()) !=
      static_cast<ssize_t>(header.size())) {
    return false;
  }
  if (payload.empty()) {
    return true;
  }
  return ::write(stdin_fd_, payload.data(), payload.size()) ==
         static_cast<ssize_t>(payload.size());
}

std::optional<std::string> LspTransport::read_message() {
  if (stdout_fd_ < 0) {
    return std::nullopt;
  }

  std::string header;
  char ch = 0;
  while (running_.load()) {
    const ssize_t n = ::read(stdout_fd_, &ch, 1);
    if (n <= 0) {
      return std::nullopt;
    }
    header.push_back(ch);
    if (header.size() >= 4 && header.substr(header.size() - 4) == "\r\n\r\n") {
      break;
    }
    if (header.size() > 8192) {
      return std::nullopt;
    }
  }

  std::size_t content_length = 0;
  std::size_t pos = 0;
  while (pos < header.size()) {
    const auto line_end = header.find("\r\n", pos);
    if (line_end == std::string::npos) {
      break;
    }
    const std::string line = header.substr(pos, line_end - pos);
    pos = line_end + 2;
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    while (!value.empty() && value[0] == ' ') {
      value.erase(value.begin());
    }
    if (key == "Content-Length") {
      content_length = static_cast<std::size_t>(std::stoul(value));
    }
  }

  if (content_length == 0) {
    return std::nullopt;
  }

  std::string body;
  body.resize(content_length);
  std::size_t read_total = 0;
  while (read_total < content_length && running_.load()) {
    const ssize_t n = ::read(stdout_fd_, body.data() + read_total,
                             content_length - read_total);
    if (n <= 0) {
      return std::nullopt;
    }
    read_total += static_cast<std::size_t>(n);
  }
  return body;
}

void LspTransport::reader_loop() {
  while (running_.load()) {
    auto message = read_message();
    if (!message) {
      if (running_.load()) {
        running_ = false;
      }
      break;
    }

    nlohmann::json json;
    try {
      json = nlohmann::json::parse(*message);
    } catch (...) {
      continue;
    }

    if (!json.contains("id") || json["id"].is_null()) {
      if (json.contains("method") && json["method"].is_string()) {
        const std::string method = json["method"].get<std::string>();
        nlohmann::json params = json.contains("params") ? json["params"] : nlohmann::json::object();
        NotificationHandler handler;
        {
          std::lock_guard<std::mutex> lock(handler_mutex_);
          handler = notification_handler_;
        }
        if (handler) {
          handler(method, params);
        }
      }
      continue;
    }

    const int id = json["id"].get<int>();
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_responses_[id] = std::move(json);
    pending_cv_.notify_all();
  }
  pending_cv_.notify_all();
}

bool LspTransport::send_request(int id, const std::string& method, nlohmann::json params,
                                int timeout_ms, nlohmann::json* out) {
  if (!running_.load()) {
    return false;
  }

  nlohmann::json request = {{"jsonrpc", "2.0"},
                            {"id", id},
                            {"method", method},
                            {"params", std::move(params)}};

  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_responses_.erase(id);
  }

  if (!write_message(request.dump())) {
    return false;
  }

  std::unique_lock<std::mutex> lock(pending_mutex_);
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (pending_responses_.find(id) == pending_responses_.end() && running_.load()) {
    if (pending_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
      return false;
    }
  }

  const auto it = pending_responses_.find(id);
  if (it == pending_responses_.end()) {
    return false;
  }

  const nlohmann::json response = it->second;
  pending_responses_.erase(it);
  lock.unlock();

  if (response.contains("error")) {
    return false;
  }
  if (!response.contains("result")) {
    return false;
  }
  *out = response["result"];
  return true;
}

void LspTransport::set_notification_handler(NotificationHandler handler) {
  std::lock_guard<std::mutex> lock(handler_mutex_);
  notification_handler_ = std::move(handler);
}

void LspTransport::send_notification(const std::string& method, nlohmann::json params) {
  if (!running_.load()) {
    return;
  }
  nlohmann::json notification = {{"jsonrpc", "2.0"},
                                 {"method", method},
                                 {"params", std::move(params)}};
  write_message(notification.dump());
}

}  // namespace tgdb
