#pragma once
#include <infrastructure/move_only_function.h>
#include <memory>

namespace rebuild::async {

template <typename R, typename... Args> struct asio_setter : std::enable_shared_from_this<asio_setter<R, Args...>> {
    using signature = R(Args...);
    using ptr       = std::shared_ptr<asio_setter>;
    using weak_ptr  = ptr::weak_type;

    rebuild::move_only_function<R(Args...)> f_{nullptr};

    // Asio entrypoint for async_compose. Will make the handle ready
    template <typename Self> void operator()(Self &&self) {
        f_ = [self = std::move(self)](Args &&...args) mutable { self.complete(std::forward<Args>(args)...); };
    }
};

template <typename Signature> struct setable_resume;

template <typename R, typename... Args> struct setable_resume<R(Args...)> {
    using signature = R(Args...);

    setable_resume() : holder_(std::make_shared<asio_setter<R, Args...>>()) {}

    ~setable_resume() { holder_->f_ = nullptr; }

    void resume(Args &&...args)
        requires std::is_void_v<R>
    {
        assert(this->is_set() && "The resume function has not yet been set, or has been cleared");
        holder_->f_(std::forward<Args>(args)...);
        holder_->f_ = nullptr;
    }

    auto resume(Args &&...)
        requires(!std::is_void_v<R>)
    {
        throw std::runtime_error("not implemented");
    }

    bool is_set() const { return static_cast<bool>(holder_->f_); }

    auto weak_ptr() { return holder_->weak_from_this(); }

  private:
    asio_setter<R, Args...>::ptr holder_;
};
} // namespace rebuild::async