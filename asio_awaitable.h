#pragma once
#include "async/asio_concepts.h"

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>

// Helper to extract value_type from awaitable
template <typename T> struct awaitable_traits {
    using value_type = typename T::value_type;
};

template <IsAsioAwaitable T> struct AsioAwaitable {
  private:
    asio::io_context &io_;
    T                 expr_;

    using awaitable_return_t = typename T::value_type;
    struct dummy {};
    static constexpr bool is_awaitable_return_void = std::is_void_v<awaitable_return_t>;
    using ResultType                               = std::conditional_t<is_awaitable_return_void, dummy, awaitable_return_t>;
    [[maybe_unused]] ResultType expr_result_;

  public:
    // Constructor for asio::awaitable objects
    template <typename Awaitable>
    explicit AsioAwaitable(asio::io_context &io, Awaitable &&expr) : io_(io), expr_(std::forward<Awaitable>(expr)) {}

    bool await_ready() const { return io_.stopped(); }

    template <typename U> auto await_suspend(std::coroutine_handle<U> handle) {
        asio::co_spawn(
            this->io_,
            [this, weak = handle.promise().weak_from_this()]() -> asio::awaitable<void> {
                if constexpr (is_awaitable_return_void) {
                    co_await std::forward<T>(this->expr_);
                } else {
                    this->expr_result_ = co_await std::forward<T>(this->expr_);
                }

                // Need this post to avoid deadlocking yourself, incase of manual resumption
                asio::post([weak]() {
                    // TODO: Only resume on
                    if (auto shared = weak.lock()) {
                        shared->resume();
                    }
                });
            },
            asio::detached);
    }

    auto await_resume() {
        if constexpr (!is_awaitable_return_void) {
            return expr_result_;
        }
    }
};

// Deduction guide for awaitable objects
template <typename AwaitableType> AsioAwaitable(asio::io_context &, AwaitableType &&) -> AsioAwaitable<AwaitableType>;