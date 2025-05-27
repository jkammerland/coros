#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <barrier>
#include <coroutine>
#include <nameof.hpp>
#include <random>
#define DOCTEST_CONFIG_IMPLEMENT
#include "async/asio_awaitable.h"
#include "async/asio_concepts.h"
#include "async/coroutine_concepts.h"
#include "async/reference_guard.h"
#include "async/shared_coroutine.h"

#include <asio.hpp>
#include <asio/executor_work_guard.hpp>
#include <async/sender_reciever.h>
#include <atomic>
#include <doctest/doctest.h>
#include <future>
#include <infrastructure/move_only_function.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

using namespace rebuild;
using namespace async;
using namespace std::chrono_literals;

struct AwaitableAsio : std::enable_shared_from_this<AwaitableAsio> {
    asio::io_context &io_;

    explicit AwaitableAsio(asio::io_context &io) : io_(io) {}

    bool await_ready() { return false; }

    template <typename T> void await_suspend(std::coroutine_handle<T> handle) {
        asio::post(io_, [weak_this = this->weak_from_this(), handle]() {
            if (auto lock = weak_this.lock()) {
                // Only resume if "this" is alive
                handle.resume();
            }
        });
    }

    void await_resume() {}
};

asio::awaitable<void> awaitable0() { co_return; }

SharedTask coro0(reference<asio::io_context> io) {
    co_await *std::make_shared<AwaitableAsio>(io.get());
    spdlog::info("After first resume");

    [[maybe_unused]] auto h = co_await detail::GetCoroutineHandle<SharedTask::promise_type>{};

    asio::steady_timer timer(io.get());
    timer.expires_after(1s);
    auto awaitable = timer.async_wait(asio::use_awaitable);
    static_assert(IsAsioAwaitableObject<decltype(awaitable)>);
    spdlog::info("Type timer wait: {}", nameof::nameof_type<decltype(awaitable)>());

    co_await AsioAwaitable(io.get(), timer.async_wait(asio::use_awaitable));
    co_await AsioAwaitable(io.get(), awaitable0());

    co_return;
}

TEST_CASE("Resume asio coro") {
    auto       l_io = reference_guarded<asio::io_context>{};
    TaskHandle task = coro0(l_io.make_reference());
    auto       io   = l_io.make_reference();
    task->try_resume();
    io.get().run();
}

asio::awaitable<void> acoro0(reference<asio::io_context> io_ref) {
    spdlog::info("Hello world");
    [[maybe_unused]] auto &io = io_ref.get();
    asio::steady_timer     timer(io);
    timer.expires_after(1s);

    CHECK_EQ(&io, &timer.get_executor().context());

    co_await timer.async_wait(asio::use_awaitable);

    co_return;
}

TEST_CASE("Asio coro") {
    auto io_guard = reference_guarded<asio::io_context>{};
    auto io_ref   = io_guard.make_reference();
    asio::co_spawn(io_ref.get(), acoro0(io_guard.make_reference()), asio::detached);
    io_ref.get().run();
}

TEST_CASE("Plain lambda") {
    auto io_guard = reference_guarded<asio::io_context>{};
    auto io_ref   = io_guard.make_reference();

    asio::co_spawn(io_ref.get(), [io = io_guard.make_reference()]() -> asio::awaitable<void> { co_return; }, asio::detached);
    io_ref.get().run();
}

TEST_CASE("Test asio awaitables concepts") {
    CHECK(IsAsioAwaitableFunction<decltype(acoro0)>);
    CHECK(!IsAsioAwaitableFunction<decltype(coro0)>);
}

template <typename Reciever> asio::awaitable<void> resume_coro0(Reciever handle, int expected = 2) {
    auto exec = co_await asio::this_coro::executor;

    auto i = co_await resumption(handle, asio::use_awaitable, exec);

    CHECK_EQ(i, expected);
    co_return;
}

TEST_CASE("custom event set resume - resume after setting") {

    asio::io_context io;
    auto             h = make_sender<int>();

    auto fut = asio::co_spawn(io, resume_coro0(h.make_reciever()), asio::use_future);
    io.run_for(2s);
    REQUIRE(h.has_reciever());
    CHECK(h.send(2));
    io.poll();
    fut.get();
}

TEST_CASE("replace reciever - ASAN is required for this test") {
    asio::io_context io;

    auto h = make_sender<int>();
    asio::co_spawn(io, resume_coro0(h.make_reciever()), asio::detached);
    h.make_reciever();
    CHECK(!h.send(1));
    io.run_for(2s);
}

TEST_CASE("custom event set resume - resume before setting") {
    asio::io_context io;

    auto h = make_sender<int>();
    asio::co_spawn(io, resume_coro0(h.make_reciever(), 3), asio::detached);
    REQUIRE(h.has_reciever());
    REQUIRE(h.send(3));
    io.run_for(2s);
}

template <typename Reciever> asio::awaitable<void> loop0(Reciever handle, int expected) {
    auto exec = co_await asio::this_coro::executor;

    for (int j = 0; j < 3; ++j) {
        auto [i] = co_await resumption(handle, asio::as_tuple(asio::use_awaitable), exec);
        CHECK_EQ(i, expected + j);
    }

    co_return;
}

template <typename Reciever> asio::awaitable<void> loop1(Reciever handle, int expected) {
    auto exec = co_await asio::this_coro::executor;

    for (int j = 0; j < 3; ++j) {
        auto [i, str] = co_await resumption(handle, asio::as_tuple(asio::use_awaitable), exec);
        CHECK_EQ(i, expected + j);
        CHECK(!str.empty());
    }

    co_return;
}

