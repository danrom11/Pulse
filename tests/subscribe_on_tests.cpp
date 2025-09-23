#include <cassert>
#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#include <pulse/pulse.hpp>

using namespace pulse;

// The simplest single-threaded executor with its own worker thread.
class single_thread_executor : public executor {
public:
  single_thread_executor() : stop_(false) {
    thr_ = std::thread([this]{
      worker_id_ = std::this_thread::get_id();
      std::unique_lock<std::mutex> lk(m_);
      for (;;) {
        if (stop_ && q_.empty()) break;
        if (q_.empty()) {
          cv_.wait(lk, [this]{ return stop_ || !q_.empty(); });
          continue;
        }
        auto f = std::move(q_.front()); q_.pop();
        lk.unlock();
        f();
        lk.lock();
      }
    });
  }

  ~single_thread_executor() override {
    {
      std::lock_guard<std::mutex> lk(m_);
      stop_ = true;
    }
    cv_.notify_all();
    if (thr_.joinable()) thr_.join();
  }

  void post(std::function<void()> f) override {
    {
      std::lock_guard<std::mutex> lk(m_);
      q_.push(std::move(f));
    }
    cv_.notify_one();
  }

  std::thread::id worker_id() const { return worker_id_; }

private:
  std::thread thr_;
  std::thread::id worker_id_{};
  std::queue<std::function<void()>> q_;
  std::mutex m_;
  std::condition_variable cv_;
  bool stop_;
};

// A synchronous observable that, when subscribed:
//    - emits the current thread_id,
//    - terminates the thread
static observable<std::thread::id> emit_current_thread_then_done() {
  return observable<std::thread::id>::create(
    [](auto on_next, auto, auto on_done){
      if (on_next) on_next(std::this_thread::get_id());
      if (on_done) on_done();
      return subscription{};
    }
  );
}

int main() {
  // 1) The subscription is actually executed in the executor thread
  {
    auto ex = std::make_shared<single_thread_executor>();
    std::thread::id got{};
    bool completed = false;

    auto sub = (emit_current_thread_then_done()
      | subscribe_on(ex)
    ).subscribe(
      [&](std::thread::id id){ got = id; },
      nullptr,
      [&]{ completed = true; }
    );

    // let's wait in line (it's a bit of a crutch, but it's enough for the test)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    assert(got == ex->worker_id() && "subscribe_on: emissions during subscribe must run on executor thread");
    assert(completed && "on_completed must be called");
  }

  // 2) Unsubscribe before actually subscribing - nothing should fly in / fall out
  {
    single_thread_executor ex;
    bool got = false;

    // Source: slow subscription (emulating heavy work when subscribing)
    auto src = observable<int>::create([](auto on_next, auto, auto){
      // если сюда зайдём — сразу что-то отправим
      if (on_next) on_next(42);
      return subscription{};
    });

    auto obs = src | subscribe_on(ex);

    subscription sub = obs.subscribe([&](int){ got = true; });
    // We unsubscribe immediately - the task might not have started yet
    sub.reset();

    // wait a little bit to make sure the queue is finished
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    assert(!got && "no emissions expected after early unsubscribe");
  }

  // 3) Throwing errors and completions from the executor's subscribe context
  {
    single_thread_executor ex;
    std::thread::id tid_emit{};
    bool got_err = false;

    auto src = observable<int>::create([](auto on_next, auto on_err, auto){
      if (on_next) on_next(1);
      if (on_err) on_err(std::make_exception_ptr(std::runtime_error("boom")));
      return subscription{};
    });

    auto sub = (src | subscribe_on(ex)).subscribe(
      [&](int){ tid_emit = std::this_thread::get_id(); },
      [&](std::exception_ptr){ got_err = true; }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(tid_emit == ex.worker_id() && "on_next should run on executor thread (since it happens during subscribe)");
    assert(got_err && "error must be delivered");
  }

  std::cout << "[subscribe_on_tests] OK\n";
  return 0;
}
