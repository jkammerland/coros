#include <asio.hpp>
#include <chrono>
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <iostream>
#include <spdlog/spdlog.h>

using namespace asio;
using namespace std::chrono_literals;

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