TEST_CASE("custom event set resume - multiple resumes") {
    asio::io_context io;

    auto h = make_sender<int>();
    asio::co_spawn(io, loop0(h.make_reciever(), 4), asio::detached);
    CHECK(h.send(4));
    CHECK(h.send(5));
    CHECK(h.send(6));

    io.run_for(1s);

    REQUIRE(!h.has_reciever());
    CHECK(!h.send(7));
}

TEST_CASE("multi arg resume") {
    asio::io_context io;

    auto [s, r] = make_sender_reciever_pair<int, std::string>();
    asio::co_spawn(io, loop1(std::move(r), 4), asio::detached);
    CHECK(s.send(4, "hello"));
    CHECK(s.send(5, "world"));
    CHECK(s.send(6, "!"));

    io.run_for(1s);

    REQUIRE(!s.has_reciever());
    CHECK(!s.send(7, ""));
}

asio::awaitable<void> pinger(auto reciever, auto sender) {
    auto exec = co_await asio::this_coro::executor;
    int  x    = 0;
    for (;;) {
        spdlog::info("sending pinger");
        sender(std::forward<int>(x));
        spdlog::info("awaiting pinger");
        x = co_await awaitable_resumption(reciever, exec);
        spdlog::info("pinger x: {}", x);
    }
    spdlog::info("Exit pinger");
    co_return;
}

asio::awaitable<void> ponger(auto reciever, auto sender) {
    auto exec = co_await asio::this_coro::executor;
    for (;;) {
        spdlog::info("awaiting ponger");
        auto x = co_await awaitable_resumption(reciever, exec);
        spdlog::info("ponger x: {}", x);

        if (x > 10) {
            co_return;
        }

        spdlog::info("sending ponger");
        sender(x + 1);
        // std::this_thread::sleep_for(500ms);
    }
    spdlog::info("Exit ponger");
    co_return;
}

TEST_CASE("ping pong") {
    asio::io_context io;
    auto             s1 = make_sender<int>();
    auto             s2 = make_sender<int>();
    auto             r2 = s2.make_reciever();
    asio::co_spawn(io, pinger(s1.make_reciever(), std::move(s2)), asio::detached);
    asio::co_spawn(io, ponger(std::move(r2), std::move(s1)), asio::detached);
    io.run_for(1s);
}

asio::awaitable<void> ping_pong() {
    using namespace asio::experimental;
    using namespace asio::experimental::awaitable_operators;
    auto exec = co_await asio::this_coro::executor;

    auto s1 = make_sender<int>();
    auto s2 = make_sender<int>();
    auto r2 = s2.make_reciever();

    spdlog::info("Starting PING PONG ROUTINE");
    asio::co_spawn(exec, pinger(s1.make_reciever(), std::move(s2)), [](auto exception_ptr) {
        if (exception_ptr)
            std::rethrow_exception(exception_ptr);
    });

    co_await ponger(std::move(r2), std::move(s1));

    co_return;
}

TEST_CASE("Experimental operator &&") {
    asio::io_context io;
    asio::co_spawn(io, ping_pong(), asio::detached);
    io.run_for(1s);
}

asio::awaitable<void> can_throw(auto reciever) {
    auto exec = co_await asio::this_coro::executor;
    for (;;) {
        auto x = co_await awaitable_resumption(reciever, exec);

        if (std::holds_alternative<std::exception_ptr>(x)) {
            std::rethrow_exception(std::get<std::exception_ptr>(x));
        }
    }
}

TEST_CASE("Test interruption - via exception") {
    asio::io_context io;

    auto sender = make_sender<std::variant<int, std::exception_ptr>>();

    asio::co_spawn(io, can_throw(sender.make_reciever()), [](std::exception_ptr e) {
        if (e)
            std::rethrow_exception(e);
    });

    sender.send(std::make_exception_ptr(std::runtime_error("test exception")));

    CHECK_THROWS_AS(io.poll(), std::runtime_error);
}

TEST_CASE("Test cancellation") {
    asio::io_context io;

    auto sender = make_sender<std::variant<int, std::exception_ptr>>();

    // Create a cancellation signal
    asio::cancellation_signal cancel_signal;

    asio::co_spawn(io, can_throw(sender.make_reciever()), asio::bind_cancellation_slot(cancel_signal.slot(), [](std::exception_ptr e) {
                       if (e)
                           std::rethrow_exception(e);
                   }));

    // Cancel before polling
    cancel_signal.emit(asio::cancellation_type::all);

    // Poll the io_context
    CHECK_THROWS(io.poll());
}

asio::awaitable<void> capture_cancel() {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_partial_cancellation());

    auto exec = co_await asio::this_coro::executor;

    asio::steady_timer t(exec, asio::chrono::seconds(100));

    spdlog::info("Waiting for timer to expire");

    co_await t.async_wait(asio::use_awaitable);

    spdlog::info("Done");
    co_return;
}

TEST_CASE("Capture cancellation") {
    asio::io_context io;

    asio::cancellation_signal cancel_signal;
    asio::co_spawn(io, capture_cancel(), asio::bind_cancellation_slot(cancel_signal.slot(), asio::detached));

    // Poll the io_context
    CHECK_NOTHROW(io.poll());

    cancel_signal.emit(asio::cancellation_type::partial);

    // Poll the io_context
    CHECK_NOTHROW(io.run());
}

int main(int argc, char **argv) {
    spdlog::set_level(spdlog::level::trace);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] [thread %t] %v");
    doctest::Context context;
    context.applyCommandLine(argc, argv);

    int res = context.run();
    if (context.shouldExit()) {
        return res;
    }
    int client_stuff_return_code = 0;
    return res + client_stuff_return_code;
}