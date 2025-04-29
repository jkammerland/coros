#include <asio/asio.hpp>

// 1. Simplest possible
asio::awaitable<void> f2(asio::steady_timer &tim) {
    return asio::async_compose<decltype(asio::use_awaitable), void()>(
        [&tim](auto &&self) { tim.async_wait([self = std::move(self)](auto) mutable { self.complete(); }); }, asio::use_awaitable);
}
// ----------------------------

// Example for 1.
asio::awaitable<void> cancel_timer(asio::steady_timer &tim) {
    spdlog::info("Before sleep");
    co_await f2(tim);
    spdlog::info("After sleep");
}

// 2. With return value
struct simple_wait_implementation {
    asio::steady_timer &timer_;

    template <typename Self> void operator()(Self &&self) {
        // Just perform a single asynchronous wait operation
        timer_.async_wait([self = std::move(self)](auto ec) mutable {
            // Complete the operation with the error code and a random int
            self.complete(ec, 5);
        });
    }
};

// Wrapper function that uses async_compose
template <typename CompletionToken> auto async_wait_wrapper(asio::steady_timer &timer, CompletionToken &&token) {
    // Call async_compose on simple_wait_implementation
    return asio::async_compose<CompletionToken, /* Your .complete(...) signature */ void(asio::error_code, int)>(
        simple_wait_implementation{timer}, token);
}
// ----------------------------

// Example for 2.
asio::awaitable<asio::error_code> wait_with_error_code(asio::steady_timer &timer) {
    std::cout << "Before wait\n";

    // The key part: we're using as_tuple to get the error code and integer
    auto [ec, a] = co_await async_wait_wrapper(timer, asio::as_tuple(asio::use_awaitable));

    std::cout << "After wait, error code: " << ec.message() << "\n";
    std::cout << a << "\n";

    co_return ec;
}