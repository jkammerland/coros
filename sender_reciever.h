#pragma once
#ifdef ASIO_STANDALONE
#include <asio/as_tuple.hpp>
#include <asio/compose.hpp>
#include <asio/use_awaitable.hpp>
#else
#include <boost/asio/compose.hpp>
#endif
#include <infrastructure/move_only_function.h>
#include <memory>
#include <queue>
#include <utility>

namespace rebuild::async {

template <typename Resumption, typename CompletionToken, typename Executor>
auto resumption(Resumption &r, CompletionToken &&token, Executor &&exec) {
    return asio::async_compose<CompletionToken, typename Resumption::signature>(
        [&r, &exec]<typename Self>(Self &&self) {
            auto can_complete = r(std::forward<Self>(self), std::forward<Executor>(exec));
            if (!can_complete) {
                throw std::runtime_error("No sender or queue empty");
            }
        },
        token, exec);
}

template <typename... Args> struct sender;
template <typename... Args> struct reciever;

template <typename... Args> struct holder {
    using ptr       = std::shared_ptr<holder>;
    using signature = void(Args...);

    // Asio entrypoint for async_compose. Will make the handle ready
    template <typename Self, typename Executor>
    [[nodiscard("if false, then the sender is gone AND the queue empty")]] bool operator()(Self &&self, Executor &&exec) {

        // These two if-statements below do "self.complete(Args...)", one is resolved immediately, the other is posted
        if (!args_.empty()) /* resumption already available, complete immediately  */ {
            auto front_args_tuple = std::move(args_.front());
            args_.pop();
            std::apply([&self /* completed here, so we can capture by ref */](
                           Args &&...unpacked_args) mutable { std::forward<Self>(self).complete(std::forward<Args>(unpacked_args)...); },
                       std::move(front_args_tuple));
            return true;
        } else if (this->has_active_sender()) {
            f_ = [self = std::move(self) /* must be moved, deferred complete */, exec](Args &&...args) mutable {
                asio::post(exec, [self = std::move(self), args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                    std::apply([&self](auto &&...captured_args) { self.complete(std::forward<decltype(captured_args)>(captured_args)...); },
                               std::move(args));
                });
            };
            return true;
        }

        return false;
    }

  private:
    friend sender<Args...>;
    friend reciever<Args...>;
    static constexpr auto make_holder() { return std::make_shared<holder>(); }

    bool has_active_sender() const { return not static_cast<bool>(f_); }
    bool has_ready_reciever() const { return static_cast<bool>(f_); }

    std::queue<std::tuple<Args...>>        args_;
    rebuild::move_only_function<signature> f_{nullptr};
    bool                                   alive_{true};
};

template <typename... Args> struct reciever {
    using signature = void(Args...);

    reciever(holder<Args...>::ptr holder) : holder_(std::move(holder)) {
        if (!holder_) {
            throw std::runtime_error("Constructed with nullptr. Cannot create a reciever with no holder");
        }
    }

    reciever(const sender<Args...> &sender) : holder_(sender.holder_) {
        if (!holder_) {
            throw std::runtime_error("Constructed with nullptr. Cannot create a reciever with no holder");
        }
    }

    ~reciever() {
        if (holder_) {
            // Make sender know that reciever has is gone.
            holder_->alive_ = false;
        }
    }

    // Deleted copy constructor and copy assignment operator
    reciever &operator=(const reciever &) = delete;
    reciever(const reciever &) noexcept   = delete;

    // Non-deleted move constructor and move assignment operator
    reciever(reciever &&other) noexcept            = default;
    reciever &operator=(reciever &&other) noexcept = default;

    bool has_sender() const {
        assert(this->holder_ && "Missing shared state, this reciever is not alive. Must've been moved from");
        // If lambda is set by anyone else but the reciever, then the sender has destructed.
        // This is garantueed when the reciever is unique.
        return holder_->has_active_sender();
    }

    template <typename Self, typename Executor>
    [[nodiscard("if false, then the sender is gone AND the queue empty")]] bool operator()(Self &&self, Executor &&exec) {
        assert(this->holder_ && "Missing shared state, this reciever is not alive. Must've been moved from");
        // Forward the asio continuation to the holder
        return (*holder_)(std::forward<Self>(self), std::forward<Executor>(exec));
    }

  private:
    holder<Args...>::ptr holder_;
};

template <typename... Args> struct sender {
    using signature = void(Args...);

    sender() : sender(holder<Args...>::make_holder()) {}

    sender(holder<Args...>::ptr holder) : holder_(std::move(holder)) {
        if (!holder_) {
            throw std::runtime_error("Constructed with nullptr. Cannot create a sender with no holder");
        }
    }

    ~sender() {
        // Signal to the receiver that it has lost sender. By setting no-op lambda.
        if (holder_) {
            // Make reciever know the sender is gone.
            holder_->f_ = [](Args &&...) {};
        }
    }

    // Deleted copy constructor and copy assignment operator
    sender(const sender &)                     = delete;
    sender &operator=(const sender &) noexcept = delete;

    // Non-deleted move constructor and move assignment operator
    sender(sender &&other) noexcept            = default;
    sender &operator=(sender &&other) noexcept = default;

    bool operator()(Args &&...args) { return this->send(std::forward<Args>(args)...); }

    bool send(Args &&...args) {
        assert(this->holder_ && "Missing shared state, this sender is not alive. Must've been moved from");
        if (this->has_reciever()) {
            if (holder_->has_ready_reciever()) {
                holder_->f_(std::forward<Args>(args)...);
            } else {
                holder_->args_.emplace(std::forward<Args>(args)...);
            }
            holder_->f_ = nullptr;
            return true;
        }
        return false;
    }

    bool has_reciever() const {
        assert(this->holder_ && "Missing shared state, this sender is not alive. Must've been moved from");
        return holder_->alive_;
    }

    auto make_reciever() {
        assert(this->holder_ && "Missing shared state, this sender is not alive. Must've been moved from");
        return reciever<Args...>(holder_);
    }

  private:
    holder<Args...>::ptr holder_;
};

template <typename... Args> auto make_sender() { return sender<Args...>(); }

template <typename Sender> auto make_reciever_from(const Sender &sender) { return sender.make_reciever(); }

template <typename... Args> auto make_sender_reciever_pair() {
    auto h = std::make_shared<holder<Args...>>();
    return std::make_pair(sender<Args...>(h), reciever<Args...>(h));
}

template <typename Executor, typename... Args> auto awaitable_resumption(reciever<Args...> &rhs, Executor &exec) {
    constexpr auto n_args = sizeof...(Args);
    if constexpr (n_args <= 1) {
        return resumption(rhs, asio::use_awaitable, exec);
    } else {
        return resumption(rhs, asio::as_tuple(asio::use_awaitable), exec);
    }
}

} // namespace rebuild::async