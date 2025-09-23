#pragma once
#include <pulse/core/scheduler.hpp>
#include <condition_variable>
#include <queue>
#include <thread>
#include <vector>
#include <mutex>
#include <functional>
#include <atomic>

namespace pulse {

class thread_pool final : public executor {
public:
  explicit thread_pool(std::size_t threads = std::thread::hardware_concurrency())
  : stop_(false) {
    if (threads == 0) threads = 1;
    workers_.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i) {
      workers_.emplace_back([this]{
        for (;;) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [&]{ return stop_ || !q_.empty(); });
            if (stop_ && q_.empty()) return;
            task = std::move(q_.front());
            q_.pop();
          }
          task();
        }
      });
    }
  }

  ~thread_pool() override {
    {
      std::lock_guard<std::mutex> lock(m_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
  }

  void post(std::function<void()> f) override {
    {
      std::lock_guard<std::mutex> lock(m_);
      q_.push(std::move(f));
    }
    cv_.notify_one();
  }

  thread_pool(const thread_pool&) = delete;
  thread_pool& operator=(const thread_pool&) = delete;

private:
  std::mutex m_;
  std::condition_variable cv_;
  std::queue<std::function<void()>> q_;
  std::vector<std::thread> workers_;
  bool stop_;
};

} // namespace pulse
