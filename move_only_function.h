#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace rebuild {

// The feature test macro __cpp_lib_move_only_function is specifically designed to detect the availability of the std::move_only_function
// feature in the standard library, which was introduced in C++23. The value 202110L represents the date when the feature was added to the
// standard (October 2021).
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L && !defined(MOVE_ONLY_FUNCTION_CUSTOM_IMPL)

// Use std::move_only_function if available
#include <functional>
template <typename Signature> using move_only_function = std::move_only_function<Signature>;

#else

// Custom implementation for pre-C++23

// Primary template
template <typename Signature> class move_only_function;

// Helper for invoking with void vs non-void return types
template <typename R, typename F, typename... Args> R invoke_and_return(F &&f, Args &&...args) {
    if constexpr (std::is_void_v<R>) {
        std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    } else {
        return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    }
}

// Specialization for any function signature R(Args...)
template <typename R, typename... Args> class move_only_function<R(Args...)> {
  private:
    // Type-erased interface for callable objects
    struct callable_base {
        virtual R invoke(Args...) = 0;
        virtual ~callable_base()  = default;
    };

    // Concrete implementation for specific callable types
    template <typename Callable> struct callable_impl : callable_base {
        Callable func;

        template <typename F> explicit callable_impl(F &&f) : func(std::forward<F>(f)) {}

        R invoke(Args... args) override { return invoke_and_return<R>(func, std::forward<Args>(args)...); }
    };

    // Storage for the callable
    std::unique_ptr<callable_base> callable;

  public:
    // Default constructor (empty function)
    move_only_function() noexcept = default;

    // Nullptr constructor (empty function)
    move_only_function(std::nullptr_t) noexcept : move_only_function() {}

    // Move constructor
    move_only_function(move_only_function &&other) noexcept = default;

    // Copy constructor deleted (move-only semantics)
    move_only_function(const move_only_function &) = delete;

    // Constructor from callable
    template <typename F>
    move_only_function(F &&f)
        requires(!std::is_same_v<std::decay_t<F>, move_only_function> && std::is_invocable_r_v<R, F, Args...>)
    {
        callable = std::make_unique<callable_impl<std::decay_t<F>>>(std::forward<F>(f));
    }

    // Move assignment
    move_only_function &operator=(move_only_function &&other) noexcept = default;

    // Copy assignment deleted (move-only semantics)
    move_only_function &operator=(const move_only_function &) = delete;

    // Assignment from nullptr
    move_only_function &operator=(std::nullptr_t) noexcept {
        callable.reset();
        return *this;
    }

    // Assignment from callable
    template <typename F>
    move_only_function &operator=(F &&f)
        requires(!std::is_same_v<std::decay_t<F>, move_only_function> && std::is_invocable_r_v<R, F, Args...>)
    {
        move_only_function tmp(std::forward<F>(f));
        callable = std::move(tmp.callable);
        return *this;
    }

    // Function call operator
    R operator()(Args... args) const {
        assert(callable && "callbable is nullptr");
        return callable->invoke(std::forward<Args>(args)...);
    }

    // Boolean conversion operator
    explicit operator bool() const noexcept { return callable != nullptr; }

    // Swap function
    void swap(move_only_function &other) noexcept { callable.swap(other.callable); }
};

// Non-member swap
template <typename Signature> void swap(move_only_function<Signature> &lhs, move_only_function<Signature> &rhs) noexcept { lhs.swap(rhs); }

#endif

} // namespace rebuild