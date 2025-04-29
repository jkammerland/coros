#pragma once

#include <cassert>
#include <coroutine>

template <typename T, template <typename...> typename Promise>
class unique_coroutine {
public:
  using coroutine_type = unique_coroutine<T, Promise>;
  using promise_type = Promise<coroutine_type, T>;

  // Constructor and destructor
  unique_coroutine(std::coroutine_handle<promise_type> h) : handle_(h) {}
  ~unique_coroutine() {
    if (handle_) {
      handle_.destroy();
    }
  }

  // Non-copyable
  unique_coroutine(const unique_coroutine &) = delete;
  unique_coroutine &operator=(const unique_coroutine &) = delete;

  // Movable
  unique_coroutine(unique_coroutine &&other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }
  unique_coroutine &operator=(unique_coroutine &&other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  void resume() {
    assert(handle_ != nullptr);
    if (!handle_.done()) {
      handle_.resume();
    }
  }

  bool is_done() {
    assert(handle_ != nullptr);
    return handle_.done();
  }

private:
  std::coroutine_handle<promise_type> handle_;
};
