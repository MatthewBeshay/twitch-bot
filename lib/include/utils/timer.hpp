#pragma once

#include <chrono>

class Timer {
public:
    using clock = std::chrono::steady_clock;

    Timer() : _start(clock::now()) {}

    void reset() noexcept { _start = clock::now(); }

    clock::duration elapsed() const noexcept {
        return clock::now() - _start;
    }

    template<typename D>
    auto elapsed_cast() const noexcept {
        return std::chrono::duration_cast<D>(elapsed()).count();
    }

private:
    clock::time_point _start;
};
