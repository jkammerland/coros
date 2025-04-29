#pragma once

#include <asio/awaitable.hpp>

template <typename Awaitable>
concept IsAsioAwaitableObject = requires {
    typename Awaitable::value_type;
    requires std::same_as<std::remove_cvref_t<Awaitable>, asio::awaitable<typename Awaitable::value_type, asio::any_io_executor>>;
};

template <typename> struct function_traits;

// Specialization for regular functions
template <typename Return, typename... Args> struct function_traits<Return(Args...)> {
    using return_type = Return;
    using args_type   = std::tuple<Args...>;
};

// Specialization for member functions
template <typename ClassType, typename Return, typename... Args> struct function_traits<Return (ClassType::*)(Args...)> {
    using return_type = Return;
    using args_type   = std::tuple<Args...>;
};

// Specialization for const member functions
template <typename ClassType, typename Return, typename... Args> struct function_traits<Return (ClassType::*)(Args...) const> {
    using return_type = Return;
    using args_type   = std::tuple<Args...>;
};

// Primary template for general callable objects
template <typename F, typename = void> struct is_asio_awaitable_function : std::false_type {};

// Specialization for callable objects (including lambdas)
template <typename F> struct is_asio_awaitable_function<F, std::void_t<decltype(&std::remove_cvref_t<F>::operator())>> {

    // Extract the member function type
    using member_func_type = decltype(&std::remove_cvref_t<F>::operator());

    // Get the return type of the call operator
    using return_type = typename function_traits<member_func_type>::return_type;

    // Check if the return type is an asio::awaitable
    static constexpr bool value = IsAsioAwaitableObject<return_type>;
};

// Specialization for function pointers
template <typename R, typename... Args> struct is_asio_awaitable_function<R (*)(Args...)> {
    static constexpr bool value = IsAsioAwaitableObject<R>;
};

// Specialization for regular functions
template <typename R, typename... Args> struct is_asio_awaitable_function<R(Args...)> {
    static constexpr bool value = IsAsioAwaitableObject<R>;
};

// The concept using the trait
template <typename F>
concept IsAsioAwaitableFunction = is_asio_awaitable_function<std::remove_cvref_t<F>>::value;

template <typename T>
concept IsAsioAwaitable = IsAsioAwaitableObject<T> || IsAsioAwaitableFunction<T>;