#pragma once
#include <concepts>
#include <coroutine>

namespace detail {
template <typename U> struct GetCoroutineHandle {
    std::coroutine_handle<U> handle_;

    bool await_ready() { return false; }

    bool await_suspend(std::coroutine_handle<U> return_me) {
        handle_ = return_me;
        return false;
    }

    auto await_resume() { return handle_; }
};

template <> struct GetCoroutineHandle<void> {
    std::coroutine_handle<> handle_;

    bool await_ready() { return false; }

    bool await_suspend(std::coroutine_handle<> return_me) {
        handle_ = return_me;
        return false;
    }

    auto await_resume() { return handle_; }
};
} // namespace detail

template <typename T>
concept IsSharedTask = requires(T t) {
    typename T::promise_type;
    // { T::promise_type::is_detached } -> std::convertible_to<bool>;
};

template <IsSharedTask T> auto shared_handle_from_this() {
    auto handle = detail::GetCoroutineHandle<typename T::promise_type>{};
    return handle->promise().shared_from_this();
}