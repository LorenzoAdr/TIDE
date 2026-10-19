#include "terminal/shell_session_manager.hpp"

#include <utility>

namespace tuide {

ShellSessionManager::ShellSessionManager() {
  sessions_.push_back(std::make_unique<ShellSession>());
}

int ShellSessionManager::count() const { return static_cast<int>(sessions_.size()); }

int ShellSessionManager::active_index() const { return active_; }

void ShellSessionManager::set_active_index(int index) {
  if (index < 0 || index >= count()) {
    return;
  }
  active_ = index;
}

ShellSession* ShellSessionManager::session_at(int index) {
  if (index < 0 || index >= count()) {
    return nullptr;
  }
  return sessions_[static_cast<std::size_t>(index)].get();
}

ShellSession* ShellSessionManager::active_session() { return session_at(active_); }

int ShellSessionManager::add_session() {
  sessions_.push_back(std::make_unique<ShellSession>());
  active_ = count() - 1;
  return active_;
}

void ShellSessionManager::close_session(int index) {
  if (index < 0 || index >= count()) {
    return;
  }
  sessions_[static_cast<std::size_t>(index)]->stop();
  sessions_.erase(sessions_.begin() + index);
  if (sessions_.empty()) {
    sessions_.push_back(std::make_unique<ShellSession>());
    active_ = 0;
    return;
  }
  if (active_ > index) {
    --active_;
  } else if (active_ >= count()) {
    active_ = count() - 1;
  }
}

void ShellSessionManager::move_session(int from, int to) {
  const int n = count();
  if (from < 0 || from >= n || to < 0 || to >= n || from == to) {
    return;
  }
  auto moved = std::move(sessions_[static_cast<std::size_t>(from)]);
  sessions_.erase(sessions_.begin() + from);
  sessions_.insert(sessions_.begin() + to, std::move(moved));
  if (active_ == from) {
    active_ = to;
  } else if (from < active_ && to >= active_) {
    --active_;
  } else if (from > active_ && to <= active_) {
    ++active_;
  }
}

void ShellSessionManager::reset() {
  stop_all();
  sessions_.clear();
  sessions_.push_back(std::make_unique<ShellSession>());
  active_ = 0;
}

void ShellSessionManager::stop_all() {
  for (auto& session : sessions_) {
    session->stop();
  }
}

}  // namespace tuide
