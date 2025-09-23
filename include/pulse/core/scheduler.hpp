#pragma once
#include <functional>
#include <queue>
#include <mutex>

namespace pulse {

// Basic executor interface
struct executor {
  virtual ~executor() = default;
  virtual void post(std::function<void()> f) = 0;
};

// Synchronous: executes immediately (good for MVP/tests)
struct inline_executor final : executor {
  void post(std::function<void()> f) override { f(); }
};

// Sequential queue (no separate thread, executed by drain())
class strand final : public executor {
public:
  void post(std::function<void()> f) override {
    std::lock_guard<std::mutex> lock(m_);
    q_.push(std::move(f));
  }
  // Explicit task drainage (call from the required thread, for example, the UI thread)
  void drain() {
    for (;;) {
      std::function<void()> f;
      {
        std::lock_guard<std::mutex> lock(m_);
        if (q_.empty()) break;
        f = std::move(q_.front()); q_.pop();
      }
      f();
    }
  }
private:
  std::mutex m_;
  std::queue<std::function<void()>> q_;
};

} // namespace pulse
