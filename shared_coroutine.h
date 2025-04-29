#pragma once

#include <concepts>
#include <coroutine>
// #include <nameof.hpp>
#include <spdlog/spdlog.h>
#include <type_traits>
#include <utility>

template <typename TaskHandle, bool IsDetached = false> struct SharedCoroutine {
  using handle_type = TaskHandle;

  struct promise_type {
    static constexpr bool is_detached = IsDetached;

    // The shared/weak ptr to **
    std::conditional_t<is_detached, typename TaskHandle::Ptr,
                       typename TaskHandle::Ptr::weak_type>
        task;

    auto get_return_object() {
      spdlog::info("get_return_object [SharedCoroutine]");
      return SharedCoroutine(
          std::coroutine_handle<promise_type>::from_promise(*this));
    }
    std::suspend_always initial_suspend() {
      spdlog::info("initial_suspend [SharedCoroutine]");
      return {};
    }
    std::suspend_always final_suspend() noexcept {
      spdlog::info("final_suspend [SharedCoroutine]");
      return {};
    }
    void unhandled_exception() { std::terminate(); }
    void return_void() { spdlog::info("return_void [SharedCoroutine]"); }

    auto shared_from_this() {
      if constexpr (is_detached) {
        return task;
      } else {
        auto shared = task.lock();
        return shared;
      }
    }

    auto weak_from_this() {
      typename TaskHandle::Ptr::weak_type weak_ptr = task;
      return weak_ptr;
    }
  };

  explicit SharedCoroutine(std::coroutine_handle<promise_type> h)
      : task(std::make_shared<TaskHandle>(h)) {
    spdlog::info("explicit SharedCoroutine(handle) [SharedCoroutine]");
    if constexpr (IsDetached) {
      h.promise().task = task->shared_from_this();
    } else {
      h.promise().task = task->weak_from_this();
    }
  }
  ~SharedCoroutine() { spdlog::info("~SharedCoroutine [destructed]"); }

  SharedCoroutine &operator=(const SharedCoroutine &) = delete;

  // Allow implicit conversion to shared_ptr
  operator typename TaskHandle::Ptr() const {
    spdlog::info("implicit SharedTask::Ptr() conversion [SharedCoroutine]");
    return task;
  }

private:
  // Temporary holder, there will be a weak ref to this ptr in **
  TaskHandle::Ptr task;
};

struct TaskImpl : std::enable_shared_from_this<TaskImpl> {
  using Ptr = std::shared_ptr<TaskImpl>;
  using Self = TaskImpl;

  explicit TaskImpl(
      std::coroutine_handle<SharedCoroutine<Self>::promise_type> h)
      : handle_(h) {
    spdlog::debug(
        "explicit TaskImpl(handle<SharedCoroutine<Self>>) [TaskImpl]");
  }
  ~TaskImpl() {
    spdlog::debug("~TaskImpl() [TaskImpl]");
    if (handle_) {
      handle_.destroy();
    }
  }

  bool try_resume() {
    spdlog::debug("try_resume() (handle<SharedCoroutine<Self>>) [TaskImpl]");
    std::unique_lock lock(mutex_, std::defer_lock);

    if (lock.try_lock() && handle_ && !handle_.done()) {
      handle_.resume();
      return true;
    } else {
      return false;
    }
  }

  bool resume() {
    spdlog::debug("resume() (handle<SharedCoroutine<Self>>) [TaskImpl]");
    std::lock_guard lock(mutex_);
    if (handle_ && !handle_.done()) {
      handle_.resume();
      return true;
    } else {
      return false;
    }
  }

  bool is_done() {
    std::lock_guard lock(mutex_);
    if (!handle_ || handle_.done()) {
      return true;
    } else {
      return false;
    }
  }

  std::mutex mutex_;
  std::coroutine_handle<SharedCoroutine<Self>::promise_type> handle_;
};

using SharedTask = SharedCoroutine<TaskImpl>;
using TaskHandle = TaskImpl::Ptr;
