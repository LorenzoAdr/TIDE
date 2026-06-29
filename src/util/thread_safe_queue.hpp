#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace tgdb {

template <typename T>
class ThreadSafeQueue {
 public:
  void push(T item) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push(std::move(item));
    }
    cv_.notify_one();
  }

  void push_front(T item) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      std::queue<T> reordered;
      reordered.push(std::move(item));
      while (!queue_.empty()) {
        reordered.push(std::move(queue_.front()));
        queue_.pop();
      }
      queue_.swap(reordered);
    }
    cv_.notify_one();
  }

  std::optional<T> try_pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return std::nullopt;
    }
    T item = std::move(queue_.front());
    queue_.pop();
    return item;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

  std::optional<T> wait_pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) {
      return std::nullopt;
    }
    T item = std::move(queue_.front());
    queue_.pop();
    return item;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_ = {};
    closed_ = false;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<T> queue_;
  bool closed_ = false;
};

}  // namespace tgdb
