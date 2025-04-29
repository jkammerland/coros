#pragma once

#include <coroutine>
#include <exception>

template <typename Coroutine, typename T> struct promise0 {
    using coroutine_type = Coroutine;
    using promise_type   = promise0<Coroutine, T>;
    using return_type    = T;

    // method to retrieve the result
    T &result() { return result_; }

    coroutine_type get_return_object() { return coroutine_type{std::coroutine_handle<promise0>::from_promise(*this)}; }

    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }

    auto return_value(T t) { result_ = t; }

    void unhandled_exception() { std::terminate(); }

  private:
    // member to store the result
    T result_;
};

template <typename Coroutine> struct promise0<Coroutine, void> {
    using coroutine_type = Coroutine;
    using promise_type   = promise0<Coroutine, void>;
    using return_type    = void;

    coroutine_type get_return_object() { return coroutine_type{std::coroutine_handle<promise0>::from_promise(*this)}; }

    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }

    void return_void() {}
    void unhandled_exception() { std::terminate(); }
};
